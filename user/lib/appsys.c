#include "appsys.h"
#include "futex.h"
#include "ipc.h"
#include "signal.h"
#include "socket.h"
#include "stddef.h"
#include "string.h"
#include "syscall.h"

#define APP_INSTALLER_CALL_BOOT_MEDIA 0U
#define APP_INSTALLER_CALL_WRITE_DISK 1U
#define APP_INSTALLER_CALL_REBOOT     2U
#define APP_INSTALLER_CALL_WRITE_TARGET 3U
#define APP_INSTALLER_CALL_LIST_TARGETS 4U
#define APP_INSTALLER_CALL_WRITE_BUFFER 5U

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
    return syscall0(SYS_GET_TICKS);
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
    (void) syscall1(SYS_LOG_STRING, (uint64_t) text);
}

int app_getcwd(char *buffer, uint32_t size)
{
    return (int) syscall2(SYS_GET_CWD, (uint64_t) buffer, size);
}

int app_get_mouse(app_mouse_snapshot_t *snapshot)
{
    return (int) syscall2(SYS_MOUSE_GET_STATE, (uint64_t) snapshot, sizeof(*snapshot));
}

int app_get_system_status(app_system_status_t *status)
{
    return (int) syscall2(SYS_SYSTEM_STATUS, (uint64_t) status, sizeof(*status));
}

int app_file_read(const char *path, void *buffer, uint32_t size)
{
    return (int) syscall3(SYS_FILE_READ, (uint64_t) path, (uint64_t) buffer, size);
}

int app_file_write(const char *path, const void *buffer, uint32_t size)
{
    return (int) syscall3(SYS_FILE_WRITE, (uint64_t) path, (uint64_t) buffer, size);
}

int app_file_size(const char *path)
{
    return (int) syscall1(SYS_FILE_SIZE, (uint64_t) path);
}

bool app_file_exists(const char *path)
{
    return syscall1(SYS_FILE_EXISTS, (uint64_t) path) != 0;
}

bool app_file_is_dir(const char *path)
{
    return syscall1(SYS_FILE_IS_DIR, (uint64_t) path) != 0;
}

bool app_file_delete(const char *path)
{
    return syscall1(SYS_FILE_DELETE, (uint64_t) path) == 0;
}

bool app_file_mkdir(const char *path)
{
    return syscall1(SYS_FILE_MKDIR, (uint64_t) path) == 0;
}

bool app_file_rmdir(const char *path)
{
    return syscall1(SYS_FILE_RMDIR, (uint64_t) path) == 0;
}

int app_file_list_dir(const char *path, char *buffer, uint32_t size)
{
    return (int) syscall3(SYS_FILE_LIST_DIR, (uint64_t) path, (uint64_t) buffer, size);
}

void app_enter_graphics_mode(void)
{
    (void) syscall0(SYS_ENTER_GRAPHICS_MODE);
}

int app_audio_play_file(const char *path)
{
    return (int) syscall1(SYS_AUDIO_PLAY_FILE, (uint64_t) path);
}

int app_graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32) | ((uint64_t) width << 16) | height;

    return (int) syscall2(SYS_GRAPHICS_FILL_RECT, pos, color);
}

int app_graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    uint64_t pos = ((uint64_t) x << 48) | ((uint64_t) y << 32);

    return (int) syscall3(SYS_GRAPHICS_DRAW_TEXT, pos, (uint64_t) text, color);
}

void app_graphics_present(void)
{
    (void) syscall0(SYS_GRAPHICS_PRESENT);
}

int app_socket_udp_open(uint16_t local_port)
{
    socket_open_request_t request;

    request.local_port = local_port;
    request.handle = -1;
    if (syscall3(SYS_SOCKET_CALL, SOCKET_CALL_UDP_OPEN, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.handle;
}

int app_socket_close(int handle)
{
    return (int) syscall2(SYS_SOCKET_CALL, SOCKET_CALL_CLOSE, (uint64_t) handle);
}

int app_socket_sendto(int handle, const char *dst_host, uint16_t dst_port, const void *payload, uint16_t payload_len)
{
    socket_sendto_request_t request;

    request.handle = handle;
    memset(request.dst_host, 0, sizeof(request.dst_host));
    if (dst_host != NULL && strlen(dst_host) < sizeof(request.dst_host)) {
        strcpy(request.dst_host, dst_host);
    }
    request.dst_port = dst_port;
    request.payload = (const uint8_t *) payload;
    request.payload_len = payload_len;
    return (int) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_SENDTO, (uint64_t) &request, sizeof(request));
}

int app_socket_recvfrom(int handle, char *src_ip, uint16_t *src_port, void *buffer, uint16_t buffer_size)
{
    socket_recvfrom_request_t request;
    int ret;

    request.handle = handle;
    request.src_ip[0] = '\0';
    request.src_port = 0;
    request.buffer = (uint8_t *) buffer;
    request.buffer_size = buffer_size;
    ret = (int) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_RECVFROM, (uint64_t) &request, sizeof(request));
    if (ret > 0) {
        if (src_ip != NULL) {
            strcpy(src_ip, request.src_ip);
        }
        if (src_port != NULL) {
            *src_port = request.src_port;
        }
    }
    return ret;
}

