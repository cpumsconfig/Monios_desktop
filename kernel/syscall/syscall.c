#include "common.h"
#include "console.h"
#include "exec.h"
#include "file.h"
#include "futex.h"
#include "graphics.h"
#include "interrupt.h"
#include "ipc.h"
#include "keyboard.h"
#include "kernel.h"
#include "mmu.h"
#include "mouse.h"
#include "net.h"
#include "path.h"
#include "shell.h"
#include "signal.h"
#include "socket.h"
#include "syscall.h"
#include "system_status.h"
#include "task.h"
#include "audio.h"
#include "smp.h"
#include "terminal.h"
#include "cpu.h"

typedef struct {
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} syscall_frame_t;

extern void syscall_interrupt_handler(void);
extern void syscall_entry(void);

/* SYSCALL 支持 */
uint64_t g_syscall_kernel_stack = 0;
uint64_t g_syscall_user_rsp = 0;
static bool g_syscall_enabled = false;

static const char *syscall_resolve_path(const char *path, char resolved[PATH_MAX_LEN])
{
    if (path == NULL) {
        return NULL;
    }
    if (!exec_resolve_path(path, resolved, PATH_MAX_LEN)) {
        return NULL;
    }
    return resolved;
}

static int32_t syscall_handle_write(uint64_t handle, const char *buffer, uint32_t size)
{
    if ((handle != EXEC_HANDLE_STDOUT && handle != EXEC_HANDLE_STDERR) || buffer == NULL) {
        return -1;
    }

    if (shell_output_capture_active()) {
        shell_output_capture_write(buffer, size);
        return (int32_t) size;
    }

    for (uint32_t i = 0; i < size; i++) {
        console_write_char(buffer[i]);
    }
    return (int32_t) size;
}

static int32_t syscall_handle_read(uint64_t handle, char *buffer, uint32_t size)
{
    uint32_t read_count = 0;

    if (handle != EXEC_HANDLE_STDIN || buffer == NULL) {
        return -1;
    }

    while (read_count < size) {
        char ch;
        if (!keyboard_read_char(&ch)) {
            break;
        }
        buffer[read_count++] = ch;
    }

    return (int32_t) read_count;
}

extern bool kernel_shutdown_requested(void);
extern bool kernel_reboot_requested(void);

static int32_t syscall_system_status(system_status_t *status, uint32_t size)
{
    const net_info_t *net;
    const char *audio_track;
    const terminal_info_t *term;
    const smp_info_t *smp;

    if (status == NULL || size < sizeof(system_status_t)) {
        return -1;
    }

    memset(status, 0, sizeof(*status));
    status->task_count = task_count();
    status->scheduler_stopping = task_scheduler_stopping();
    status->shutdown_requested = kernel_shutdown_requested();
    status->reboot_requested = kernel_reboot_requested();

    net = net_info();
    status->net_present = net->present;
    status->net_connected = net->connected;
    status->net_tx_packets = net->tx_packets;
    status->net_rx_packets = net->rx_packets;
    status->net_ping_requests = net->ping_requests;
    status->net_ping_replies = net->ping_replies;
    status->net_dhcp_configured = net->dhcp_configured;
    strcpy(status->net_driver, net->driver);
    strcpy(status->net_mac, net->mac_text);
    strcpy(status->net_ip, net->ip_text);
    strcpy(status->net_gateway, net->gateway_text);
    strcpy(status->net_dns, net->dns_text);
    strcpy(status->net_status, net_status());
    strcpy(status->net_last_target, net->last_target);

    status->audio_playing = audio_is_playing();
    status->audio_paused = audio_is_paused();
    status->audio_present = audio_primary_device()->present;
    status->audio_volume = audio_volume();
    switch (audio_primary_device()->kind) {
    case AUDIO_DEVICE_AC97:
        strcpy(status->audio_driver, "onboard-ac97");
        break;
    case AUDIO_DEVICE_HDA:
        strcpy(status->audio_driver, "hda");
        break;
    case AUDIO_DEVICE_SB16:
        strcpy(status->audio_driver, "sb16");
        break;
    case AUDIO_DEVICE_ES1371:
        strcpy(status->audio_driver, "onboard-es1371");
        break;
    default:
        strcpy(status->audio_driver, "none");
        break;
    }
    audio_track = audio_current_track();
    if (audio_track != NULL) {
        strcpy(status->audio_track, audio_track);
    }

    status->gpu_submits = graphics_gpu_submit_count();
    status->gpu_presents = graphics_gpu_present_count();
    status->gpu_pending = graphics_gpu_pending_count();
    status->wm_windows = graphics_window_count();
    status->wm_focused = graphics_focused_window_index();
    term = terminal_info();
    status->terminal_active = term->active;
    status->terminal_focused = term->focused;
    status->terminal_lines = term->lines_written;
    smp = smp_info();
    status->smp_supported = smp->supported;
    status->smp_bootstrap_only = smp->bootstrap_only;
    status->smp_logical_processors = smp->logical_processors;
    status->smp_online_processors = smp->online_processors;
    return (int32_t) sizeof(*status);
}

