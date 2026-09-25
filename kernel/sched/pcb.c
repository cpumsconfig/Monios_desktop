#include "pcb.h"
#include "common.h"
#include "interrupt.h"
#include "pool.h"
#include "memory.h"
#include "vm.h"

#define PCB_MAX 64U

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
    case PCB_STATE_ZOMBIE:
        return "zombie";
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
    /* Default credentials: root (uid=0) until user login sets otherwise. */
    pcb->uid = 0;
    pcb->gid = 0;
    pcb->euid = 0;
    pcb->egid = 0;
    pcb->parent_pid = -1;
    /* Every process starts as its own process-group / session leader. */
    pcb->pgid = (int32_t) pcb->pid;
    pcb->sid = (int32_t) pcb->pid;
    pcb->session_leader = true;
    pcb->wait_target_pid = -1;
    pcb_fd_init(pcb);
    /* Default scheduling parameters. */
    pcb->priority = SCHED_PRIORITY_DEFAULT;
    pcb->time_slice = SCHED_TIMESLICE_TICKS;
    pcb->remaining_ticks = SCHED_TIMESLICE_TICKS;
    g_current_pid = (int32_t) pcb->pid;
    strcpy(g_pcb_status, "pcb: process running");
    return (int32_t) pcb->pid;
}

static void pcb_finish_process(int32_t pid, pcb_state_t state, int32_t exit_code)
{
    pcb_t *pcb = pcb_find_by_pid(pid);
    pcb_t *parent;

    if (pcb == NULL) {
        return;
    }
    pcb->state = state;
    pcb->exit_code = exit_code;
    pcb->end_tick = timer_ticks();
    if (g_current_pid == pid) {
        g_current_pid = -1;
    }
    /* Wake the parent if it is blocked in waitpid() on this child.  The
     * parent resumes as READY so the scheduler can pick it up again. */
    parent = pcb_find_by_pid(pcb->parent_pid);
    if (parent != NULL && parent->state == PCB_STATE_WAITING) {
        int32_t target = parent->wait_target_pid;
        bool matches = false;

        if (target == -1) {
            matches = true;
        } else if (target > 0) {
            matches = (target == pid);
        } else if (target == 0) {
            matches = (parent->pgid == pcb->pgid);
        } else /* target < -1 */ {
            matches = (-target == pcb->pgid);
        }
        if (matches) {
            parent->state = PCB_STATE_READY;
            parent->wait_target_pid = -1;
        }
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

void pcb_process_fault(int32_t pid, uint8_t vector, uint64_t error_code, int32_t exit_code)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return;
    }
    pcb->fault_vector = vector;
    pcb->fault_error_code = error_code;
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

/* ====================================================================== */
/* Feature 7: process management enhancement                              */
/* ====================================================================== */

void pcb_set_parent(int32_t pid, int32_t parent_pid)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb != NULL) {
        pcb->parent_pid = parent_pid;
    }
}

void pcb_account_cpu(int32_t pid, bool in_kernel, uint64_t ticks)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return;
    }
    if (in_kernel) {
        pcb->cpu_kernel_ticks += ticks;
    } else {
        pcb->cpu_user_ticks += ticks;
    }
}

void pcb_set_mem_pages(int32_t pid, uint32_t pages)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb != NULL) {
        pcb->mem_pages = pages;
    }
}

void pcb_add_thread(int32_t pid, int32_t delta)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return;
    }
    if (delta < 0 && pcb->thread_count != 0) {
        pcb->thread_count--;
    } else if (delta > 0) {
        pcb->thread_count++;
    }
}

static void pcb_fill_info(const pcb_t *pcb, process_info_t *out)
{
    uint64_t now = timer_ticks();

    memset(out, 0, sizeof(*out));
    out->pid = (int32_t) pcb->pid;
    out->parent_pid = pcb->parent_pid;
    pcb_copy_text(out->name, sizeof(out->name), pcb->name);
    pcb_copy_text(out->state_name, sizeof(out->state_name), pcb_state_name(pcb->state));
    out->state = pcb->state;
    out->cpu_user_ticks = pcb->cpu_user_ticks;
    out->cpu_kernel_ticks = pcb->cpu_kernel_ticks;
    out->cpu_total_ticks = pcb->cpu_user_ticks + pcb->cpu_kernel_ticks;
    out->uptime_ticks = pcb->end_tick != 0 ? pcb->end_tick - pcb->start_tick
                                            : now - pcb->start_tick;
    out->mem_pages = pcb->mem_pages;
    out->mem_kb = pcb->mem_pages * 4U; /* 4 KiB pages */
    out->thread_count = pcb->thread_count;
}

