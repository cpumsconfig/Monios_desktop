#ifndef _PCB_H_
#define _PCB_H_

#include "stdbool.h"
#include "stdint.h"
#include "vm.h"

typedef enum {
    PCB_STATE_FREE = 0,
    PCB_STATE_READY,
    PCB_STATE_RUNNING,
    PCB_STATE_EXITED,
    PCB_STATE_ABORTED,
    PCB_STATE_STOPPED,
    PCB_STATE_ZOMBIE,
    PCB_STATE_WAITING
} pcb_state_t;

/*
 * Process Control Block.
 *
 * Extended for multi-user / multi-process isolation: per-process address
 * space (vm_address_space_t), uid/gid, saved register state for context
 * switching, and a zombie state for wait() exit-code collection.
 */
/* Scheduling priorities.  Higher numeric value = higher priority. */
#define SCHED_PRIORITY_IDLE     0
#define SCHED_PRIORITY_LOW      1
#define SCHED_PRIORITY_NORMAL   2
#define SCHED_PRIORITY_HIGH     3
#define SCHED_PRIORITY_REALTIME 4
#define SCHED_PRIORITY_DEFAULT  SCHED_PRIORITY_NORMAL
#define SCHED_TIMESLICE_TICKS   10U   /* ~10 ms at 1000 Hz tick */

/* Per-process file descriptor table capacity.  FDs 0/1/2 are reserved for
 * stdin/stdout/stderr.  An fd_table entry value of -1 means unused. */
#define PCB_FD_MAX              64
#define PCB_FD_CLOEXEC_WORDS    ((PCB_FD_MAX + 7U) / 8U)

typedef struct {
    bool used;
    uint32_t slot;
    uint32_t pid;
    char name[32];
    char path[128];
    pcb_state_t state;
    int32_t exit_code;
    uint64_t start_tick;
    uint64_t end_tick;
    uint32_t pending_signals;
    uint8_t fault_vector;
    uint64_t fault_error_code;
    /* --- task-manager accounting (Feature 7) --- */
    uint64_t cpu_user_ticks;    /* ticks spent in user mode */
    uint64_t cpu_kernel_ticks;  /* ticks spent in kernel mode */
    uint32_t mem_pages;          /* resident page-table pages */
    int32_t parent_pid;          /* parent process pid (-1 = kernel/root) */
    uint32_t thread_count;       /* threads owned by this process */
    /* --- preemptive scheduling --- */
    uint32_t priority;           /* scheduling priority (0..4) */
    uint32_t time_slice;         /* full time slice length in ticks */
    uint32_t remaining_ticks;    /* ticks remaining in current slice */
    /* --- multi-user / multi-process extensions --- */
    uint32_t uid;                /* real user ID */
    uint32_t gid;                /* real group ID */
    uint32_t euid;               /* effective user ID */
    uint32_t egid;               /* effective group ID */
    vm_address_space_t addr_space; /* per-process page tables + user region */
    /* Saved register state for context switch / fork return. */
    uint64_t reg_rax, reg_rbx, reg_rcx, reg_rdx;
    uint64_t reg_rsi, reg_rdi, reg_rbp, reg_rsp;
    uint64_t reg_r8, reg_r9, reg_r10, reg_r11;
    uint64_t reg_r12, reg_r13, reg_r14, reg_r15;
    uint64_t reg_rip;
    uint64_t reg_rflags;
    uint8_t *kernel_stack;       /* per-process kernel stack */
    uint64_t kernel_stack_size;
    /* --- process groups / sessions --- */
    int32_t pgid;                /* process group ID (defaults to own pid) */
    int32_t sid;                 /* session ID (defaults to own pid)        */
    bool session_leader;         /* true if this process leads its session  */
    /* --- waitpid blocking support --- */
    int32_t wait_target_pid;     /* WAITING: -1 any, >0 specific, 0 group, <-1 -group */
    /* --- per-process file descriptor table --- */
    int32_t fd_table[PCB_FD_MAX];      /* fd -> global handle (-1 = free)   */
    uint8_t fd_cloexec[PCB_FD_CLOEXEC_WORDS]; /* close-on-exec bitmask      */
} pcb_t;

/* Snapshot of a process suitable for userspace task managers. */
typedef struct {
    int32_t pid;
    int32_t parent_pid;
    char name[32];
    char state_name[12];
    pcb_state_t state;
    uint64_t cpu_user_ticks;
    uint64_t cpu_kernel_ticks;
    uint64_t cpu_total_ticks;
    uint64_t uptime_ticks;
    uint32_t mem_pages;
    uint32_t mem_kb;
    uint32_t thread_count;
} process_info_t;