void syscall_init(void)
{
    idt_set_handler(0x80, (uint64_t) syscall_interrupt_handler, 0xEE);
    log_write("syscall: int 0x80 ready");

    /* 检测并启用 AMD64 SYSCALL 快速系统调用 */
    const cpu_info_t *cpu = cpu_current_info();
    if (cpu->has_syscall && cpu->has_long_mode) {
        uint64_t star;
        uint64_t efer;

        /* 设置 STAR MSR：
         * 位 32-47: 内核代码段选择子
         * 位 48-63: 用户代码段选择子（SYSRET 时使用）
         */
        star = ((uint64_t) 0x08 << 32) | ((uint64_t) (0x18 | 3) << 48);
        cpu_write_msr(IA32_STAR_MSR, star);

        /* 设置 LSTAR MSR：SYSCALL 入口地址 */
        cpu_write_msr(IA32_LSTAR_MSR, (uint64_t) syscall_entry);

        /* 设置 SFMASK MSR：清除 RFLAGS 中的某些位
         * 清除 IF（中断标志），这样进入内核时自动关中断
         */
        cpu_write_msr(IA32_SFMASK_MSR, 0x200);  /* 清除 IF */

        /* 启用 EFER 中的 SCE 位 */
        efer = cpu_read_msr(IA32_EFER_MSR);
        efer |= EFER_SCE;
        cpu_write_msr(IA32_EFER_MSR, efer);

        /* 设置内核栈指针（暂时用当前栈） */
        asm volatile ("mov %%rsp, %0" : "=r" (g_syscall_kernel_stack));

        g_syscall_enabled = true;
        log_write("syscall: AMD64 SYSCALL/SYSRET enabled");
    }
}

