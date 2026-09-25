#include "common.h"
#include "app_memory.h"
#include "console.h"
#include "exec.h"
#include "file.h"
#include "futex.h"
#include "graphics.h"
#include "http.h"
#include "interrupt.h"
#include "installer.h"
#include "ipc.h"
#include "pipe.h"
#include "shm.h"
#include "keyboard.h"
#include "kernel.h"
#include "memory.h"
#include "memtest.h"
#include "mmu.h"
#include "mouse.h"
#include "net.h"
#include "path.h"
#include "pcb.h"
#include "registry.h"
#include "rtc.h"
#include "shell.h"
#include "signal.h"
#include "session.h"
#include "socket.h"
#include "syscall.h"
#include "system_status.h"
#include "task.h"
#include "audio.h"
#include "print.h"
#include "lpt.h"
#include "gpu.h"
#include "smp.h"
#include "terminal.h"
#include "cpu.h"
#include "driver_manager.h"
#include "ui.h"
#include "profiler.h"
#include "memstats.h"
#include "firewall.h"
#include "audit.h"

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
static char g_syscall_http_response[SYS_HTTP_RESPONSE_MAX];

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

static char syscall_lower_ascii(char ch)
{
    return ch >= 'A' && ch <= 'Z' ? (char) (ch + ('a' - 'A')) : ch;
}

static bool syscall_path_prefix_ci(const char *path, const char *prefix, uint32_t prefix_length)
{
    for (uint32_t i = 0; i < prefix_length; i++) {
        if (syscall_lower_ascii(path[i]) != syscall_lower_ascii(prefix[i])) {
            return false;
        }
    }
    return true;
}

static bool syscall_path_write_allowed(const char *resolved_path)
{
    const session_user_t *user;
    uint32_t home_length;
    uint32_t path_length;

    if (resolved_path == NULL || !exec_active() ||
        exec_current_privilege_level() <= EXEC_PRIV_R2) {
        return true;
    }
    user = session_current_user();
    if (user == NULL || user->home[0] == '\0') {
        return false;
    }
    home_length = (uint32_t) strlen(user->home);
    path_length = (uint32_t) strlen(resolved_path);
    return home_length > 0 &&
           path_length > home_length &&
           strcasecmp(resolved_path, user->home) != 0 &&
           syscall_path_prefix_ci(resolved_path, user->home, home_length) &&
           resolved_path[home_length] == PATH_SEPARATOR;
}