/* Syscall 51 / 52 / 53 request descriptors (kernel-side handlers). */
typedef struct {
    uint32_t count;                 /* out: number of live processes */
    process_info_t entries[16];      /* out: snapshots */
} process_enum_result_t;

typedef struct {
    int32_t pid;                    /* in: pid to query */
    process_info_t info;            /* out */
} process_stats_request_t;

void pcb_init(void);
int32_t pcb_process_start(const char *path);
void pcb_process_exit(int32_t pid, int32_t exit_code);
void pcb_process_abort(int32_t pid, int32_t exit_code);
void pcb_process_fault(int32_t pid, uint8_t vector, uint64_t error_code, int32_t exit_code);
void pcb_process_stop(int32_t pid);
int32_t pcb_current_pid(void);
uint32_t pcb_count(void);
uint32_t pcb_capacity(void);
bool pcb_snapshot(uint32_t index, pcb_t *out);
uint32_t pcb_pending_signal_mask(int32_t pid);
bool pcb_signal_or(int32_t pid, uint32_t mask);
bool pcb_signal_set(int32_t pid, uint32_t mask);
const char *pcb_status(void);
const char *pcb_state_name(pcb_state_t state);

/* --- Feature 7: process management enhancement --- */
void pcb_set_parent(int32_t pid, int32_t parent_pid);
void pcb_account_cpu(int32_t pid, bool in_kernel, uint64_t ticks);
void pcb_set_mem_pages(int32_t pid, uint32_t pages);
void pcb_add_thread(int32_t pid, int32_t delta);
uint32_t process_enum(process_info_t *out, uint32_t max_entries);
int32_t process_terminate(int32_t pid);
bool process_stats(int32_t pid, process_info_t *out);
uint32_t process_enum_all(process_enum_result_t *result);

/* --- multi-user / multi-process extensions --- */
pcb_t *pcb_get_current(void);
pcb_t *pcb_get_by_pid(int32_t pid);
int32_t pcb_alloc_pid(void);
pcb_t *pcb_create_child(int32_t parent_pid);
void pcb_set_current(int32_t pid);
void pcb_set_credentials(int32_t pid, uint32_t uid, uint32_t gid,
                         uint32_t euid, uint32_t egid);
void pcb_set_state(int32_t pid, pcb_state_t state);
int32_t pcb_wait_child(int32_t parent_pid, int32_t *exit_code);
bool pcb_has_zombie_child(int32_t parent_pid);

/* Reap a specific child pid of parent_pid, freeing its address space, kernel
 * stack and slot.  Returns the reaped pid on success, -1 if no such reapable
 * child exists. */
int32_t pcb_reap_child(int32_t parent_pid, int32_t child_pid, int32_t *exit_code);

/* --- process groups / sessions --- */
int32_t pcb_setpgid(int32_t pid, int32_t pgid);
int32_t pcb_getpgid(int32_t pid);
int32_t pcb_setsid(void);

/* --- per-process file descriptor table --- */
void pcb_fd_init(pcb_t *proc);
int32_t pcb_fd_alloc(pcb_t *proc);
void pcb_fd_free(pcb_t *proc, int32_t fd);
int32_t pcb_fd_get_handle(pcb_t *proc, int32_t fd);
void pcb_fd_set(pcb_t *proc, int32_t fd, int32_t handle);
void pcb_fd_copy(pcb_t *dst, const pcb_t *src);

/* --- preemptive scheduling --- */
void pcb_set_priority(int32_t pid, uint32_t priority);
uint32_t pcb_get_priority(int32_t pid);
void pcb_reset_time_slice(pcb_t *pcb);
/* Called from the timer interrupt every tick.  Decrements the currently
 * running process's remaining time slice and returns true if a reschedule
 * is needed (slice expired and a higher-or-equal priority READY process
 * exists). */
bool scheduler_tick(void);
/* Pick the next READY process to run based on priority and round-robin
 * within the same priority level.  Returns NULL if no other READY process. */
pcb_t *scheduler_pick_next(void);
/* Voluntarily yield the CPU to another READY process. */
void sys_sched_yield(void);
/* Set whether preemption is enabled (default on after scheduler init). */
void scheduler_set_preemptive(bool enabled);
bool scheduler_is_preemptive(void);

#endif