uint64_t syscall_interrupt_dispatch(void *frame_ptr)
{
    syscall_frame_t *frame = (syscall_frame_t *) frame_ptr;
    char resolved[PATH_MAX_LEN];

    switch (frame->rax) {
    case SYS_GET_TICKS:
        return timer_ticks();
    case SYS_LOG_STRING:
        if ((const char *) frame->rbx != NULL) {
            log_write((const char *) frame->rbx);
        }
        return 0;
    case SYS_FILE_ROOT_COUNT:
        return file_root_entry_count();
    case SYS_FILE_READ: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path == NULL ? (uint64_t) -1 : (uint64_t) file_read(path, (void *) frame->rcx, (uint32_t) frame->rdx);
    }
    case SYS_ENTER_GRAPHICS_MODE:
        /* Request graphics mode from kernel — may be deferred until MMU active. */
        kernel_request_graphics_mode();
        return 0;
    case SYS_FILE_WRITE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path == NULL ? (uint64_t) -1 : (uint64_t) file_write(path, (const void *) frame->rcx, (uint32_t) frame->rdx);
    }
    case SYS_FILE_DELETE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_delete(path) ? 0 : (uint64_t) -1;
    }
    case SYS_FILE_MKDIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_mkdir(path) ? 0 : (uint64_t) -1;
    }
    case SYS_FILE_RMDIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_rmdir(path) ? 0 : (uint64_t) -1;
    }
    case SYS_FILE_LIST_DIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_list_dir(path, (char *) frame->rcx, (uint32_t) frame->rdx) ? 0 : (uint64_t) -1;
    }
    case SYS_HANDLE_WRITE:
        return (uint64_t) syscall_handle_write(frame->rbx, (const char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_HANDLE_READ:
        return (uint64_t) syscall_handle_read(frame->rbx, (char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_GET_CWD: {
        const char *cwd = exec_current_cwd();
        uint32_t size = (uint32_t) strlen(cwd);
        if ((char *) frame->rbx == NULL || frame->rcx <= size) {
            return (uint64_t) -1;
        }
        memcpy((void *) frame->rbx, cwd, size + 1);
        return size;
    }
    case SYS_FILE_EXISTS: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_exists(path) ? 1 : 0;
    }
    case SYS_FILE_SIZE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path == NULL ? (uint64_t) -1 : (uint64_t) file_size(path);
    }
    case SYS_MOUSE_GET_STATE: {
        mouse_snapshot_t snapshot;
        if ((void *) frame->rbx == NULL || frame->rcx < sizeof(snapshot)) {
            return (uint64_t) -1;
        }
        mouse_get_snapshot(&snapshot);
        memcpy((void *) frame->rbx, &snapshot, sizeof(snapshot));
        return sizeof(snapshot);
    }
    case SYS_FILE_IS_DIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && file_is_dir(path) ? 1 : 0;
    }
    case SYS_EXIT_PROCESS:
        exec_complete_from_syscall((int32_t) frame->rbx);
        return 0;
    case SYS_SYSTEM_STATUS:
        return (uint64_t) syscall_system_status((system_status_t *) frame->rbx, (uint32_t) frame->rcx);
    case SYS_GRAPHICS_FILL_RECT: {
        uint16_t x = (uint16_t) ((frame->rbx >> 48) & 0xFFFF);
        uint16_t y = (uint16_t) ((frame->rbx >> 32) & 0xFFFF);
        uint16_t width = (uint16_t) ((frame->rbx >> 16) & 0xFFFF);
        uint16_t height = (uint16_t) (frame->rbx & 0xFFFF);

        return graphics_user_fill_rect(x, y, width, height, (uint32_t) frame->rcx) ? 0 : (uint64_t) -1;
    }
    case SYS_GRAPHICS_PRESENT:
        graphics_user_present();
        return 0;
    case SYS_AUDIO_PLAY_FILE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && audio_play_file(path) ? 0 : (uint64_t) -1;
    }
    case SYS_OPEN_CUBE3D_WINDOW:
        graphics_open_cube3d_window();
        return 0;
    case SYS_SOCKET_CALL:
        if (frame->rbx == SOCKET_CALL_UDP_OPEN) {
            socket_open_request_t *request = (socket_open_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->handle = socket_udp_open(request->local_port);
            return request->handle >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_CLOSE) {
            return socket_close((int32_t) frame->rcx) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_SENDTO) {
            socket_sendto_request_t *request = (socket_sendto_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            return (uint64_t) socket_sendto_ipv4(request->handle,
                                                request->dst_host,
                                                request->dst_port,
                                                request->payload,
                                                request->payload_len);
        }
        if (frame->rbx == SOCKET_CALL_RECVFROM) {
            socket_recvfrom_request_t *request = (socket_recvfrom_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            return (uint64_t) socket_recvfrom_ipv4(request->handle,
                                                  request->src_ip,
                                                  &request->src_port,
                                                  request->buffer,
                                                  request->buffer_size);
        }
        return (uint64_t) -1;
    case SYS_FUTEX_CALL:
        if (frame->rbx == FUTEX_CALL_WAIT) {
            futex_wait_request_t *request = (futex_wait_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->result = futex_wait(request->address, request->expected, request->timeout_ticks);
            return request->result >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == FUTEX_CALL_WAKE) {
            futex_wake_request_t *request = (futex_wake_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->result = futex_wake(request->address, request->count);
            return 0;
        }
        if (frame->rbx == FUTEX_CALL_COUNT) {
            futex_wake_request_t *request = (futex_wake_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->result = futex_waiter_count(request->address);
            return 0;
        }
        return (uint64_t) -1;
    case SYS_IPC_CALL:
        if (frame->rbx == IPC_CALL_CREATE) {
            ipc_create_request_t *request = (ipc_create_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->port_id = ipc_port_create(request->name);
            return request->port_id >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == IPC_CALL_SEND) {
            ipc_send_request_t *request = (ipc_send_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            return ipc_send_text(request->port_id, request->text) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == IPC_CALL_RECV) {
            ipc_recv_request_t *request = (ipc_recv_request_t *) frame->rcx;

            if (request == NULL || frame->rdx < sizeof(*request)) {
                return (uint64_t) -1;
            }
            request->result = ipc_recv_text(request->port_id, request->buffer, request->buffer_size);
            return request->result >= 0 ? 0 : (uint64_t) -1;
        }
        return (uint64_t) -1;
    case SYS_SIGNAL_CALL: {
        signal_request_t *request = (signal_request_t *) frame->rcx;

        if (request == NULL || frame->rdx < sizeof(*request)) {
            return (uint64_t) -1;
        }
        if (frame->rbx == SIGNAL_CALL_SEND) {
            return signal_send(request->pid, (uint8_t) request->signo) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SIGNAL_CALL_PENDING) {
            request->mask = signal_pending(request->pid);
            return 0;
        }
        if (frame->rbx == SIGNAL_CALL_TAKE) {
            request->mask = signal_take_pending(request->pid);
            return 0;
        }
        if (frame->rbx == SIGNAL_CALL_CLEAR) {
            signal_clear(request->pid);
            return 0;
        }
        return (uint64_t) -1;
    }
    case SYS_REQUEST_R0: {
        const exec_launch_info_t *info = exec_current_launch_info();

        if (info == NULL) {
            return (uint64_t) -1;
        }
        if ((info->image_flags & EXEC_IMAGE_FLAG_DRIVER) == 0 ||
            (info->image_flags & EXEC_IMAGE_FLAG_SIGNED) == 0) {
            return (uint64_t) -1;
        }
        if (info->privilege_level == EXEC_PRIV_R0) {
            return 0;
        }
        if (info->privilege_level > EXEC_PRIV_R2) {
            return (uint64_t) -1;
        }
        /* R0 is reserved for trusted driver installation and does not prompt the user. */
        ((exec_launch_info_t *) info)->privilege_level = EXEC_PRIV_R0;
        ((exec_launch_info_t *) info)->image_flags |= EXEC_IMAGE_FLAG_NEEDS_R0;
        return 0;
    }
    case SYS_REQUEST_R2: {
        const exec_launch_info_t *info = exec_current_launch_info();
        const char *reason = (const char *) frame->rbx;

        if (info == NULL) {
            return (uint64_t) -1;
        }
        if (info->privilege_level <= EXEC_PRIV_R2) {
            return 0;
        }
        if (!graphics_request_uac_elevation(info->program_path, reason, EXEC_PRIV_R2)) {
            return (uint64_t) -1;
        }
        ((exec_launch_info_t *) info)->privilege_level = EXEC_PRIV_R2;
        ((exec_launch_info_t *) info)->image_flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
        return 0;
    }
    default:
        return (uint64_t) -1;
    }
}
