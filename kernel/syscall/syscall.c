#include "common.h"
#include "app_memory.h"
#include "console.h"
#include "exec.h"
#include "file.h"
#include "futex.h"
#include "graphics.h"
#include "interrupt.h"
#include "installer.h"
#include "ipc.h"
#include "keyboard.h"
#include "kernel.h"
#include "mmu.h"
#include "mouse.h"
#include "net.h"
#include "path.h"
#include "pcb.h"
#include "registry.h"
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
    char user_path[PATH_MAX_LEN];

    if (path == NULL) {
        return NULL;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(user_path, sizeof(user_path), path)) {
            return NULL;
        }
        path = user_path;
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
    if (exec_active() && !app_memory_user_range(buffer, size)) {
        exec_abort_from_exception(13, 0);
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
    if (exec_active() && !app_memory_user_range(buffer, size)) {
        exec_abort_from_exception(13, 0);
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

static void syscall_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    strlcpy(dst, src != NULL ? src : "", dst_size);
}

static int32_t syscall_system_status(system_status_t *status, uint32_t size)
{
    system_status_t snapshot;
    const net_info_t *net;
    const char *audio_track;
    const terminal_info_t *term;
    const smp_info_t *smp;

    if (status == NULL || size < sizeof(system_status_t)) {
        return -1;
    }
    if (exec_active() && !app_memory_user_range(status, sizeof(*status))) {
        exec_abort_from_exception(13, 0);
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.task_count = task_count();
    snapshot.process_count = pcb_count();
    snapshot.current_pid = pcb_current_pid();
    snapshot.scheduler_stopping = task_scheduler_stopping();
    snapshot.shutdown_requested = kernel_shutdown_requested();
    snapshot.reboot_requested = kernel_reboot_requested();

    net = net_info();
    snapshot.net_present = net->present;
    snapshot.net_connected = net->connected;
    snapshot.net_tx_packets = net->tx_packets;
    snapshot.net_rx_packets = net->rx_packets;
    snapshot.net_ping_requests = net->ping_requests;
    snapshot.net_ping_replies = net->ping_replies;
    snapshot.net_dhcp_configured = net->dhcp_configured;
    syscall_copy_text(snapshot.net_driver, sizeof(snapshot.net_driver), net->driver);
    syscall_copy_text(snapshot.net_mac, sizeof(snapshot.net_mac), net->mac_text);
    syscall_copy_text(snapshot.net_ip, sizeof(snapshot.net_ip), net->ip_text);
    syscall_copy_text(snapshot.net_gateway, sizeof(snapshot.net_gateway), net->gateway_text);
    syscall_copy_text(snapshot.net_dns, sizeof(snapshot.net_dns), net->dns_text);
    syscall_copy_text(snapshot.net_status, sizeof(snapshot.net_status), net_status());
    syscall_copy_text(snapshot.net_last_target, sizeof(snapshot.net_last_target), net->last_target);

    snapshot.audio_playing = audio_is_playing();
    snapshot.audio_paused = audio_is_paused();
    snapshot.audio_present = audio_primary_device()->present;
    snapshot.audio_volume = audio_volume();
    switch (audio_primary_device()->kind) {
    case AUDIO_DEVICE_AC97:
        syscall_copy_text(snapshot.audio_driver, sizeof(snapshot.audio_driver), "onboard-ac97");
        break;
    case AUDIO_DEVICE_HDA:
        syscall_copy_text(snapshot.audio_driver, sizeof(snapshot.audio_driver), "hda");
        break;
    case AUDIO_DEVICE_SB16:
        syscall_copy_text(snapshot.audio_driver, sizeof(snapshot.audio_driver), "sb16");
        break;
    case AUDIO_DEVICE_ES1371:
        syscall_copy_text(snapshot.audio_driver, sizeof(snapshot.audio_driver), "onboard-es1371");
        break;
    default:
        syscall_copy_text(snapshot.audio_driver, sizeof(snapshot.audio_driver), "none");
        break;
    }
    audio_track = audio_current_track();
    if (audio_track != NULL) {
        syscall_copy_text(snapshot.audio_track, sizeof(snapshot.audio_track), audio_track);
    }

    snapshot.gpu_submits = graphics_gpu_submit_count();
    snapshot.gpu_presents = graphics_gpu_present_count();
    snapshot.gpu_pending = graphics_gpu_pending_count();
    snapshot.wm_windows = graphics_window_count();
    snapshot.wm_focused = graphics_focused_window_index();
    term = terminal_info();
    snapshot.terminal_active = term->active;
    snapshot.terminal_focused = term->focused;
    snapshot.terminal_lines = term->lines_written;
    smp = smp_info();
    snapshot.smp_supported = smp->supported;
    snapshot.smp_bootstrap_only = smp->bootstrap_only;
    snapshot.smp_logical_processors = smp->logical_processors;
    snapshot.smp_online_processors = smp->online_processors;

    if (exec_active()) {
        if (!app_memory_copy_to_user(status, &snapshot, sizeof(snapshot))) {
            exec_abort_from_exception(13, 0);
            return -1;
        }
    } else {
        *status = snapshot;
    }
    return (int32_t) sizeof(*status);
}

static uint64_t syscall_registry_get(const char *key_ptr, char *value_ptr, uint32_t value_size)
{
    char key[REGISTRY_KEY_MAX];
    char value[REGISTRY_VALUE_MAX];

    if (key_ptr == NULL || value_ptr == NULL || value_size == 0) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(key, sizeof(key), key_ptr) ||
            !app_memory_user_range(value_ptr, value_size)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        if (strlen(key_ptr) >= sizeof(key)) {
            return (uint64_t) -1;
        }
        strcpy(key, key_ptr);
    }
    if (!registry_get_copy(key, value, sizeof(value)) || strlen(value) + 1 > value_size) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_to_user(value_ptr, value, (uint32_t) strlen(value) + 1)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        strcpy(value_ptr, value);
    }
    return 0;
}