static bool syscall_path_read_allowed(const char *resolved_path)
{
    if (resolved_path == NULL || !exec_active() ||
        exec_current_privilege_level() <= EXEC_PRIV_R2) {
        return true;
    }
    return strcasecmp(resolved_path, UI_AUTH_PATH) != 0 &&
           strcasecmp(resolved_path, UI_TRUST_ROOT_BUNDLE_PATH) != 0;
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

    if (exec_capture_active()) {
        exec_capture_write(buffer, size);
        return (int32_t) size;
    }

    if (exec_active()) {
        console_write_process_buffer(pcb_current_pid(), buffer, size);
    } else {
        console_write_buffer(buffer, size);
    }
    graphics_notify_process_output();
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

static bool syscall_copy_to_user_or_abort(void *dst, const void *src, uint32_t size)
{
    if (!exec_active() || app_memory_copy_to_user(dst, src, size)) {
        return true;
    }
    exec_abort_from_exception(13, 0);
    return false;
}

static bool syscall_fixed_string_terminated(const char *text, uint32_t size)
{
    if (text == NULL || size == 0) {
        return false;
    }
    for (uint32_t i = 0; i < size; i++) {
        if (text[i] == '\0') {
            return true;
        }
    }
    return false;
}

static bool syscall_futex_address_allowed(uint64_t address)
{
    if (address == 0) {
        return false;
    }
    return !exec_active() ||
           app_memory_user_range((const void *) (uintptr_t) address, sizeof(uint32_t));
}

static bool syscall_signal_target_allowed(int32_t pid)
{
    return !exec_active() ||
           exec_current_privilege_level() <= EXEC_PRIV_R2 ||
           pid == pcb_current_pid();
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
    snapshot.smp_firmware_processors = smp->firmware_processors;
    snapshot.smp_firmware_enabled_processors = smp->firmware_enabled_processors;

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
    char key[REGISTRY_KEY_MAX];
    char value[REGISTRY_VALUE_MAX];

    if (key_ptr == NULL || value_ptr == NULL) {
        return (uint64_t) -1;
    }
    if (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2) {
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
    char extension[32];
    char app_path[PATH_MAX_LEN];

    if (extension_ptr == NULL || app_path_ptr == NULL) {
        return (uint64_t) -1;
    }
    if (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2) {
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
    char path[PATH_MAX_LEN];

    if (path_ptr == NULL ||
        (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2)) {
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
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
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
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
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
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            *((installer_target_write_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_COPY_TARGET) {
        installer_target_copy_request_t request;
        char source_path[PATH_MAX_LEN];
        char target_path[PATH_MAX_LEN];
        int32_t result;

        if (request_ptr == NULL || request_size < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, request_ptr, sizeof(request)) ||
                !app_memory_copy_string_from_user(source_path, sizeof(source_path), request.source_path) ||
                !app_memory_copy_string_from_user(target_path, sizeof(target_path), request.target_path)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            request = *((installer_target_copy_request_t *) request_ptr);
            if (request.source_path == NULL || request.target_path == NULL ||
                strlen(request.source_path) >= sizeof(source_path) ||
                strlen(request.target_path) >= sizeof(target_path)) {
                return (uint64_t) -1;
            }
            strcpy(source_path, request.source_path);
            strcpy(target_path, request.target_path);
        }
        result = installer_copy_file_to_target(source_path,
                                               request.source_offset,
                                               request.byte_count,
                                               target_path,
                                               request.target_lba);
        request.bytes_written = result > 0 ? (uint32_t) result : 0;
        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            *((installer_target_copy_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_READ_MEDIA) {
        installer_media_read_request_t request;
        char source_path[PATH_MAX_LEN];
        uint8_t data[INSTALLER_MEDIA_READ_MAX];
        int32_t result;

        if (request_ptr == NULL || request_size < sizeof(request)) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, request_ptr, sizeof(request)) ||
                !app_memory_copy_string_from_user(source_path, sizeof(source_path), request.source_path) ||
                request.byte_count > sizeof(data)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            request = *((installer_media_read_request_t *) request_ptr);
            if (request.source_path == NULL || strlen(request.source_path) >= sizeof(source_path) ||
                request.byte_count > sizeof(data)) {
                return (uint64_t) -1;
            }
            strcpy(source_path, request.source_path);
        }
        result = installer_read_media_file(source_path,
                                           request.source_offset,
                                           data,
                                           request.byte_count);
        request.bytes_read = result > 0 ? (uint32_t) result : 0;
        if (result > 0 && exec_active() &&
            !app_memory_copy_to_user(request.data, data, (uint32_t) result)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t) -1;
        }
        if (result > 0 && !exec_active() && request.data != NULL) {
            memcpy(request.data, data, (uint32_t) result);
        }
        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            *((installer_media_read_request_t *) request_ptr) = request;
        }
        return (uint64_t) result;
    }
    if (call == INSTALLER_CALL_STAT_MEDIA) {
        installer_media_stat_request_t request;
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
            request = *((installer_media_stat_request_t *) request_ptr);
            if (request.source_path == NULL || strlen(request.source_path) >= sizeof(source_path)) {
                return (uint64_t) -1;
            }
            strcpy(source_path, request.source_path);
        }
        result = installer_media_file_size(source_path);
        request.file_size = result;
        request.exists = result >= 0 ? 1U : 0U;
        request.reserved[0] = 0;
        request.reserved[1] = 0;
        request.reserved[2] = 0;
        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort(request_ptr, &request, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            *((installer_media_stat_request_t *) request_ptr) = request;
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

/* --- New subsystem syscall handlers (batch expansion) --- */
extern uint64_t fs_cache_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);
extern uint64_t zcomp_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);
extern uint64_t iosched_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);
extern uint64_t gpu_accel_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);
extern uint64_t usb_mount_ctl(uint64_t sub, uint64_t arg1, uint64_t arg2);
extern uint64_t dos_exec_syscall(uint64_t path_ptr, uint64_t arg1, uint64_t arg2);
extern uint64_t debug_ctl(uint64_t op, uint64_t arg1, uint64_t arg2);
extern uint64_t profile_ctl(uint64_t op, uint64_t arg1, uint64_t arg2);
extern bool ntp_sync_all(void);
extern bool power_hibernate_to_disk(void);
extern void pcb_process_abort(int32_t pid, int32_t exit_code);
extern void graphics_clipboard_set_text(const char *text);
extern void graphics_notification_post(const char *title, const char *body);
extern void graphics_theme_set(bool dark, uint32_t accent);
extern void graphics_wallpaper_set(const char *path);
extern bool graphics_display_mode_set(uint16_t width, uint16_t height);
extern bool uac_request_elevation(const char *program_path, const char *reason, uint32_t privilege_level);
extern void firewall_set_enabled(bool enabled);
extern bool firewall_enabled(void);
extern bool efs_encrypt_file(const char *path);
extern bool efs_decrypt_file(const char *path);
extern bool efs_is_encrypted(const char *path);
#include "proc_mgmt.h"
#include "user_mgmt.h"
#include "fs_perm.h"
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
    case SYS_CONSOLE_SET_TITLE: {
        char title[TERMINAL_WINDOW_TITLE_MAX];

        if ((const char *) frame->rbx == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(title,
                                                  sizeof(title),
                                                  (const char *) frame->rbx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            strlcpy(title, (const char *) frame->rbx, sizeof(title));
        }
        return terminal_set_process_console_title(pcb_current_pid(), title) ? 0 : (uint64_t) -1;
    }
    case SYS_FILE_ROOT_COUNT:
        return file_root_entry_count();
    case SYS_FILE_READ: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        if (path == NULL || !syscall_path_read_allowed(path)) {
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
        if (path == NULL || !syscall_path_write_allowed(path)) {
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
        return path != NULL && syscall_path_write_allowed(path) && file_delete(path) ?
               0 : (uint64_t) -1;
    }
    case SYS_FILE_MKDIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && syscall_path_write_allowed(path) && file_mkdir(path) ?
               0 : (uint64_t) -1;
    }
    case SYS_FILE_RMDIR: {
        const char *path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && syscall_path_write_allowed(path) && file_rmdir(path) ?
               0 : (uint64_t) -1;
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
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, cwd, size + 1U)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            memcpy((void *) frame->rbx, cwd, size + 1U);
        }
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
        sys_exit((int32_t) frame->rbx);
        return 0;
    case SYS_FORK:
        return (uint64_t) sys_fork(frame);
    case SYS_WAITPID:
        return (uint64_t) sys_waitpid((int32_t) frame->rbx,
                                      (int32_t *) (uintptr_t) frame->rcx);
    case SYS_GETPID:
        return (uint64_t) sys_getpid();
    case SYS_GETPPID:
        return (uint64_t) sys_getppid();
    case SYS_SCHED_YIELD:
        sys_sched_yield();
        return 0;
    case SYS_SET_PRIORITY:
        pcb_set_priority(pcb_current_pid(), (uint32_t) frame->rbx);
        return 0;
    case SYS_BACKUP_CTL: {
        uint32_t backup_op = (uint32_t) frame->rbx;
        uint64_t ba = frame->rcx;
        uint64_t bb = frame->rdx;
        uint64_t bc = frame->rsi;
        uint64_t bd = frame->rdi;
        char img_path[PATH_MAX_LEN];

        /* raw partition imaging needs elevated privilege (R2 or above). */
        if (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2) {
            return (uint64_t) -1;
        }

        switch (backup_op) {
        case 1: { /* BEGIN_BACKUP: ba = user image path */
            int64_t total;
            if (!exec_active() ||
                !app_memory_copy_string_from_user(img_path, sizeof(img_path),
                                                  (const char *) ba)) {
                return (uint64_t) -1;
            }
            total = file_backup_begin(img_path);
            return (uint64_t) total;
        }
        case 2: { /* READ_SRC: ba=rel_lba, bb=sectors, bc=userbuf */
            void *buf = (void *) (uint64_t) bc;
            uint32_t nbytes = (uint32_t) bb * 512u;
            if (exec_active() && !app_memory_user_range(buf, nbytes)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) file_backup_read_src((uint32_t) ba, (uint32_t) bb, buf);
        }
        case 3: { /* APPEND_IMG: ba=userbuf, bb=len */
            const void *buf = (const void *) (uint64_t) ba;
            uint32_t len = (uint32_t) bb;
            if (exec_active() && !app_memory_user_range((void *) buf, len)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) file_backup_append(buf, len);
        }
        case 4: /* END_BACKUP */
            return (uint64_t) file_backup_end();
        case 5: { /* BEGIN_RESTORE: ba = user image path */
            int64_t sz;
            if (!exec_active() ||
                !app_memory_copy_string_from_user(img_path, sizeof(img_path),
                                                  (const char *) ba)) {
                return (uint64_t) -1;
            }
            sz = file_backup_restore_begin(img_path);
            return (uint64_t) sz;
        }
        case 6: { /* READ_IMG: ba=user image path, bb=offset, bc=userbuf, bd=len */
            void *buf = (void *) (uint64_t) bc;
            uint32_t len = (uint32_t) bd;
            if (!exec_active() ||
                !app_memory_copy_string_from_user(img_path, sizeof(img_path),
                                                  (const char *) ba) ||
                !app_memory_user_range(buf, len)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) file_backup_read_img(img_path, (uint32_t) bb, buf, len);
        }
        case 7: { /* WRITE_SRC: ba=rel_lba, bb=sectors, bc=userbuf */
            const void *buf = (const void *) (uint64_t) bc;
            uint32_t nbytes = (uint32_t) bb * 512u;
            if (exec_active() && !app_memory_user_range((void *) buf, nbytes)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) file_backup_write_src((uint32_t) ba, (uint32_t) bb, buf);
        }
        case 8: /* END_RESTORE */
            return (uint64_t) file_backup_restore_end();
        default:
            return (uint64_t) -1;
        }
    }
    case SYS_SYSTEM_STATUS:
        return (uint64_t) syscall_system_status((system_status_t *) frame->rbx, (uint32_t) frame->rcx);
    case SYS_HTTP_GET_URL: {
        char url[HTTP_MAX_URL];
        char *buffer = (char *) frame->rcx;
        uint32_t buffer_size = (uint32_t) frame->rdx;
        uint32_t fetch_size = buffer_size;
        int32_t result;

        if ((const char *) frame->rbx == NULL || buffer == NULL || buffer_size == 0) {
            return (uint64_t) -1;
        }
        if (fetch_size > sizeof(g_syscall_http_response) - 1U) {
            fetch_size = sizeof(g_syscall_http_response) - 1U;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(url, sizeof(url), (const char *) frame->rbx) ||
                !app_memory_user_range(buffer, fetch_size)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            strlcpy(url, (const char *) frame->rbx, sizeof(url));
        }
        result = http_get_url(url, g_syscall_http_response, fetch_size);
        if (result < 0) {
            return (uint64_t) result;
        }
        if (exec_active()) {
            if (!app_memory_copy_to_user(buffer,
                                         g_syscall_http_response,
                                         (uint32_t) result + 1U)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            memcpy(buffer, g_syscall_http_response, (uint32_t) result + 1U);
        }
        return (uint64_t) result;
    }
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
    case SYS_GRAPHICS_GET_WIDTH:
        return graphics_framebuffer_width();
    case SYS_GRAPHICS_GET_HEIGHT:
        return graphics_framebuffer_height();
    case SYS_AUDIO_PLAY_PCM: {
        audio_pcm_submit_request_t request;
        uint8_t *pcm;
        bool copied;
        bool played;

        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(request)))) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&request, (void *) frame->rbx, sizeof(request))) {
                return (uint64_t) -1;
            }
        } else {
            request = *((audio_pcm_submit_request_t *) frame->rbx);
        }
        if (request.data == NULL || request.byte_count == 0 ||
            request.byte_count > AUDIO_PCM_MAX_BYTES ||
            request.sample_rate == 0 || request.channels == 0 ||
            request.bits_per_sample == 0) {
            return (uint64_t) -1;
        }
        if (exec_active() && !app_memory_user_range(request.data, request.byte_count)) {
            return (uint64_t) -1;
        }
        pcm = (uint8_t *) kmalloc(request.byte_count);
        if (pcm == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            copied = app_memory_copy_from_user(pcm, request.data, request.byte_count);
        } else {
            memcpy(pcm, request.data, request.byte_count);
            copied = true;
        }
        played = copied && audio_play_pcm(pcm, request.byte_count, request.sample_rate,
                                           request.channels, request.bits_per_sample);
        kfree(pcm);
        return played ? 0 : (uint64_t) -1;
    }
    case SYS_OPEN_CUBE3D_WINDOW:
        return graphics_open_cube3d_window() ? 0 : (uint64_t) -1;
    case SYS_OPEN_NOTEPAD_WINDOW:
        return graphics_open_notepad_window() ? 0 : (uint64_t) -1;
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
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
            if (!syscall_fixed_string_terminated(request.dst_host, sizeof(request.dst_host))) {
                return (uint64_t) -1;
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
                    if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                        return (uint64_t) -1;
                    }
                } else {
                    *((socket_recvfrom_request_t *) frame->rcx) = request;
                }
            }
            return (uint64_t) result;
        }
        if (frame->rbx == SOCKET_CALL_TCP_OPEN) {
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
            request.handle = socket_tcp_open(request.local_port);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                *((socket_open_request_t *) frame->rcx) = request;
            }
            return request.handle >= 0 ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_TCP_CONNECT) {
            socket_tcp_connect_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_tcp_connect_request_t *) frame->rcx);
            }
            if (!syscall_fixed_string_terminated(request.dst_host, sizeof(request.dst_host))) {
                return (uint64_t) -1;
            }
            return socket_tcp_connect(request.handle, request.dst_host, request.dst_port)
                       ? 0
                       : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_TCP_SEND) {
            socket_tcp_send_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_tcp_send_request_t *) frame->rcx);
            }
            if (exec_active() && !app_memory_user_range((void *) request.data, request.len)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) socket_tcp_send(request.handle, request.data, request.len);
        }
        if (frame->rbx == SOCKET_CALL_TCP_RECV) {
            socket_tcp_recv_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_tcp_recv_request_t *) frame->rcx);
            }
            if (exec_active() && !app_memory_user_range((void *) request.buffer, request.buffer_size)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            return (uint64_t) socket_tcp_recv(request.handle, request.buffer, request.buffer_size);
        }
        if (frame->rbx == SOCKET_CALL_TCP_HAS_DATA) {
            return socket_tcp_has_data((int32_t) frame->rcx) ? 1U : 0U;
        }
        if (frame->rbx == SOCKET_CALL_TCP_CONNECTED) {
            return socket_tcp_is_connected((int32_t) frame->rcx) ? 1U : 0U;
        }
        if (frame->rbx == SOCKET_CALL_TCP_LISTEN) {
            socket_simple_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_simple_request_t *) frame->rcx);
            }
            return socket_tcp_listen(request.handle) == 0 ? 0U : (uint64_t) -1;
        }
        if (frame->rbx == SOCKET_CALL_TCP_ACCEPT) {
            socket_tcp_accept_request_t request;

            if ((void *) frame->rcx == NULL || frame->rdx < sizeof(request)) {
                return (uint64_t) -1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(&request, (void *) frame->rcx, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                request = *((socket_tcp_accept_request_t *) frame->rcx);
            }
            request.client = socket_tcp_accept(request.handle, request.client_ip, &request.client_port);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                *((socket_tcp_accept_request_t *) frame->rcx) = request;
            }
            return request.client >= 0 ? 0U : (uint64_t) -1;
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
            if (!syscall_futex_address_allowed(request.address)) {
                return (uint64_t) -1;
            }
            request.result = futex_wait(request.address, request.expected, request.timeout_ticks);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
            if (!syscall_futex_address_allowed(request.address)) {
                return (uint64_t) -1;
            }
            request.result = futex_wake(request.address, request.count);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
            if (!syscall_futex_address_allowed(request.address)) {
                return (uint64_t) -1;
            }
            request.result = futex_waiter_count(request.address);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
            if (!syscall_fixed_string_terminated(request.name, sizeof(request.name))) {
                return (uint64_t) -1;
            }
            request.port_id = ipc_port_create(request.name);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
            if (!syscall_fixed_string_terminated(request.text, sizeof(request.text))) {
                return (uint64_t) -1;
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
                    if (!syscall_copy_to_user_or_abort(request.buffer, text, copy_size)) {
                        return (uint64_t) -1;
                    }
                } else {
                    memcpy(request.buffer, text, copy_size);
                }
            }
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
        if (!syscall_signal_target_allowed(request.pid)) {
            return (uint64_t) -1;
        }
        if (frame->rbx == SIGNAL_CALL_SEND) {
            return signal_send(request.pid, (uint8_t) request.signo) ? 0 : (uint64_t) -1;
        }
        if (frame->rbx == SIGNAL_CALL_PENDING) {
            request.mask = signal_pending(request.pid);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
            } else {
                *((signal_request_t *) frame->rcx) = request;
            }
            return 0;
        }
        if (frame->rbx == SIGNAL_CALL_TAKE) {
            request.mask = signal_take_pending(request.pid);
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rcx, &request, sizeof(request))) {
                    return (uint64_t) -1;
                }
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
        uint32_t image_flags = exec_current_image_flags();

        if (!exec_active() ||
            (image_flags & EXEC_IMAGE_FLAG_DRIVER) == 0 ||
            (image_flags & EXEC_IMAGE_FLAG_SIGNED) == 0) {
            return (uint64_t) -1;
        }
        if (exec_current_privilege_level() == EXEC_PRIV_R0) {
            return 0;
        }
        return (uint64_t) -1;
    }
    case SYS_REQUEST_R2: {
        const char *reason = (const char *) frame->rbx;
        char reason_copy[128];

        if (!exec_active()) {
            return (uint64_t) -1;
        }
        if (exec_current_privilege_level() <= EXEC_PRIV_R2) {
            return 0;
        }
        if (exec_active() && reason != NULL) {
            if (!app_memory_copy_string_from_user(reason_copy, sizeof(reason_copy), reason)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            reason = reason_copy;
        }
        if (!graphics_request_uac_elevation(exec_current_program_path(), reason, EXEC_PRIV_R2)) {
            return (uint64_t) -1;
        }
        return exec_grant_current_privilege(EXEC_PRIV_R2) ? 0 : (uint64_t) -1;
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
    case SYS_DRIVER_LOAD: {
        const char *path;

        if (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2) {
            return (uint64_t) -1;
        }
        path = syscall_resolve_path((const char *) frame->rbx, resolved);
        return path != NULL && driver_manager_load(path) ? 0 : (uint64_t) -1;
    }
    case SYS_DRIVER_UNLOAD: {
        const char *name_ptr = (const char *) frame->rbx;
        char name[PATH_MAX_LEN];

        if (exec_active() && exec_current_privilege_level() > EXEC_PRIV_R2) {
            return (uint64_t) -1;
        }
        if (name_ptr == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(name, sizeof(name), name_ptr)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            strlcpy(name, name_ptr, sizeof(name));
        }
        return driver_manager_unload(name, false) ? 0 : (uint64_t) -1;
    }
    case SYS_DRIVER_QUERY: {
        driver_status_snapshot_t snapshot;

        if ((void *) frame->rbx == NULL ||
            frame->rcx < sizeof(snapshot) ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(snapshot)))) {
            return (uint64_t) -1;
        }
        if (!driver_manager_snapshot(&snapshot)) {
            return (uint64_t) -1;
        }
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
    /* --- Batch expansion syscalls 44-69 --- */
    case SYS_CLIPBOARD_SET:
        if (frame->rbx != 0) {
            char cb_text[256];
            if (exec_active()) {
                if (app_memory_copy_string_from_user(cb_text, sizeof(cb_text), (const char *) frame->rbx))
                    graphics_clipboard_set_text(cb_text);
            } else {
                graphics_clipboard_set_text((const char *) frame->rbx);
            }
        }
        return 0;
    case SYS_CLIPBOARD_GET: {
        char cb[256];
        uint32_t n = graphics_clipboard_get_text(cb, sizeof(cb));
        uint32_t cap = (uint32_t) frame->rcx;
        if (cap == 0U) {
            return n;
        }
        if (n + 1U > cap) {
            n = cap - 1U;
        }
        cb[n] = '\0';
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, cb, n + 1U)) {
                return (uint64_t)-1;
            }
        } else {
            memcpy((void *) frame->rbx, cb, n + 1U);
        }
        return n;
    }
    case SYS_CLIPBOARD_HISTORY: {
        char cb[1024];
        uint32_t n = graphics_clipboard_history_pack(cb, sizeof(cb));
        uint32_t cap = (uint32_t) frame->rcx;
        if (cap == 0U) {
            return graphics_clipboard_history_count();
        }
        if (n >= cap) {
            n = cap - 1U;
        }
        cb[n] = '\0';
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, cb, n + 1U)) {
                return (uint64_t)-1;
            }
        } else {
            memcpy((void *) frame->rbx, cb, n + 1U);
        }
        return n;
    }
    case SYS_NOTIFICATION_POST: {
        char n_title[128], n_body[256];
        const char *t, *b;
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(n_title, sizeof(n_title), (const char *) frame->rbx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            if (!app_memory_copy_string_from_user(n_body, sizeof(n_body), (const char *) frame->rcx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            t = n_title; b = n_body;
        } else {
            t = (const char *) frame->rbx; b = (const char *) frame->rcx;
        }
        if (t && b) graphics_notification_post(t, b);
        return 0;
    }
    case SYS_THEME_SET:
        graphics_theme_set(frame->rbx != 0, (uint32_t) frame->rcx);
        return 0;
    case SYS_WALLPAPER_SET: {
        char wp_path[256];
        const char *p;
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(wp_path, sizeof(wp_path), (const char *) frame->rbx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            p = wp_path;
        } else {
            p = (const char *) frame->rbx;
        }
        if (p) graphics_wallpaper_set(p);
        return 0;
    }
    case SYS_DISPLAY_MODE:
        return graphics_display_mode_set((uint16_t) frame->rbx, (uint16_t) frame->rcx) ? 0 : (uint64_t)-1;
    case SYS_PROCESS_ENUM: {
        pcb_t snap;
        if (!pcb_snapshot((uint32_t) frame->rbx, &snap)) return (uint64_t)-1;
        if (exec_active()) {
            if (!app_memory_user_range((void *) frame->rcx, sizeof(snap))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            app_memory_copy_to_user((void *) frame->rcx, &snap, sizeof(snap));
        } else {
            *(pcb_t *) frame->rcx = snap;
        }
        return 0;
    }
    case SYS_PROCESS_TERMINATE: {
        int32_t target_pid = (int32_t) frame->rbx;
        if (!syscall_signal_target_allowed(target_pid)) return (uint64_t)-1;
        pcb_process_abort(target_pid, -1);
        return 0;
    }
    case SYS_PROCESS_STATS: {
        process_info_t stats;
        if (!process_stats((int32_t) frame->rbx, &stats)) return (uint64_t)-1;
        if (exec_active()) {
            if (!app_memory_user_range((void *) frame->rcx, sizeof(stats))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            app_memory_copy_to_user((void *) frame->rcx, &stats, sizeof(stats));
        } else {
            *(process_info_t *) frame->rcx = stats;
        }
        return 0;
    }
    case SYS_POWER_SET_STATE:
        if (frame->rbx == 4) return power_hibernate_to_disk() ? 0 : (uint64_t)-1;
        return 0;
    case SYS_AUDIO_MIXER_CTL:
        return 0;
    case SYS_AUDIO_PLAYER_CTL: {
        audio_player_ctl_request_t req;

        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(req)))) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&req, (void *) frame->rbx, sizeof(req))) {
                return (uint64_t) -1;
            }
        } else {
            req = *((audio_player_ctl_request_t *) frame->rbx);
        }
        {
            int32_t rc = audio_player_ctl(&req);
            req.position = audio_player_position_bytes();
            req.total = audio_player_total_bytes();
            req.playing = audio_is_playing() ? 1u : 0u;
            req.paused = audio_is_paused() ? 1u : 0u;
            if (exec_active()) {
                if (!syscall_copy_to_user_or_abort((void *) frame->rbx, &req, sizeof(req))) {
                    return (uint64_t) -1;
                }
            } else {
                *((audio_player_ctl_request_t *) frame->rbx) = req;
            }
            return rc == 0 ? 0u : (uint64_t) -1;
        }
    }
    case SYS_AUDIO_REC_CTL: {
        audio_record_request_t rreq;
        static uint8_t rec_bounce[8192U];
        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(rreq)))) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&rreq, (void *) frame->rbx, sizeof(rreq))) {
                return (uint64_t) -1;
            }
        } else {
            rreq = *((audio_record_request_t *) frame->rbx);
        }
        if (rreq.cmd == AUDIO_REC_CMD_READ) {
            void *userbuf = rreq.buffer;
            uint32_t cap = rreq.capacity;
            if (cap > sizeof(rec_bounce)) cap = (uint32_t) sizeof(rec_bounce);
            rreq.capacity = cap;
            rreq.buffer = rec_bounce;
            audio_record_ctl(&rreq);
            if (exec_active()) {
                if (!app_memory_user_range(userbuf, rreq.bytes_copied)) return (uint64_t) -1;
                app_memory_copy_to_user(userbuf, rec_bounce, rreq.bytes_copied);
            } else {
                memcpy(userbuf, rec_bounce, rreq.bytes_copied);
            }
            rreq.buffer = userbuf;
        } else {
            audio_record_ctl(&rreq);
        }
        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort((void *) frame->rbx, &rreq, sizeof(rreq))) {
                return (uint64_t) -1;
            }
        } else {
            *((audio_record_request_t *) frame->rbx) = rreq;
        }
        return 0;
    }
    case SYS_VIDEO_CTL:
        return video_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_LPT_CTL:
        return lpt_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_MEMTEST_CTL: {
        memtest_ctl_request_t mreq;

        if ((void *) frame->rbx == NULL ||
            (exec_active() &&
             !app_memory_user_range((void *) frame->rbx, sizeof(mreq)))) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&mreq, (void *) frame->rbx, sizeof(mreq))) {
                return (uint64_t) -1;
            }
        } else {
            mreq = *((memtest_ctl_request_t *) frame->rbx);
        }

        (void) memtest_ctl(&mreq);

        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort((void *) frame->rbx, &mreq, sizeof(mreq))) {
                return (uint64_t) -1;
            }
        } else {
            *((memtest_ctl_request_t *) frame->rbx) = mreq;
        }
        return mreq.result == 0 ? 0u : (uint64_t) -1;
    }
    case SYS_TIME_NTP_SYNC:
        return ntp_sync_all() ? 0 : (uint64_t)-1;
    case SYS_PRINTER_CTL: {
        printer_ctl_request_t preq;
        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(preq)))) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_from_user(&preq, (void *) frame->rbx, sizeof(preq))) {
                return (uint64_t) -1;
            }
        } else {
            preq = *((printer_ctl_request_t *) frame->rbx);
        }
        preq.result = printer_ctl(&preq);
        if (exec_active()) {
            if (!syscall_copy_to_user_or_abort((void *) frame->rbx, &preq, sizeof(preq))) {
                return (uint64_t) -1;
            }
        } else {
            *((printer_ctl_request_t *) frame->rbx) = preq;
        }
        return preq.result == 0 ? 0u : (uint64_t) -1;
    }
    case SYS_FS_CACHE_CTL:
        return fs_cache_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_MEM_COMPRESS_CTL:
        return zcomp_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_IOSCHED_CTL:
        return iosched_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_GPU_ACCEL_CTL:
        return gpu_accel_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_FIREWALL_CTL: {
        /* sub-ops: 0=set_enabled, 1=is_enabled, 2=add_rule, 3=remove_rule,
         *          4=list_rules, 5=save_cfg, 6=dropped_counters */
        if (frame->rbx == 0) { firewall_set_enabled(frame->rcx != 0); audit_log(AUDIT_FIREWALL_CHANGE, "kernel", frame->rcx ? "enable" : "disable", true); return 0; }
        if (frame->rbx == 1) return firewall_enabled() ? 1 : 0;
        if (frame->rbx == 2) {
            fw_rule_arg_t karg;
            fw_rule_arg_t *a = (exec_active()) ? &karg : (fw_rule_arg_t *)frame->rcx;
            if (exec_active()) {
                if (!app_memory_user_range((void *)frame->rcx, sizeof(fw_rule_arg_t))) return (uint64_t)-1;
                if (!app_memory_copy_from_user(&karg, (void *)frame->rcx, sizeof(karg))) return (uint64_t)-1;
            }
            int32_t id = firewall_add_rule(a->direction, a->action, a->protocol,
                                           a->src_ip, a->src_mask,
                                           a->dst_ip, a->dst_mask,
                                           a->src_port, a->dst_port);
            audit_log(AUDIT_FIREWALL_CHANGE, "kernel", id >= 0 ? "add-rule" : "add-fail", id >= 0);
            return (uint64_t) id;
        }
        if (frame->rbx == 3) {
            int32_t id = (int32_t) frame->rcx;
            bool ok = firewall_remove_rule(id);
            audit_log(AUDIT_FIREWALL_CHANGE, "kernel", "remove-rule", ok);
            return ok ? 0 : (uint64_t)-1;
        }
        if (frame->rbx == 4) {
            uint32_t cap = (uint32_t) frame->rdx;
            char tmp[1024];
            uint32_t n = firewall_list_rules(tmp, sizeof(tmp));
            if (n > cap) n = cap;
            if (exec_active()) {
                if (!app_memory_user_range((void *)frame->rcx, cap)) return (uint64_t)-1;
                if (!app_memory_copy_to_user((void *)frame->rcx, tmp, n)) return (uint64_t)-1;
            } else {
                memcpy((void *)frame->rcx, tmp, n);
            }
            return n;
        }
        if (frame->rbx == 5) {
            bool ok = firewall_save_rules("/Monios/System/Config/firewall.cfg");
            audit_log(AUDIT_FIREWALL_CHANGE, "kernel", "save", ok);
            return ok ? 0 : (uint64_t)-1;
        }
        if (frame->rbx == 6) {
            uint32_t counters[2] = { firewall_dropped_inbound(), firewall_dropped_outbound() };
            if (exec_active()) {
                if (!app_memory_user_range((void *)frame->rcx, sizeof(counters))) return (uint64_t)-1;
                app_memory_copy_to_user((void *)frame->rcx, counters, sizeof(counters));
            } else {
                memcpy((void *)frame->rcx, counters, sizeof(counters));
            }
            return 0;
        }
        return (uint64_t)-1;
    }
    case SYS_EFS_CTL: {
        char efs_path[256];
        const char *ep;
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(efs_path, sizeof(efs_path), (const char *) frame->rcx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            ep = efs_path;
        } else {
            ep = (const char *) frame->rcx;
        }
        if (!ep) return (uint64_t)-1;
        if (frame->rbx == 0) return efs_encrypt_file(ep) ? 0 : (uint64_t)-1;
        if (frame->rbx == 1) return efs_decrypt_file(ep) ? 0 : (uint64_t)-1;
        if (frame->rbx == 2) return efs_is_encrypted(ep) ? 1 : 0;
        return (uint64_t)-1;
    }
    case SYS_UAC_ELEVATE: {
        char uac_prog[256], uac_reason[256];
        const char *up, *ur;
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(uac_prog, sizeof(uac_prog), (const char *) frame->rbx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            if (!app_memory_copy_string_from_user(uac_reason, sizeof(uac_reason), (const char *) frame->rcx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            up = uac_prog; ur = uac_reason;
        } else {
            up = (const char *) frame->rbx; ur = (const char *) frame->rcx;
        }
        return uac_request_elevation(up ? up : "", ur ? ur : "", (uint32_t) frame->rdx) ? 0 : (uint64_t)-1;
    }
    case SYS_USB_MOUNT_CTL:
        return usb_mount_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_DOS_EXEC:
        return dos_exec_syscall(frame->rbx, frame->rcx, frame->rdx);
    case SYS_DEBUG_CTL:
        return debug_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_PROFILE_CTL:
        return profile_ctl(frame->rbx, frame->rcx, frame->rdx);
    case SYS_PACKAGE_CTL:
        return 0;
    case SYS_NET_PING: {
        /* rbx = NUL-terminated host string (IPv4 literal or hostname).
         * Returns 0 when the ICMP echo reply was seen, -1 otherwise. */
        char target[128];
        const char *t;

        if ((const char *) frame->rbx == NULL) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(target, sizeof(target), (const char *) frame->rbx)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            t = target;
        } else {
            t = (const char *) frame->rbx;
        }
        return net_ping(t) ? 0 : (uint64_t) -1;
    }
    case SYS_NET_RESOLVE: {
        /* rbx = hostname string, rcx = user char[16] output buffer,
         * rdx = buffer size. Resolves to IPv4 and writes dotted text. */
        char host[128];
        uint8_t ip[4];
        char text[16];
        uint32_t out_size = (uint32_t) frame->rdx;
        uint32_t i;
        uint32_t pos = 0;
        const char *h;

        if ((const char *) frame->rbx == NULL || (void *) frame->rcx == NULL || out_size == 0) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(host, sizeof(host), (const char *) frame->rbx) ||
                !app_memory_user_range((void *) frame->rcx, out_size)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
            h = host;
        } else {
            h = (const char *) frame->rbx;
        }
        if (!net_resolve_ipv4(h, ip)) {
            return (uint64_t) -1;
        }
        /* format dotted quad (no snprintf in freestanding kernel) */
        for (i = 0; i < 4; i++) {
            uint8_t octet = ip[i];
            char part[4];
            uint32_t n = 0;
            uint32_t b;

            if (octet >= 100) {
                part[n++] = (char) ('0' + octet / 100);
                octet %= 100;
            }
            if (octet >= 10 || n > 0) {
                part[n++] = (char) ('0' + octet / 10);
                octet %= 10;
            }
            part[n++] = (char) ('0' + octet);
            for (b = 0; b < n && pos + 1 < out_size; b++) {
                text[pos++] = part[b];
            }
            if (i < 3 && pos + 1 < out_size) {
                text[pos++] = '.';
            }
        }
        text[pos] = '\0';
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rcx, text, pos + 1)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            memcpy((void *) frame->rcx, text, pos + 1);
        }
        return 0;
    }
    case SYS_KEYBOARD_READ_EVENT: {
        /* rbx = user buffer of 8 bytes: uint32 type, char ch, uint8 mods,
         * uint8 pad[2]. Returns 1 when an event was copied, 0 when the
         * keyboard queue is empty. Exposes special keys (arrows, F-keys,
         * Esc) that keyboard_read_char() deliberately discards. */
        key_event_t ev;
        uint8_t out[8];

        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(out)))) {
            return (uint64_t) -1;
        }
        if (!keyboard_poll_event(&ev)) {
            return 0;
        }
        out[0] = (uint8_t) ((uint32_t) ev.type & 0xFFu);
        out[1] = (uint8_t) (((uint32_t) ev.type >> 8) & 0xFFu);
        out[2] = (uint8_t) (((uint32_t) ev.type >> 16) & 0xFFu);
        out[3] = (uint8_t) (((uint32_t) ev.type >> 24) & 0xFFu);
        out[4] = (uint8_t) ev.ch;
        out[5] = (uint8_t) ((ev.status.shift_down ? 0x01u : 0u) |
                            (ev.status.ctrl_down ? 0x02u : 0u) |
                            (ev.status.alt_down ? 0x04u : 0u));
        out[6] = 0;
        out[7] = 0;
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, out, sizeof(out))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t) -1;
            }
        } else {
            memcpy((void *) frame->rbx, out, sizeof(out));
        }
        return 1;
    }
    case SYS_GRAPHICS_READ_FRAMEBUFFER: {
        /* rbx = user BGRA8888 row-major buffer;
         * rcx = (x<<48)|(y<<32)|(width<<16)|height. */
        void *dst = (void *) frame->rbx;
        uint16_t x = (uint16_t)((frame->rcx >> 48) & 0xFFFFu);
        uint16_t y = (uint16_t)((frame->rcx >> 32) & 0xFFFFu);
        uint16_t width = (uint16_t)((frame->rcx >> 16) & 0xFFFFu);
        uint16_t height = (uint16_t)(frame->rcx & 0xFFFFu);
        uint32_t clipped_bytes;

        if (dst == NULL || width == 0 || height == 0) {
            return (uint64_t)-1;
        }
        if (exec_active()) {
            uint64_t need = (uint64_t)width * (uint64_t)height * 4u;

            if (!app_memory_user_range(dst, need)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
        }
        {
            uint8_t *tmp = (uint8_t *)kmalloc((uint64_t)width * height * 4u);

            if (tmp == NULL) {
                return (uint64_t)-1;
            }
            clipped_bytes = graphics_user_read_framebuffer(tmp, x, y, width, height);
            if (clipped_bytes == 0 ||
                (exec_active() && !app_memory_copy_to_user(dst, tmp, clipped_bytes))) {
                kfree(tmp);
                return (uint64_t)-1;
            }
            if (!exec_active()) {
                memcpy(dst, tmp, clipped_bytes);
            }
            kfree(tmp);
        }
        return (uint64_t)clipped_bytes;
    }

    case SYS_GET_RTC_TIME: {
        /* rbx = user 8-byte buffer: uint16 year, uint8 month/day/
         * hour/minute/second, pad[2]. */
        cmos_time_t t;
        uint8_t out[8];

        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, sizeof(out)))) {
            return (uint64_t)-1;
        }
        cmos_read_time(&t);
        out[0] = (uint8_t)(t.year & 0xFFu);
        out[1] = (uint8_t)(t.year >> 8);
        out[2] = t.month;
        out[3] = t.day;
        out[4] = t.hour;
        out[5] = t.minute;
        out[6] = t.second;
        out[7] = 0;
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) frame->rbx, out, sizeof(out))) {
                return (uint64_t)-1;
            }
        } else {
            memcpy((void *) frame->rbx, out, sizeof(out));
        }
        return 0;
    }

    /* Task 25: 性能采样器控制。 */
    case SYS_PROFILER_START:
        return (uint64_t)profiler_start(frame->rbx);
    case SYS_PROFILER_STOP:
        profiler_stop();
        return 0;
    case SYS_PROFILER_GET_DATA: {
        /* rbx = 用户缓冲区, rcx = 缓冲区字节数。 */
        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx, frame->rcx))) {
            return (uint64_t)-1;
        }
        int64_t n = profiler_copy_to_user((void *) frame->rbx, frame->rcx);
        if (n < 0) {
            return (uint64_t)-1;
        }
        return (uint64_t)n;
    }

    /* Task 26: 细粒度内存统计。 rbx = 用户 memstats_snapshot_t 缓冲区。 */
    case SYS_MEMORY_STATS: {
        if ((void *) frame->rbx == NULL ||
            (exec_active() && !app_memory_user_range((void *) frame->rbx,
                                                     sizeof(memstats_snapshot_t)))) {
            return (uint64_t)-1;
        }
        memstats_fill((memstats_snapshot_t *) (uintptr_t) frame->rbx);
        return 0;
    }
    /* CGI: run a .exe and capture its stdout (SYS_EXEC_CAPTURE). */
    case SYS_EXEC_CAPTURE: {
        char cgi_path[PATH_MAX_LEN];
        uint32_t cgi_cap = (uint32_t) frame->rdx;
        char *cgi_kbuf = NULL;
        int32_t cgi_exit = -1;
        bool cgi_ok;
        uint32_t cgi_n;

        if ((void *) frame->rbx == NULL || (void *) frame->rcx == NULL ||
            cgi_cap == 0U || cgi_cap > 65536U) {
            return (uint64_t)-1;
        }
        if (exec_active()) {
            if (!app_memory_copy_string_from_user(cgi_path, sizeof(cgi_path),
                                                  (const char *) (uintptr_t) frame->rbx) ||
                !app_memory_user_range((void *) (uintptr_t) frame->rcx, cgi_cap)) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
            if (frame->rsi != 0U &&
                !app_memory_user_range((void *) (uintptr_t) frame->rsi, sizeof(int32_t))) {
                exec_abort_from_exception(13, 0);
                return (uint64_t)-1;
            }
        } else {
            const char *upath = (const char *) (uintptr_t) frame->rbx;
            if (upath == NULL || strlen(upath) >= sizeof(cgi_path)) {
                return (uint64_t)-1;
            }
            strcpy(cgi_path, upath);
        }

        {
            char query_str[256];
            char *envp[4];
            char env_qs[280];
            uint32_t env_count = 0;

            /* Read query_string from user (arg4 = rdi) */
            query_str[0] = '\0';
            if (frame->rdi != 0U) {
                const char *uqs = (const char *) (uintptr_t) frame->rdi;
                uint32_t i = 0;
                while (*uqs != '\0' && i < sizeof(query_str) - 1U) {
                    query_str[i++] = *uqs++;
                }
                query_str[i] = '\0';
            }

            /* Build environment variables */
            if (query_str[0] != '\0') {
                strcpy(env_qs, "QUERY_STRING=");
                strcat(env_qs, query_str);
                envp[env_count++] = env_qs;
            }
            envp[env_count++] = "REQUEST_METHOD=GET";
            envp[env_count++] = "SERVER_SOFTWARE=Monios/1.0";
            envp[env_count] = NULL;

            cgi_kbuf = (char *) kmalloc(cgi_cap);
            if (cgi_kbuf == NULL) {
                return (uint64_t)-1;
            }
            cgi_ok = exec_run_capture(cgi_path, cgi_kbuf, cgi_cap, &cgi_exit,
                                      env_count > 1U ? envp : NULL);
        }
        if (!cgi_ok) {
            kfree(cgi_kbuf);
            return (uint64_t)-1;
        }

        cgi_n = (uint32_t) strlen(cgi_kbuf);
        if (cgi_n >= cgi_cap) {
            cgi_n = cgi_cap - 1U;
        }
        if (exec_active()) {
            app_memory_copy_to_user((void *) (uintptr_t) frame->rcx, cgi_kbuf, cgi_n + 1U);
            if (frame->rsi != 0U) {
                app_memory_copy_to_user((void *) (uintptr_t) frame->rsi, &cgi_exit, sizeof(cgi_exit));
            }
        }
        kfree(cgi_kbuf);
        return (uint64_t) cgi_n;
    }
    case SYS_GRAPHICS_BLIT: {
        /* rbx = user source pixel buffer (width*height pixels, BGRA8888
         *       little-endian = 0xAARRGGBB, row-major);
         * rcx = packed (x<<48)|(y<<32)|(width<<16)|height;
         * rdx = flags (bit0 = source-over alpha blend; 0 = overwrite).
         * Returns pixels written (clipped) or (uint64_t)-1. */
        void *src = (void *)(uintptr_t) frame->rbx;
        uint16_t x = (uint16_t)((frame->rcx >> 48) & 0xFFFFu);
        uint16_t y = (uint16_t)((frame->rcx >> 32) & 0xFFFFu);
        uint16_t width = (uint16_t)((frame->rcx >> 16) & 0xFFFFu);
        uint16_t height = (uint16_t)(frame->rcx & 0xFFFFu);
        uint32_t flags = (uint32_t) frame->rdx;
        uint64_t need;
        uint32_t written;

        if (src == NULL || width == 0 || height == 0) {
            return (uint64_t)-1;
        }
        need = (uint64_t)width * (uint64_t)height * 4u;
        if (exec_active() && !app_memory_user_range(src, need)) {
            exec_abort_from_exception(13, 0);
            return (uint64_t)-1;
        }
        {
            uint8_t *tmp = (uint8_t *)kmalloc(need);

            if (tmp == NULL) {
                return (uint64_t)-1;
            }
            if (exec_active()) {
                if (!app_memory_copy_from_user(tmp, src, need)) {
                    kfree(tmp);
                    exec_abort_from_exception(13, 0);
                    return (uint64_t)-1;
                }
            } else {
                memcpy(tmp, src, (uint32_t)need);
            }
            written = graphics_user_blit(tmp, x, y, width, height, flags);
            kfree(tmp);
        }
        return (uint64_t)written;
    }
    case SYS_PIPE: {
        int32_t fds[2];

        fds[0] = -1;
        fds[1] = -1;
        if ((void *) frame->rbx == NULL) {
            return (uint64_t) -1;
        }
        if (pipe_create(&fds[0], &fds[1]) != 0) {
            return (uint64_t) -1;
        }
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *)(uintptr_t) frame->rbx, fds, sizeof(fds))) {
                return (uint64_t) -1;
            }
        } else {
            ((int32_t *)(uintptr_t) frame->rbx)[0] = fds[0];
            ((int32_t *)(uintptr_t) frame->rbx)[1] = fds[1];
        }
        return 0;
    }
    case SYS_SHM_CALL: {
        uint64_t shm_op = frame->rbx;

        if (shm_op == SHM_OP_CREATE) {
            int32_t id = shm_create((uint32_t) frame->rcx, (uint32_t) frame->rdx);
            return (uint64_t) id;
        }
        if (shm_op == SHM_OP_ATTACH) {
            void *addr = shm_attach((int32_t) frame->rcx);
            return (uint64_t)(uintptr_t) addr;
        }
        if (shm_op == SHM_OP_DETACH) {
            return (uint64_t) shm_detach((const void *)(uintptr_t) frame->rcx);
        }
        if (shm_op == SHM_OP_DESTROY) {
            return (uint64_t) shm_destroy((int32_t) frame->rcx);
        }
        return (uint64_t) -1;
    }
    case SYS_SETPGID:
        return (uint64_t) pcb_setpgid((int32_t) frame->rbx, (int32_t) frame->rcx);
    case SYS_GETPGID: {
        int32_t qpid = (int32_t) frame->rbx;
        if (qpid == 0) {
            qpid = pcb_current_pid();
        }
        return (uint64_t) pcb_getpgid(qpid);
    }
    case SYS_SETSID:
        return (uint64_t) pcb_setsid();
    default:
        return (uint64_t) -1;
    }
}
