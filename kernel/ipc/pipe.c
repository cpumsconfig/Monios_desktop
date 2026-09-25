#include "pipe.h"
#include "common.h"
#include "memory.h"
#include "pcb.h"
#include "signal.h"
#include "string.h"

/*
 * Anonymous pipe implementation: a 4 KiB ring buffer shared between a reader
 * and a writer, with per-side reference counting.  The buffer lives in kernel
 * heap memory; data is copied directly to/from the caller's buffer (the
 * syscall layer validates user addresses up front).
 */

typedef struct {
    bool used;
    uint8_t *buffer;      /* PIPE_BUF_SIZE kernel buffer */
    uint32_t head;        /* read index  */
    uint32_t tail;        /* write index */
    uint32_t count;       /* bytes present in the buffer */
    uint32_t read_refs;   /* number of open read ends  */
    uint32_t write_refs;  /* number of open write ends */
} pipe_entry_t;

static pipe_entry_t g_pipes[PIPE_MAX_PIPES];

void pipe_init(void)
{
    uint32_t i;

    for (i = 0; i < PIPE_MAX_PIPES; i++) {
        g_pipes[i].used = false;
        g_pipes[i].buffer = NULL;
        g_pipes[i].head = 0;
        g_pipes[i].tail = 0;
        g_pipes[i].count = 0;
        g_pipes[i].read_refs = 0;
        g_pipes[i].write_refs = 0;
    }
}

/* Decode a global pipe handle into its pipe slot and direction.
 * direction_out: 0 = read, 1 = write.  Returns slot index or -1. */
static int32_t pipe_decode_handle(int32_t handle, uint32_t *direction_out)
{
    int32_t adjusted;
    int32_t slot;

    if (handle < PIPE_HANDLE_BASE) {
        return -1;
    }
    adjusted = handle - PIPE_HANDLE_BASE;
    slot = adjusted / 2;
    if (slot < 0 || slot >= (int32_t) PIPE_MAX_PIPES) {
        return -1;
    }
    if (direction_out != NULL) {
        *direction_out = (uint32_t)(adjusted % 2);
    }
    return slot;
}

static void pipe_free_if_idle(pipe_entry_t *pipe)
{
    if (pipe->read_refs == 0 && pipe->write_refs == 0) {
        if (pipe->buffer != NULL) {
            kfree(pipe->buffer);
            pipe->buffer = NULL;
        }
        pipe->used = false;
        pipe->head = 0;
        pipe->tail = 0;
        pipe->count = 0;
    }
}

int32_t pipe_create(int32_t *read_fd, int32_t *write_fd)
{
    pcb_t *cur = pcb_get_current();
    pipe_entry_t *pipe = NULL;
    uint32_t i;
    int32_t rfd;
    int32_t wfd;
    int32_t slot;

    if (cur == NULL || read_fd == NULL || write_fd == NULL) {
        return -1;
    }
    for (i = 0; i < PIPE_MAX_PIPES; i++) {
        if (!g_pipes[i].used) {
            pipe = &g_pipes[i];
            slot = (int32_t) i;
            break;
        }
    }
    if (pipe == NULL) {
        return -1;
    }
    pipe->buffer = (uint8_t *) kmalloc(PIPE_BUF_SIZE);
    if (pipe->buffer == NULL) {
        return -1;
    }
    rfd = pcb_fd_alloc(cur);
    wfd = pcb_fd_alloc(cur);
    if (rfd < 0 || wfd < 0) {
        if (rfd >= 0) {
            pcb_fd_free(cur, rfd);
        }
        if (wfd >= 0) {
            pcb_fd_free(cur, wfd);
        }
        kfree(pipe->buffer);
        pipe->buffer = NULL;
        return -1;
    }
    pipe->used = true;
    pipe->head = 0;
    pipe->tail = 0;
    pipe->count = 0;
    pipe->read_refs = 1;
    pipe->write_refs = 1;

    pcb_fd_set(cur, rfd, PIPE_HANDLE_BASE + slot * 2);
    pcb_fd_set(cur, wfd, PIPE_HANDLE_BASE + slot * 2 + 1);

    *read_fd = rfd;
    *write_fd = wfd;
    return 0;
}