static uint64_t syscall_registry_set(const char *key_ptr, const char *value_ptr)
{
    const exec_launch_info_t *info = exec_current_launch_info();
    char key[REGISTRY_KEY_MAX];
    char value[REGISTRY_VALUE_MAX];

    if (key_ptr == NULL || value_ptr == NULL) {
        return (uint64_t) -1;
    }
    if (exec_active() && (info == NULL || info->privilege_level > EXEC_PRIV_R2)) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(key, sizeof(key), key_ptr) ||
            !app_memory_copy_string_from_user(value, sizeof(value), value_ptr)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        if (strlen(key_ptr) >= sizeof(key) || strlen(value_ptr) >= sizeof(value)) {
            return (uint64_t) -1;
        }
        strcpy(key, key_ptr);
        strcpy(value, value_ptr);
    }
    return registry_set(key, value) ? 0 : (uint64_t) -1;
}

static uint64_t syscall_default_app_get(const char *extension_ptr, char *app_path_ptr, uint32_t app_path_size)
{
    char extension[32];
    char app_path[PATH_MAX_LEN];

    if (extension_ptr == NULL || app_path_ptr == NULL || app_path_size == 0) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(extension, sizeof(extension), extension_ptr) ||
            !app_memory_user_range(app_path_ptr, app_path_size)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        if (strlen(extension_ptr) >= sizeof(extension)) {
            return (uint64_t) -1;
        }
        strcpy(extension, extension_ptr);
    }
    if (!registry_default_app_for_extension(extension, app_path, sizeof(app_path)) ||
        strlen(app_path) + 1 > app_path_size) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_to_user(app_path_ptr, app_path, (uint32_t) strlen(app_path) + 1)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        strcpy(app_path_ptr, app_path);
    }
    return 0;
}