uint32_t process_enum(process_info_t *out, uint32_t max_entries)
{
    uint32_t filled = 0;

    if (out == NULL || max_entries == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < PCB_MAX && filled < max_entries; index++) {
        if (!g_pcbs[index].used || g_pcbs[index].state == PCB_STATE_FREE) {
            continue;
        }
        pcb_fill_info(&g_pcbs[index], &out[filled]);
        filled++;
    }
    return filled;
}

uint32_t process_enum_all(process_enum_result_t *result)
{
    uint32_t count;

    if (result == NULL) {
        return 0;
    }
    count = process_enum(result->entries, 16);
    result->count = count;
    return count;
}

bool process_stats(int32_t pid, process_info_t *out)
{
    pcb_t *pcb;

    if (out == NULL) {
        return false;
    }
    pcb = pcb_find_by_pid(pid);
    if (pcb == NULL) {
        return false;
    }
    pcb_fill_info(pcb, out);
    return true;
}

int32_t process_terminate(int32_t pid)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb == NULL) {
        return -1;
    }
    /* never let the caller kill the idle / kernel anchor (pid 0) */
    if (pid <= 0) {
        return -1;
    }
    pcb_process_abort(pid, 1);
    return 0;
}

/* ====================================================================== */
/* multi-user / multi-process extensions                                  */
/* ====================================================================== */

pcb_t *pcb_get_current(void)
{
    if (g_current_pid < 0) {
        return NULL;
    }
    return pcb_find_by_pid(g_current_pid);
}

pcb_t *pcb_get_by_pid(int32_t pid)
{
    return pcb_find_by_pid(pid);
}

int32_t pcb_alloc_pid(void)
{
    return (int32_t) g_next_pid++;
}

/* Create a fresh child PCB (used by fork).  Returns a writable pointer or
 * NULL if no slot / pid is available.  The child starts READY; the caller
 * fills in its address space, registers and kernel stack. */
pcb_t *pcb_create_child(int32_t parent_pid)
{
    int32_t slot = pcb_claim_slot();
    pcb_t *pcb;

    if (slot < 0) {
        return NULL;
    }
    pcb = &g_pcbs[slot];
    memset(pcb, 0, sizeof(*pcb));
    pcb->used = true;
    pcb->slot = (uint32_t) slot;
    pcb->pid = pcb_alloc_pid();
    pcb->state = PCB_STATE_READY;
    pcb->parent_pid = parent_pid;
    pcb->start_tick = timer_ticks();
    pcb->uid = 0;
    pcb->gid = 0;
    pcb->euid = 0;
    pcb->egid = 0;
    /* Inherit scheduling priority from parent; fresh time slice. */
    {
        pcb_t *par = pcb_find_by_pid(parent_pid);
        pcb->priority = (par != NULL) ? par->priority : SCHED_PRIORITY_DEFAULT;
        /* Children inherit the parent's process group and session, but are
         * never session leaders themselves. */
        if (par != NULL) {
            pcb->pgid = par->pgid;
            pcb->sid = par->sid;
            pcb_fd_copy(pcb, par);
        } else {
            pcb->pgid = pcb->pid;
            pcb->sid = pcb->pid;
            pcb_fd_init(pcb);
        }
    }
    pcb->session_leader = false;
    pcb->wait_target_pid = -1;
    pcb->time_slice = SCHED_TIMESLICE_TICKS;
    pcb->remaining_ticks = SCHED_TIMESLICE_TICKS;
    return pcb;
}

/* Switch the "currently running" PCB pointer (used while running a forked
 * child nested inside the parent's syscall). */
void pcb_set_current(int32_t pid)
{
    g_current_pid = pid;
}

void pcb_set_credentials(int32_t pid, uint32_t uid, uint32_t gid,
                         uint32_t euid, uint32_t egid)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb != NULL) {
        pcb->uid = uid;
        pcb->gid = gid;
        pcb->euid = euid;
        pcb->egid = egid;
    }
}

void pcb_set_state(int32_t pid, pcb_state_t state)
{
    pcb_t *pcb = pcb_find_by_pid(pid);

    if (pcb != NULL) {
        pcb->state = state;
    }
}

bool pcb_has_zombie_child(int32_t parent_pid)
{
    for (uint32_t i = 0; i < PCB_MAX; i++) {
        if (g_pcbs[i].used &&
            g_pcbs[i].state == PCB_STATE_ZOMBIE &&
            g_pcbs[i].parent_pid == parent_pid) {
            return true;
        }
    }
    return false;
}

