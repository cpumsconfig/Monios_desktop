#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "windows_dll.h"

static const app_launch_info_t *g_launch_info;

const app_launch_info_t *app_launch_info(void)
{
    return g_launch_info;
}

void app_runtime_set_launch_info(const app_launch_info_t *info)
{
    g_launch_info = info;
}

uint64_t app_ticks(void)
{
    return monios_get_ticks();
}

void app_sleep_ticks(uint32_t ticks)
{
    uint64_t end = app_ticks() + ticks;

    while (app_ticks() < end) {
        asm volatile ("pause");
    }
}

void app_log(const char *text)
{
    monios_log_string(text);
}

int app_getcwd(char *buffer, uint32_t size)
{
    return (int) monios_getcwd(buffer, size);
}

int app_get_mouse(app_mouse_snapshot_t *snapshot)
{
    return (int) monios_get_mouse(snapshot);
}

int app_get_system_status(app_system_status_t *status)
{
    return (int) monios_get_system_status(status);
}

int app_file_read(const char *path, void *buffer, uint32_t size)
{
    return (int) monios_file_read(path, buffer, size);
}

int app_file_write(const char *path, const void *buffer, uint32_t size)
{
    return (int) monios_file_write(path, buffer, size);
}

int app_file_size(const char *path)
{
    return (int) monios_file_size(path);
}

bool app_file_exists(const char *path)
{
    return monios_file_exists(path) != 0;
}

bool app_file_is_dir(const char *path)
{
    return monios_file_is_dir(path) != 0;
}

bool app_file_delete(const char *path)
{
    return monios_file_delete(path) == 0;
}

bool app_file_mkdir(const char *path)
{
    return monios_file_mkdir(path) == 0;
}

bool app_file_rmdir(const char *path)
{
    return monios_file_rmdir(path) == 0;
}

int app_file_list_dir(const char *path, char *buffer, uint32_t size)
{
    return (int) monios_file_list_dir(path, buffer, size);
}

void app_enter_graphics_mode(void)
{
    windows_enter_graphics_mode();
}

int app_audio_play_file(const char *path)
{
    return (int) monios_audio_play_file(path);
}

int app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    return (int) windows_fill_rect(x, y, width, height, color);
}

int app_graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    return (int) windows_draw_text(x, y, text, color);
}

void app_graphics_present(void)
{
    windows_present();
}

int app_socket_udp_open(uint16_t local_port)
{
    return (int) monios_socket_udp_open(local_port);
}

int app_socket_close(int handle)
{
    return (int) monios_socket_close(handle);
}

int app_socket_sendto(int handle, const char *dst_host, uint16_t dst_port, const void *payload, uint16_t payload_len)
{
    return (int) monios_socket_sendto(handle, dst_host, dst_port, payload, payload_len);
}

int app_socket_recvfrom(int handle, char *src_ip, uint16_t *src_port, void *buffer, uint16_t buffer_size)
{
    return (int) monios_socket_recvfrom(handle, src_ip, src_port, buffer, buffer_size);
}

int app_futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks)
{
    return (int) monios_futex_wait(address, expected, timeout_ticks);
}

int app_futex_wake(uint64_t address, uint32_t count)
{
    return (int) monios_futex_wake(address, count);
}

int app_ipc_create(const char *name)
{
    return (int) monios_ipc_create(name);
}

int app_ipc_send(int port_id, const char *text)
{
    return (int) monios_ipc_send(port_id, text);
}

int app_ipc_recv(int port_id, char *buffer, uint32_t buffer_size)
{
    return (int) monios_ipc_recv(port_id, buffer, buffer_size);
}

int app_signal_send(int pid, uint32_t signo)
{
    return (int) monios_signal_send(pid, signo);
}

uint32_t app_signal_pending(int pid)
{
    return monios_signal_pending(pid);
}

uint32_t app_signal_take(int pid)
{
    return monios_signal_take(pid);
}

void app_signal_clear(int pid)
{
    monios_signal_clear(pid);
}

bool app_request_r2(const char *reason)
{
    return monios_request_r2(reason) == 0;
}

bool app_request_r0(const char *reason)
{
    return monios_request_r0(reason) == 0;
}

bool app_registry_get(const char *key, char *value, uint32_t value_size)
{
    return monios_registry_get(key, value, value_size) == 0;
}

bool app_registry_set(const char *key, const char *value)
{
    return monios_registry_set(key, value) == 0;
}

bool app_default_app_get(const char *extension, char *app_path, uint32_t app_path_size)
{
    return monios_default_app_get(extension, app_path, app_path_size) == 0;
}

bool app_default_app_set(const char *extension, const char *app_path)
{
    return monios_default_app_set(extension, app_path) == 0;
}

bool app_defer_exec(const char *path)
{
    return monios_defer_exec(path) == 0;
}

bool app_installer_boot_media(void)
{
    return monios_installer_boot_media() != 0;
}

int app_installer_list_targets(app_installer_target_list_t *list)
{
    return (int) monios_installer_list_targets(list);
}

int app_installer_write_disk(const char *source_path, uint32_t source_offset, uint32_t disk_lba, uint32_t byte_count)
{
    return (int) monios_installer_write_disk(source_path, source_offset, disk_lba, byte_count);
}

int app_installer_write_buffer(const void *data, uint32_t disk_lba, uint32_t byte_count)
{
    return (int) monios_installer_write_buffer(data, disk_lba, byte_count);
}

int app_installer_write_target_file(const char *target_path, const void *data, uint32_t target_lba, uint32_t byte_count)
{
    return (int) monios_installer_write_target_file(target_path, data, target_lba, byte_count);
}

int app_installer_copy_target_file(const char *source_path, uint32_t source_offset, uint32_t byte_count, const char *target_path, uint32_t target_lba)
{
    return (int) monios_installer_copy_target_file(source_path, source_offset, byte_count, target_path, target_lba);
}

int app_installer_read_media(const char *source_path, uint32_t source_offset, void *data, uint32_t byte_count)
{
    return (int) monios_installer_read_media(source_path, source_offset, data, byte_count);
}

int app_installer_media_size(const char *source_path)
{
    return (int) monios_installer_media_size(source_path);
}

void app_installer_reboot(void)
{
    monios_installer_reboot();
}

void app_exit(int code)
{
    monios_exit_process(code);
    for (;;) {
    }
}