static uint64_t syscall_default_app_set(const char *extension_ptr, const char *app_path_ptr)
{
    const exec_launch_info_t *info = exec_current_launch_info();
    char extension[32];
    char app_path[PATH_MAX_LEN];

    if (extension_ptr == NULL || app_path_ptr == NULL) {
        return (uint64_t) -1;
    }
    if (exec_active() && (info == NULL || info->privilege_level > EXEC_PRIV_R2)) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(extension, sizeof(extension), extension_ptr) ||
            !app_memory_copy_string_from_user(app_path, sizeof(app_path), app_path_ptr)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        if (strlen(extension_ptr) >= sizeof(extension) || strlen(app_path_ptr) >= sizeof(app_path)) {
            return (uint64_t) -1;
        }
        strcpy(extension, extension_ptr);
        strcpy(app_path, app_path_ptr);
    }
    return registry_set_default_app(extension, app_path) ? 0 : (uint64_t) -1;
}

static uint64_t syscall_exec_defer(const char *path_ptr)
{
    const exec_launch_info_t *info = exec_current_launch_info();
    char path[PATH_MAX_LEN];

    if (path_ptr == NULL || info == NULL || info->privilege_level > EXEC_PRIV_R2) {
        return (uint64_t) -1;
    }
    if (exec_active()) {
        if (!app_memory_copy_string_from_user(path, sizeof(path), path_ptr)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
    } else {
        if (strlen(path_ptr) >= sizeof(path)) {
            return (uint64_t) -1;
        }
        strcpy(path, path_ptr);
    }
    return shell_defer_exec_path(path) ? 0 : (uint64_t) -1;
}

static uint64_t syscall_installer_call(uint64_t call, void *request_ptr, uint32_t request_size)
{
    if (call == INSTALLER_CALL_BOOT_MEDIA) {
        return installer_boot_media_present() ? 1 : 0;
    }
    if (call == INSTALLER_CALL_REBOOT) {
        installer_request_reboot();
        return 0;
    }
    if (call == INSTALLER_CALL_LIST_TARGETS) {
        installer_target_list_t *list_ptr = (installer_target_list_t *) request_ptr;
        installer_target_list_t list;
        int32_t result;

        if (list_ptr == NULL || request_size < sizeof(list)) {
            return (uint64_t) -1;
        }
        result = installer_list_targets(&list, sizeof(list));
        if (result < 0) {
            return (uint64_t) result;
        }
        if (exec_active()) {
            if (!app_memory_copy_to_user(list_ptr, &list, sizeof(list))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            *list_ptr = list;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_WRITE_DISK) {
        installer_write_request_t request;
        char source_path[PATH_MAX_LEN];
        int32_t result;

        if (request_ptr == NULL || request_size < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, request_ptr, sizeof(request)) ||
                !app_memory_copy_string_from_user(source_path, sizeof(source_path), request.source_path)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            request = *((installer_write_request_t *) request_ptr);
            if (request.source_path == NULL || strlen(request.source_path) >= sizeof(source_path)) {
                return (uint64_t) -1;
            }
            strcpy(source_path, request.source_path);
        }
        result = installer_write_file_to_disk(source_path,
                                              request.source_offset,
                                              request.disk_lba,
                                              request.byte_count);
        request.bytes_written = result > 0 ? (uint32_t) result : 0;
        if (exec_active()) {
            (void) app_memory_copy_to_user(request_ptr, &request, sizeof(request));
        } else {
            *((installer_write_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_WRITE_BUFFER) {
        installer_buffer_write_request_t request;
        uint8_t data[INSTALLER_WRITE_BUFFER_MAX];
        int32_t result;

        if (request_ptr == NULL || request_size < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, request_ptr, sizeof(request)) ||
                request.byte_count > sizeof(data) ||
                !app_memory_copy_from_user(data, request.data, request.byte_count)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            request = *((installer_buffer_write_request_t *) request_ptr);
            if (request.data == NULL || request.byte_count > sizeof(data)) {
                return (uint64_t) -1;
            }
            memcpy(data, request.data, request.byte_count);
        }
        result = installer_write_buffer_to_disk(data, request.disk_lba, request.byte_count);
        request.bytes_written = result > 0 ? (uint32_t) result : 0;
        if (exec_active()) {
            (void) app_memory_copy_to_user(request_ptr, &request, sizeof(request));
        } else {
            *((installer_buffer_write_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_WRITE_TARGET) {
        installer_target_write_request_t request;
        char target_path[PATH_MAX_LEN];
        uint8_t data[INSTALLER_TARGET_WRITE_MAX];
        int32_t result;

        if (request_ptr == NULL || request_size < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, request_ptr, sizeof(request)) ||
                !app_memory_copy_string_from_user(target_path, sizeof(target_path), request.target_path) ||
                request.byte_count > sizeof(data) ||
                !app_memory_copy_from_user(data, request.data, request.byte_count)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            request = *((installer_target_write_request_t *) request_ptr);
            if (request.target_path == NULL || request.data == NULL ||
                strlen(request.target_path) >= sizeof(target_path) ||
                request.byte_count > sizeof(data)) {
                return (uint64_t) -1;
            }
            strcpy(target_path, request.target_path);
            memcpy(data, request.data, request.byte_count);
        }
        result = installer_write_target_file(target_path, data, request.target_lba, request.byte_count);
        request.bytes_written = result > 0 ? (uint32_t) result : 0;
        if (exec_active()) {
            (void) app_memory_copy_to_user(request_ptr, &request, sizeof(request));
        } else {
            *((installer_target_write_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    return (uint64_t) -1;
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
            char text[128];

            if (!exec_active()) {
                log_write((const char *) frame->rbx);
            } else if (app_memory_copy_string_from_user(text, sizeof(text), (const char *) frame->rbx)) {
                log_write(text);
            } else {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        }
        return 0;
    case SYS_FILE_ROOT_COUNT:
        return file_root_entry_count();
    case SYS_FILE_READ: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        if (path == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active() && !app_memory_user_range((void *) frame->rcx, frame->rdx)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
        return (uint64_t) file_read(path, (void *) frame->rcx, (uint32_t) frame->rdx);
    }
    case SYS_ENTER_GRAPHICS_MODE:
        /* Request graphics mode from kernel — may be deferred until MMU active. */
        kernel_request_graphics_mode();
        return 0;
    case SYS_FILE_WRITE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        if (path == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active() && !app_memory_user_range((const void *) frame->rcx, frame->rdx)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
        return (uint64_t) file_write(path, (const void *) frame->rcx, (uint32_t) frame->rdx);
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
        if (path == NULL || (char *) frame->rcx == NULL || frame->rdx == 0) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            char list_buffer[2048];
            uint32_t copy_size = (uint32_t) frame->rdx;

            if (!app_memory_user_range((char *) frame->rcx, frame->rdx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            if (!file_list_dir(path, list_buffer, sizeof(list_buffer))) {
                return (uint64_t) -1;
            }
            if (copy_size > sizeof(list_buffer)) {
                copy_size = sizeof(list_buffer);
            }
            return app_memory_copy_to_user((char *) frame->rcx, list_buffer, copy_size) ? 0 : (uint64_t) -1;
        }
        return file_list_dir(path, (char *) frame->rcx, (uint32_t) frame->rdx) ? 0 : (uint64_t) -1;
    }
    case SYS_HANDLE_WRITE:
        return (uint64_t) syscall_handle_write(frame->rbx, (const char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_HANDLE_READ:
        return (uint64_t) syscall_handle_read(frame->rbx, (char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_GET_CWD: {
        const char *cwd = exec_current_cwd();
        uint32_t size = (uint32_t) strlen(cwd);
        if ((char *) frame->rbx == NULL || frame->rcx <= size ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, frame->rcx))) {
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
        if ((void *) frame->rbx == NULL || frame->rcx < sizeof(snapshot) ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(snapshot)))) {
            return (uint64_t) -1;
        }
        mouse_get_snapshot(&snapshot);
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, &snapshot, sizeof(snapshot))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            memcpy((void *) frame->rbx, &snapshot, sizeof(snapshot));
        }
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
    case SYS_GRAPHICS_DRAW_TEXT: {
        uint16_t x = (uint16_t) ((frame->rbx >> 48) & 0xFFFF);
        uint16_t y = (uint16_t) ((frame->rbx >> 32) & 0xFFFF);
        char text[160];

        if ((const char *) frame->rcx == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(text, sizeof(text), (const char *) frame->rcx)) {
                return (uint64_t) -1;
            }
        } else {
            strcpy(text, (const char *) frame->rcx);
        }
        return graphics_user_draw_text(x, y, text, (uint32_t) frame->rdx) ? 0 : (uint64_t) -1;
    }
    case SYS_GRAPHICS_PRESENT:
        graphics_user_present();
        return 0;
    case SYS_AUDIO_PLAY_FILE: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && audio_play_file(path) ? 0 : (uint64_t) -1;
    }
    case SYS_OPEN_CUBE3D_WINDOW:
        return (uint64_t) -1;
    case SYS_SOCKET_CALL:
        if (frame->rbx == SOCKET_CALL_UDP_OPEN) {
            socket_open_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_open_request_t *) frame->rcx);
            }
            request.handle = socket_udp_open(request.local_port);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((socket_open_request_t *) frame->rcx) = request;
            }
            return request.handle >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_CLOSE) {
            return socket_close((int32_t) frame->rcx) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_SENDTO) {
            socket_sendto_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_sendto_request_t *) frame->rcx);
            }
            if (exec_active() && !app_memory_user_range(request.payload, request.payload_len)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) socket_sendto_ipv4(request.handle,
                                                request.dst_host,
                                                request.dst_port,
                                                request.payload,
                                                request.payload_len);
        }
        if (frame->rbx == SOCKET_CALL_RECVFROM) {
            socket_recvfrom_request_t request;
            char src_ip[16];
            uint16_t src_port = 0;
            int32_t result;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_recvfrom_request_t *) frame->rcx);
            }
            if (exec_active() && !app_memory_user_range(request.buffer, request.buffer_size)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            result = socket_recvfrom_ipv4(request.handle,
                                         src_ip,
                                         &src_port,
                                         request.buffer,
                                         request.buffer_size);
            if (result > 0) {
                strcpy(request.src_ip, src_ip);
                request.src_port = src_port;
                if (exec_active()) {
                    (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
                } else {
                    *((socket_recvfrom_request_t *) frame->rcx) = request;
                }
            }
            return (uint64_t) result;
        }
        return (uint64_t) -1;
    case SYS_FUTEX_CALL:
        if (frame->rbx == FUTEX_CALL_WAIT) {
            futex_wait_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((futex_wait_request_t *) frame->rcx);
            }
            request.result = futex_wait(request.address, request.expected, request.timeout_ticks);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((futex_wait_request_t *) frame->rcx) = request;
            }
            return request.result >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == FUTEX_CALL_WAKE) {
            futex_wake_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((futex_wake_request_t *) frame->rcx);
            }
            request.result = futex_wake(request.address, request.count);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((futex_wake_request_t *) frame->rcx) = request;
            }
            return 0;
        }
        if (frame->rbx == FUTEX_CALL_COUNT) {
            futex_wake_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((futex_wake_request_t *) frame->rcx);
            }
            request.result = futex_waiter_count(request.address);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((futex_wake_request_t *) frame->rcx) = request;
            }
            return 0;
        }
        return (uint64_t) -1;
    case SYS_IPC_CALL:
        if (frame->rbx == IPC_CALL_CREATE) {
            ipc_create_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((ipc_create_request_t *) frame->rcx);
            }
            request.port_id = ipc_port_create(request.name);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((ipc_create_request_t *) frame->rcx) = request;
            }
            return request.port_id >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == IPC_CALL_SEND) {
            ipc_send_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((ipc_send_request_t *) frame->rcx);
            }
            return ipc_send_text(request.port_id, request.text) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == IPC_CALL_RECV) {
            ipc_recv_request_t request;
            char text[IPC_MESSAGE_MAX];

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((ipc_recv_request_t *) frame->rcx);
            }
            if (exec_active() && !app_memory_user_range(request.buffer, request.buffer_size)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            request.result = ipc_recv_text(request.port_id, text, sizeof(text));
            if (request.result > 0) {
                uint32_t copy_size = (uint32_t) request.result + 1u;
                if (copy_size > request.buffer_size) {
                    copy_size = request.buffer_size;
                }
                if (exec_active()) {
                    (void) app_memory_copy_to_user(request.buffer, text, copy_size);
                } else {
                    memcpy(request.buffer, text, copy_size);
                }
            }
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((ipc_recv_request_t *) frame->rcx) = request;
            }
            return request.result >= 0 ? 0 : (uint64_t) -1;
        }
        return (uint64_t) -1;
    case SYS_SIGNAL_CALL: {
        signal_request_t request;

        if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            request = *((signal_request_t *) frame->rcx);
        }
        if (frame->rbx == SIGNAL_CALL_SEND) {
            return signal_send(request.pid, (uint8_t) request.signo) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SIGNAL_CALL_PENDING) {
            request.mask = signal_pending(request.pid);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((signal_request_t *) frame->rcx) = request;
            }
            return 0;
        }
        if (frame->rbx == SIGNAL_CALL_TAKE) {
            request.mask = signal_take_pending(request.pid);
            if (exec_active()) {
                (void) app_memory_copy_to_user((void *) frame->rcx, &request, sizeof(request));
            } else {
                *((signal_request_t *) frame->rcx) = request;
            }
            return 0;
        }
        if (frame->rbx == SIGNAL_CALL_CLEAR) {
            signal_clear(request.pid);
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
        return (uint64_t) -1;
    }
    case SYS_REQUEST_R2: {
        const exec_launch_info_t *info = exec_current_launch_info();
        const char *reason = (const char *) frame->rbx;
        char reason_copy[128];

        if (info == NULL) {
            return (uint64_t) -1;
        }
        if (info->privilege_level <= EXEC_PRIV_R2) {
            return 0;
        }
        if (exec_active() && reason != NULL) {
            if (!app_memory_copy_string_from_user(reason_copy, sizeof(reason_copy), reason)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            reason = reason_copy;
        }
        if (!graphics_request_uac_elevation(info->program_path, reason, EXEC_PRIV_R2)) {
            return (uint64_t) -1;
        }
        ((exec_launch_info_t *) info)->privilege_level = EXEC_PRIV_R2;
        ((exec_launch_info_t *) info)->image_flags |= EXEC_IMAGE_FLAG_NEEDS_R2;
        return 0;
    }
    case SYS_REGISTRY_GET:
        return syscall_registry_get((const char *) frame->rbx, (char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_REGISTRY_SET:
        return syscall_registry_set((const char *) frame->rbx, (const char *) frame->rcx);
    case SYS_DEFAULT_APP_GET:
        return syscall_default_app_get((const char *) frame->rbx, (char *) frame->rcx, (uint32_t) frame->rdx);
    case SYS_DEFAULT_APP_SET:
        return syscall_default_app_set((const char *) frame->rbx, (const char *) frame->rcx);
    case SYS_EXEC_DEFER:
        return syscall_exec_defer((const char *) frame->rbx);
    case SYS_INSTALLER_CALL:
        return syscall_installer_call(frame->rbx, (void *) frame->rcx, (uint32_t) frame->rdx);
    default:
        return (uint64_t) -1;
    }
}