int32_t pcb_wait_child(int32_t parent_pid, int32_t *exit_code)
{
    for (uint32_t i = 0; i < PCB_MAX; i++) {
        pcb_t *pcb = &g_pcbs[i];
        int32_t reaped_pid;

        if (!pcb->used || pcb->parent_pid != parent_pid) {
            continue;
        }
        if (pcb->state != PCB_STATE_ZOMBIE &&
            pcb->state != PCB_STATE_EXITED &&
            pcb->state != PCB_STATE_ABORTED) {
            continue;
        }
        if (exit_code != NULL) {
            *exit_code = pcb->exit_code;
        }
        reaped_pid = (int32_t) pcb->pid;
        /* Release resources and the slot so it can be reused. */
        if (pcb->addr_space.active) {
            vm_destroy_address_space(&pcb->addr_space);
        }
        if (pcb->kernel_stack != NULL) {
            kfree(pcb->kernel_stack);
            pcb->kernel_stack = NULL;
        }
        memset(pcb, 0, sizeof(*pcb));
        pool_free_slot(&g_pcb_pool, i);
        return reaped_pid;
    }
    return -1;
}

/* ====================================================================== */
/* Preemptive scheduling with priorities                                  */
/* ====================================================================== */

static bool g_preemptive_enabled = true;
static uint32_t g_rr_cursor;

void scheduler_set_preemptive(bool enabled)
{
    g_preemptive_enabled = enabled;
}

bool scheduler_is_preemptive(void)
{
    return g_preemptive_enabled;
}

void pcb_set_priority(int32_t pid, uint32_t priority)
{
    pcb_t *pcb = pcb_find_by_pid(pid);
    if (pcb != NULL) {
        if (priority > SCHED_PRIORITY_REALTIME) {
            priority = SCHED_PRIORITY_REALTIME;
        }
        pcb->priority = priority;
        pcb->remaining_ticks = pcb->time_slice;
    }
}

uint32_t pcb_get_priority(int32_t pid)
{
    pcb_t *pcb = pcb_find_by_pid(pid);
    return (pcb != NULL) ? pcb->priority : SCHED_PRIORITY_DEFAULT;
}

void pcb_reset_time_slice(pcb_t *pcb)
{
    if (pcb != NULL) {
        uint32_t mult = 1;
        if (pcb->priority >= SCHED_PRIORITY_REALTIME) mult = 4;
        else if (pcb->priority >= SCHED_PRIORITY_HIGH) mult = 2;
        pcb->time_slice = SCHED_TIMESLICE_TICKS * mult;
        pcb->remaining_ticks = pcb->time_slice;
    }
}

pcb_t *scheduler_pick_next(void)
{
    pcb_t *best = NULL;
    uint32_t best_priority = SCHED_PRIORITY_IDLE - 1u;
    uint32_t current_pid = (g_current_pid >= 0) ? (uint32_t) g_current_pid : 0xFFFFFFFFu;

    for (uint32_t i = 0; i < PCB_MAX; i++) {
        pcb_t *p = &g_pcbs[i];
        if (!p->used || p->state != PCB_STATE_READY) continue;
        if (p->pid == current_pid) continue;
        if (p->priority > best_priority) {
            best = p;
            best_priority = p->priority;
        } else if (p->priority == best_priority && best != NULL) {
            if (i > g_rr_cursor && best->slot <= g_rr_cursor) {
                best = p;
            }
        }
    }
    return best;
}

bool scheduler_tick(void)
{
    pcb_t *cur;

    if (!g_preemptive_enabled) {
        return false;
    }
    cur = pcb_get_current();
    if (cur == NULL) {
        return false;
    }
    cur->cpu_user_ticks++;

    if (cur->remaining_ticks > 0) {
        cur->remaining_ticks--;
    }
    if (cur->remaining_ticks == 0) {
        pcb_t *next = scheduler_pick_next();
        if (next != NULL) {
            g_rr_cursor = (cur->slot + 1) % PCB_MAX;
            pcb_reset_time_slice(cur);
            return true;
        }
        pcb_reset_time_slice(cur);
    }
    return false;
}

void sys_sched_yield(void)
{
    pcb_t *cur = pcb_get_current();
    if (cur == NULL) {
        return;
    }
    cur->remaining_ticks = 0;
    pcb_reset_time_slice(cur);
}

/* ====================================================================== */
/* waitpid specific-reap / process groups / sessions / FD table           */
/* ====================================================================== */

/* Reap a specific child pid of parent_pid.  Returns the reaped pid, or -1 if
 * the named child does not exist, is not a child, or is not yet reapable. */