int32_t pipe_read(int32_t fd, void *buf, uint32_t count)
{
    pcb_t *cur = pcb_get_current();
    int32_t handle;
    uint32_t direction;
    int32_t slot;
    pipe_entry_t *pipe;
    uint32_t done = 0;

    if (cur == NULL || buf == NULL || count == 0) {
        return -1;
    }
    handle = pcb_fd_get_handle(cur, fd);
    slot = pipe_decode_handle(handle, &direction);
    if (slot < 0 || direction != 0) {
        return -1; /* not a read end */
    }
    pipe = &g_pipes[slot];
    if (!pipe->used) {
        return -1;
    }

    while (done < count && pipe->count > 0) {
        uint32_t chunk = count - done;
        uint32_t avail = PIPE_BUF_SIZE - pipe->head;

        if (chunk > avail) {
            chunk = avail;
        }
        if (chunk > pipe->count) {
            chunk = pipe->count;
        }
        memcpy((uint8_t *) buf + done, pipe->buffer + pipe->head, chunk);
        pipe->head = (pipe->head + chunk) % PIPE_BUF_SIZE;
        pipe->count -= chunk;
        done += chunk;
    }

    /* EOF: no data left and no writer alive. */
    if (done == 0 && pipe->count == 0 && pipe->write_refs == 0) {
        return 0;
    }
    return (int32_t) done;
}

int32_t pipe_write(int32_t fd, const void *buf, uint32_t count)
{
    pcb_t *cur = pcb_get_current();
    int32_t handle;
    uint32_t direction;
    int32_t slot;
    pipe_entry_t *pipe;
    uint32_t done = 0;

    if (cur == NULL || buf == NULL || count == 0) {
        return -1;
    }
    handle = pcb_fd_get_handle(cur, fd);
    slot = pipe_decode_handle(handle, &direction);
    if (slot < 0 || direction != 1) {
        return -1; /* not a write end */
    }
    pipe = &g_pipes[slot];
    if (!pipe->used) {
        return -1;
    }

    /* No reader alive: signal the writer and report a broken pipe. */
    if (pipe->read_refs == 0) {
        signal_send((int32_t) cur->pid, (uint8_t) PIPE_SIGPIPE);
        return -1;
    }

    while (done < count && (PIPE_BUF_SIZE - pipe->count) > 0) {
        uint32_t chunk = count - done;
        uint32_t avail = PIPE_BUF_SIZE - pipe->tail;
        uint32_t space = PIPE_BUF_SIZE - pipe->count;

        if (chunk > avail) {
            chunk = avail;
        }
        if (chunk > space) {
            chunk = space;
        }
        memcpy(pipe->buffer + pipe->tail, (const uint8_t *) buf + done, chunk);
        pipe->tail = (pipe->tail + chunk) % PIPE_BUF_SIZE;
        pipe->count += chunk;
        done += chunk;
    }
    return (int32_t) done;
}

void pipe_close_fd(int32_t fd)
{
    pcb_t *cur = pcb_get_current();
    int32_t handle;
    uint32_t direction;
    int32_t slot;
    pipe_entry_t *pipe;

    if (cur == NULL) {
        return;
    }
    handle = pcb_fd_get_handle(cur, fd);
    slot = pipe_decode_handle(handle, &direction);
    if (slot < 0) {
        return;
    }
    pipe = &g_pipes[slot];
    if (!pipe->used) {
        return;
    }
    if (direction == 0) {
        if (pipe->read_refs > 0) {
            pipe->read_refs--;
        }
    } else {
        if (pipe->write_refs > 0) {
            pipe->write_refs--;
        }
    }
    pcb_fd_free(cur, fd);
    pipe_free_if_idle(pipe);
}