int app_futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks)
{
    futex_wait_request_t request;

    request.address = address;
    request.expected = expected;
    request.timeout_ticks = timeout_ticks;
    request.result = -1;
    if (syscall3(SYS_FUTEX_CALL, FUTEX_CALL_WAIT, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.result;
}

int app_futex_wake(uint64_t address, uint32_t count)
{
    futex_wake_request_t request;

    request.address = address;
    request.count = count;
    request.result = 0;
    if (syscall3(SYS_FUTEX_CALL, FUTEX_CALL_WAKE, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return (int) request.result;
}

static void app_copy_limited(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t index = 0;

    if (dst_size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < dst_size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

int app_ipc_create(const char *name)
{
    ipc_create_request_t request;

    app_copy_limited(request.name, sizeof(request.name), name);
    request.port_id = -1;
    if (syscall3(SYS_IPC_CALL, IPC_CALL_CREATE, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.port_id;
}

int app_ipc_send(int port_id, const char *text)
{
    ipc_send_request_t request;

    request.port_id = port_id;
    app_copy_limited(request.text, sizeof(request.text), text);
    return (int) syscall3(SYS_IPC_CALL, IPC_CALL_SEND, (uint64_t) &request, sizeof(request));
}

int app_ipc_recv(int port_id, char *buffer, uint32_t buffer_size)
{
    ipc_recv_request_t request;

    request.port_id = port_id;
    request.buffer = buffer;
    request.buffer_size = buffer_size;
    request.result = -1;
    if (syscall3(SYS_IPC_CALL, IPC_CALL_RECV, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.result;
}

int app_signal_send(int pid, uint32_t signo)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = signo;
    request.mask = 0;
    return (int) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_SEND, (uint64_t) &request, sizeof(request));
}

uint32_t app_signal_pending(int pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_PENDING, (uint64_t) &request, sizeof(request));
    return request.mask;
}

uint32_t app_signal_take(int pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_TAKE, (uint64_t) &request, sizeof(request));
    return request.mask;
}

void app_signal_clear(int pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_CLEAR, (uint64_t) &request, sizeof(request));
}

bool app_request_r2(const char *reason)
{
    return syscall1(SYS_REQUEST_R2, (uint64_t) reason) == 0;
}

bool app_request_r0(const char *reason)
{
    return syscall1(SYS_REQUEST_R0, (uint64_t) reason) == 0;
}

bool app_registry_get(const char *key, char *value, uint32_t value_size)
{
    return syscall3(SYS_REGISTRY_GET, (uint64_t) key, (uint64_t) value, value_size) == 0;
}

bool app_registry_set(const char *key, const char *value)
{
    return syscall2(SYS_REGISTRY_SET, (uint64_t) key, (uint64_t) value) == 0;
}

bool app_default_app_get(const char *extension, char *app_path, uint32_t app_path_size)
{
    return syscall3(SYS_DEFAULT_APP_GET, (uint64_t) extension, (uint64_t) app_path, app_path_size) == 0;
}

bool app_default_app_set(const char *extension, const char *app_path)
{
    return syscall2(SYS_DEFAULT_APP_SET, (uint64_t) extension, (uint64_t) app_path) == 0;
}

bool app_defer_exec(const char *path)
{
    return syscall1(SYS_EXEC_DEFER, (uint64_t) path) == 0;
}

bool app_installer_boot_media(void)
{
    return syscall3(SYS_INSTALLER_CALL, APP_INSTALLER_CALL_BOOT_MEDIA, 0, 0) != 0;
}

int app_installer_list_targets(app_installer_target_list_t *list)
{
    if (list == NULL) {
        return -1;
    }
    return (int) syscall3(SYS_INSTALLER_CALL,
                          APP_INSTALLER_CALL_LIST_TARGETS,
                          (uint64_t) list,
                          sizeof(*list));
}

int app_installer_write_disk(const char *source_path, uint32_t source_offset, uint32_t disk_lba, uint32_t byte_count)
{
    app_installer_write_request_t request;

    request.source_path = source_path;
    request.source_offset = source_offset;
    request.disk_lba = disk_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int) syscall3(SYS_INSTALLER_CALL,
                          APP_INSTALLER_CALL_WRITE_DISK,
                          (uint64_t) &request,
                          sizeof(request));
}

int app_installer_write_buffer(const void *data, uint32_t disk_lba, uint32_t byte_count)
{
    app_installer_buffer_write_request_t request;

    request.data = data;
    request.disk_lba = disk_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int) syscall3(SYS_INSTALLER_CALL,
                          APP_INSTALLER_CALL_WRITE_BUFFER,
                          (uint64_t) &request,
                          sizeof(request));
}

int app_installer_write_target_file(const char *target_path, const void *data, uint32_t target_lba, uint32_t byte_count)
{
    app_installer_target_write_request_t request;

    request.target_path = target_path;
    request.data = data;
    request.target_lba = target_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int) syscall3(SYS_INSTALLER_CALL,
                          APP_INSTALLER_CALL_WRITE_TARGET,
                          (uint64_t) &request,
                          sizeof(request));
}

void app_installer_reboot(void)
{
    (void) syscall3(SYS_INSTALLER_CALL, APP_INSTALLER_CALL_REBOOT, 0, 0);
}

void app_exit(int code)
{
    (void) syscall1(SYS_EXIT_PROCESS, (uint64_t) code);
    for (;;) {
    }
}
