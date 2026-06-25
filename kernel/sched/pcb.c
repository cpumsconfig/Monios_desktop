#include "pcb.h"
#include "common.h"
#include "interrupt.h"
#include "pool.h"

#define PCB_MAX 16U

static pcb_t g_pcbs[PCB_MAX];
static uint32_t g_pcb_bitmap[(PCB_MAX + BITMAP_WORD_BITS - 1u) / BITMAP_WORD_BITS];
static pool_t g_pcb_pool;
static uint32_t g_next_pid;
static int32_t g_current_pid;
static char g_pcb_status[64];

static void pcb_copy_text(char *dst, uint32_t size, const char *src)
{
    uint32_t index = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

static void pcb_copy_name_from_path(char *dst, uint32_t size, const char *path)
{
    const char *name = path;

    if (path == NULL) {
        pcb_copy_text(dst, size, "kernel");
        return;
    }
    for (uint32_t index = 0; path[index] != '\0'; index++) {
        if (path[index] == '/' || path[index] == '\\') {
            name = &path[index + 1];
        }
    }
    pcb_copy_text(dst, size, name[0] != '\0' ? name : path);
}

static pcb_t *pcb_find_by_pid(int32_t pid)
{
    for (uint32_t index = 0; index < PCB_MAX; index++) {
        if (g_pcbs[index].used && (int32_t) g_pcbs[index].pid == pid) {
            return &g_pcbs[index];
        }
    }
    return NULL;
}

static int32_t pcb_claim_slot(void)
{
    int32_t slot = pool_alloc_slot(&g_pcb_pool);

    if (slot >= 0) {
        return slot;
    }
    for (uint32_t index = 0; index < PCB_MAX; index++) {
        if (g_pcbs[index].used && g_pcbs[index].state != PCB_STATE_RUNNING) {
            pool_free_slot(&g_pcb_pool, index);
            memset(&g_pcbs[index], 0, sizeof(g_pcbs[index]));
            return pool_alloc_slot(&g_pcb_pool);
        }
    }
    return -1;
}

const char *pcb_state_name(pcb_state_t state)
{
    switch (state) {
    case PCB_STATE_READY:
        return "ready";
    case PCB_STATE_RUNNING:
        return "running";
    case PCB_STATE_EXITED:
        return "exited";
    case PCB_STATE_ABORTED:
        return "aborted";
    case PCB_STATE_STOPPED:
        return "stopped";
    case PCB_STATE_FREE:
    default:
        return "free";
    }
}

void pcb_init(void)
{
    memset(g_pcbs, 0, sizeof(g_pcbs));
    pool_init(&g_pcb_pool, "pcb", g_pcb_bitmap, PCB_MAX);
    g_next_pid = 1;
    g_current_pid = -1;
    strcpy(g_pcb_status, "pcb: ready");
}

int32_t pcb_process_start(const char *path)
{
    int32_t slot = pcb_claim_slot();
    pcb_t *pcb;

    if (slot < 0) {
        strcpy(g_pcb_status, "pcb: pool full");
        return -1;
    }
    pcb = &g_pcbs[slot];
    memset(pcb, 0, sizeof(*pcb));
    pcb->used = true;
    pcb->slot = (uint32_t) slot;
    pcb->pid = g_next_pid++;
    pcb->state = PCB_STATE_RUNNING;
    pcb->start_tick = timer_ticks();
    pcb_copy_text(pcb->path, sizeof(pcb->path), path);
    pcb_copy_name_from_path(pcb->name, sizeof(pcb->name), path);
    g_current_pid = (int32_t) pcb->pid;
    strcpy(g_pcb_status, "pcb: process running");
    return (int32_t) pcb->pid;
}

static void pcb_finish_process(int32_t pid, pcb_state_t state, int32_t exit_code)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return;
    }
    pcb->state = state;
    pcb->exit_code = exit_code;
    pcb->end_tick = timer_ticks();
    if (g_current_pid == pid) {
        g_current_pid = -1;
    }
    strcpy(g_pcb_status, "pcb: process completed");
}

void pcb_process_exit(int32_t pid, int32_t exit_code)
{
    pcb_finish_process(pid, PCB_STATE_EXITED, exit_code);
}

void pcb_process_abort(int32_t pid, int32_t exit_code)
{
    pcb_finish_process(pid, PCB_STATE_ABORTED, exit_code);
}

void pcb_process_stop(int32_t pid)
{
    pcb_finish_process(pid, PCB_STATE_STOPPED, -1);
}

int32_t pcb_current_pid(void)
{
    return g_current_pid;
}

uint32_t pcb_count(void)
{
    uint32_t count = 0;

    for (uint32_t index = 0; index < PCB_MAX; index++) {
        if (g_pcbs[index].used) {
            count++;
        }
    }
    return count;
}

uint32_t pcb_capacity(void)
{
    return PCB_MAX;
}

bool pcb_snapshot(uint32_t index, pcb_t *out)
{
    if (index >= PCB_MAX || out == NULL) {
        return false;
    }
    *out = g_pcbs[index];
    return true;
}

uint32_t pcb_pending_signal_mask(int32_t pid)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    return pcb == NULL ? 0u : pcb->pending_signals;
}

bool pcb_signal_or(int32_t pid, uint32_t mask)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return false;
    }
    pcb->pending_signals |= mask;
    return true;
}

bool pcb_signal_set(int32_t pid, uint32_t mask)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return false;
    }
    pcb->pending_signals = mask;
    return true;
}

const char *pcb_status(void)
{
    return g_pcb_status;
}
