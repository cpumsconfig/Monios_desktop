#include "common.h"
#include "proc_mgmt.h"
#include "pcb.h"
#include "vm.h"
#include "exec.h"
#include "memory.h"
#include "mmu.h"
#include "string.h"

/*
 * Process management: fork / execv / waitpid / getpid / getppid / exit.
 *
 * Monios uses a cooperative model: a user process runs to completion (or
 * until it makes a blocking syscall).  fork() snapshots the parent's register
 * frame and address space into a child PCB marked READY; waitpid() then runs
 * that child to completion on its own address space and collects its exit code.
 *
 * The syscall register frame laid down by syscall_interrupt_handler is:
 *   [0..14]  saved GP registers (rax,rbx,rcx,rdx,rsi,rdi,rbp,r8..r15)
 *   [15]     user rip, [16] cs, [17] rflags, [18] user rsp, [19] ss
 */

void proc_mgmt_init(void)
{
    /* Nothing to initialise yet; PCB and VM are already up. */
}

int32_t sys_getpid(void)
{
    return pcb_current_pid();
}

int32_t sys_getppid(void)
{
    pcb_t *cur = pcb_get_current();
    return (cur != NULL) ? cur->parent_pid : -1;
}

void sys_exit(int32_t exit_code)
{
    pcb_t *cur = pcb_get_current();

    if (cur != NULL) {
        cur->exit_code = exit_code;
        cur->state = PCB_STATE_ZOMBIE;
        pcb_process_exit((int32_t) cur->pid, exit_code);
    }
    /* The syscall dispatcher will not return to user mode; exec layer
     * handles the actual context teardown. */
    exec_complete_from_syscall(exit_code);
}

int32_t sys_fork(void *frame_ptr)
{
    pcb_t *parent = pcb_get_current();
    pcb_t *child;
    uint64_t *f = (uint64_t *) frame_ptr;

    if (parent == NULL || frame_ptr == NULL) {
        return -1;
    }

    child = pcb_create_child((int32_t) parent->pid);
    if (child == NULL) {
        return -1;
    }

    strlcpy(child->name, parent->name, sizeof(child->name));
    strlcpy(child->path, parent->path, sizeof(child->path));

    /* Give the child its own address space and share the parent's image
     * via copy-on-write (COW).  This avoids duplicating the entire 4 MiB
     * user region plus all demand-paged pages at fork time; pages are only
     * copied when either process writes to them. */
    if (!vm_create_address_space(&child->addr_space)) {
        child->used = false;
        return -1;
    }
    if (!vm_cow_share(&parent->addr_space, &child->addr_space)) {
        /* Fall back to a full copy if COW setup fails (e.g. out of memory
         * for page-table pages). */
        vm_copy_user_region(&child->addr_space, &parent->addr_space);
    }

    /* Per-process kernel stack for nested syscalls. */
    child->kernel_stack = (uint8_t *) kmalloc(EXEC_KERNEL_STACK_SIZE);
    if (child->kernel_stack == NULL) {
        vm_destroy_address_space(&child->addr_space);
        child->used = false;
        return -1;
    }
    child->kernel_stack_size = EXEC_KERNEL_STACK_SIZE;

    /* Inherit credentials. */
    child->uid   = parent->uid;
    child->gid   = parent->gid;
    child->euid  = parent->euid;
    child->egid  = parent->egid;

    /* Snapshot the parent's user register state.  The child restarts right
     * after the fork syscall with RAX = 0 (the fork() child return value). */
    child->reg_rax = 0;
    child->reg_rbx = f[1];
    child->reg_rcx = f[2];
    child->reg_rdx = f[3];
    child->reg_rsi = f[4];
    child->reg_rdi = f[5];
    child->reg_rbp = f[6];
    child->reg_r8  = f[7];
    child->reg_r9  = f[8];
    child->reg_r10 = f[9];
    child->reg_r11 = f[10];
    child->reg_r12 = f[11];
    child->reg_r13 = f[12];
    child->reg_r14 = f[13];
    child->reg_r15 = f[14];
    child->reg_rip    = f[15];   /* user return address */
    child->reg_rflags = f[17];
    child->reg_rsp    = f[18];   /* user stack pointer  */

    child->state = PCB_STATE_READY;

    /* Parent returns the child pid. */
    return (int32_t) child->pid;
}

int32_t sys_execv(const char *path, char *argv[])
{
    /* execv replaces the current process.  In the current architecture
     * this is equivalent to calling exec_run() from kernel context.
     * For now, return -1; the shell uses its own exec path. */
    (void) path;
    (void) argv;
    return -1;
}

/* Decide whether a child PCB satisfies the waitpid() pid filter.
 *   pid >  0: wait only for that specific child
 *   pid == -1: wait for any child
 *   pid ==  0: wait for any child in our process group
 *   pid <  -1: wait for any child in process group (-pid)
 */
static bool waitpid_matches(const pcb_t *cur, const pcb_t *child, int32_t pid)
{
    if (pid > 0) {
        return (int32_t) child->pid == pid;
    }
    if (pid == -1) {
        return true;
    }
    if (pid == 0) {
        return child->pgid == cur->pgid;
    }
    /* pid < -1 */
    return child->pgid == -pid;
}

static bool waitpid_reapable(pcb_state_t state)
{
    return state == PCB_STATE_ZOMBIE ||
           state == PCB_STATE_EXITED ||
           state == PCB_STATE_ABORTED;
}

int32_t sys_waitpid(int32_t pid, int32_t *exit_code)
{
    pcb_t *cur = pcb_get_current();
    pcb_t *child = NULL;
    uint32_t i;
    int32_t code;
    int32_t child_pid;
    bool saw_live_match = false;

    if (cur == NULL) {
        return -1;
    }

    /* First pass: reap an already-exited child that matches the filter. */
    for (i = 0; i < pcb_capacity(); i++) {
        pcb_t t;

        if (!pcb_snapshot(i, &t) || !t.used ||
            t.parent_pid != (int32_t) cur->pid) {
            continue;
        }
        if (!waitpid_matches(cur, &t, pid)) {
            continue;
        }
        if (waitpid_reapable(t.state)) {
            return pcb_reap_child((int32_t) cur->pid, (int32_t) t.pid, exit_code);
        }
        saw_live_match = true;
    }

    /* Second pass: no zombie match.  Run a matching READY child inline to
     * completion on its own address space (cooperative execution model). */
    for (i = 0; i < pcb_capacity(); i++) {
        pcb_t t;

        if (!pcb_snapshot(i, &t) || !t.used ||
            t.parent_pid != (int32_t) cur->pid) {
            continue;
        }
        if (!waitpid_matches(cur, &t, pid)) {
            continue;
        }
        if (t.state == PCB_STATE_READY) {
            child = pcb_get_by_pid(t.pid);
            break;
        }
    }

    if (child != NULL) {
        child->state = PCB_STATE_RUNNING;
        code = exec_run_child(child);
        child->exit_code = code;
        child->state = PCB_STATE_ZOMBIE;
        child_pid = (int32_t) child->pid;
        /* Reap the specific child: frees its address space, kernel stack and
         * slot, and reports its exit code. */
        pcb_reap_child((int32_t) cur->pid, child_pid, exit_code);
        return child_pid;
    }

    /* A matching child exists but is still running elsewhere: block the
     * caller.  The child's exit path (pcb_finish_process) will wake this
     * parent back to READY. */
    if (saw_live_match) {
        cur->state = PCB_STATE_WAITING;
        cur->wait_target_pid = pid;
    }
    return -1;
}