int32_t pcb_reap_child(int32_t parent_pid, int32_t child_pid, int32_t *exit_code)
{
    for (uint32_t i = 0; i < PCB_MAX; i++) {
        pcb_t *pcb = &g_pcbs[i];
        int32_t reaped_pid;

        if (!pcb->used || pcb->parent_pid != parent_pid) {
            continue;
        }
        if ((int32_t) pcb->pid != child_pid) {
            continue;
        }
        if (pcb->state != PCB_STATE_ZOMBIE &&
            pcb->state != PCB_STATE_EXITED &&
            pcb->state != PCB_STATE_ABORTED) {
            return -1;
        }
        if (exit_code != NULL) {
            *exit_code = pcb->exit_code;
        }
        reaped_pid = (int32_t) pcb->pid;
        if (pcb->addr_space.active) {
            vm_destroy_address_space(&pcb->addr_space);
        }
        if (pcb->kernel_stack != NULL) {
            kfree(pcb->kernel_stack);
            pcb->kernel_stack = NULL;
        }
        memset(pcb, 0, sizeof(*pcb));
        pool_free_slot(&g_pcb_pool, i);
        return reaped_pid;
    }
    return -1;
}

int32_t pcb_setpgid(int32_t pid, int32_t pgid)
{
    pcb_t *target;
    pcb_t *group;

    if (pid <= 0) {
        return -1;
    }
    target = pcb_find_by_pid(pid);
    if (target == NULL) {
        return -1;
    }
    /* pgid == 0 means "use the target's own pid" (start a new group). */
    if (pgid == 0) {
        pgid = pid;
    }
    /* Joining a foreign group requires that group to already exist. */
    if (pgid != pid) {
        group = pcb_find_by_pid(pgid);
        if (group == NULL) {
            return -1;
        }
    }
    target->pgid = pgid;
    return 0;
}

int32_t pcb_getpgid(int32_t pid)
{
    pcb_t *target = pcb_find_by_pid(pid);

    return (target != NULL) ? target->pgid : -1;
}

int32_t pcb_setsid(void)
{
    pcb_t *cur = pcb_get_current();

    if (cur == NULL) {
        return -1;
    }
    /* A session leader cannot create a new session. */
    if (cur->session_leader) {
        return -1;
    }
    cur->sid = (int32_t) cur->pid;
    cur->pgid = (int32_t) cur->pid;
    cur->session_leader = true;
    return (int32_t) cur->sid;
}

void pcb_fd_init(pcb_t *proc)
{
    uint32_t i;

    if (proc == NULL) {
        return;
    }
    for (i = 0; i < PCB_FD_MAX; i++) {
        proc->fd_table[i] = -1;
    }
    for (i = 0; i < PCB_FD_CLOEXEC_WORDS; i++) {
        proc->fd_cloexec[i] = 0;
    }
    /* Reserve stdin/stdout/stderr as kernel handle indices 0/1/2. */
    proc->fd_table[0] = 0;
    proc->fd_table[1] = 1;
    proc->fd_table[2] = 2;
}

int32_t pcb_fd_alloc(pcb_t *proc)
{
    uint32_t i;

    if (proc == NULL) {
        return -1;
    }
    for (i = 3; i < PCB_FD_MAX; i++) {
        if (proc->fd_table[i] == -1) {
            proc->fd_table[i] = -2; /* temporarily occupied until fd_set */
            return (int32_t) i;
        }
    }
    return -1;
}

void pcb_fd_free(pcb_t *proc, int32_t fd)
{
    uint32_t byte_index;
    uint32_t bit_index;

    if (proc == NULL || fd < 0 || fd >= PCB_FD_MAX) {
        return;
    }
    proc->fd_table[fd] = -1;
    byte_index = (uint32_t) fd / 8U;
    bit_index = (uint32_t) fd % 8U;
    proc->fd_cloexec[byte_index] &= (uint8_t) ~(1U << bit_index);
}

int32_t pcb_fd_get_handle(pcb_t *proc, int32_t fd)
{
    if (proc == NULL || fd < 0 || fd >= PCB_FD_MAX) {
        return -1;
    }
    return proc->fd_table[fd];
}

void pcb_fd_set(pcb_t *proc, int32_t fd, int32_t handle)
{
    if (proc == NULL || fd < 0 || fd >= PCB_FD_MAX) {
        return;
    }
    proc->fd_table[fd] = handle;
}

void pcb_fd_copy(pcb_t *dst, const pcb_t *src)
{
    uint32_t i;

    if (dst == NULL || src == NULL) {
        if (dst != NULL) {
            pcb_fd_init(dst);
        }
        return;
    }
    for (i = 0; i < PCB_FD_MAX; i++) {
        dst->fd_table[i] = src->fd_table[i];
    }
    for (i = 0; i < PCB_FD_CLOEXEC_WORDS; i++) {
        dst->fd_cloexec[i] = src->fd_cloexec[i];
    }
}
