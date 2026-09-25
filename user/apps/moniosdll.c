#define MONIOS_DLL_BUILD 1
#include "audio.h"
#include "monios_dll.h"
#include "futex.h"
#include "ipc.h"
#include "signal.h"
#include "socket.h"
#include "stddef.h"
#include "string.h"
#include "syscall.h"
#include "icons_data.h"

#define MONIOS_DLL_EXPORT __attribute__((dllexport))

#define APP_INSTALLER_CALL_BOOT_MEDIA 0U
#define APP_INSTALLER_CALL_WRITE_DISK 1U
#define APP_INSTALLER_CALL_REBOOT     2U
#define APP_INSTALLER_CALL_WRITE_TARGET 3U
#define APP_INSTALLER_CALL_LIST_TARGETS 4U
#define APP_INSTALLER_CALL_WRITE_BUFFER 5U
#define APP_INSTALLER_CALL_COPY_TARGET 6U
#define APP_INSTALLER_CALL_READ_MEDIA 7U
#define APP_INSTALLER_CALL_STAT_MEDIA 8U

int DllMainCRTStartup(void *module, uint32_t reason, void *reserved)
{
    (void) module;
    (void) reason;
    (void) reserved;
    return 1;
}

MONIOS_DLL_EXPORT
uint32_t monios_abi_version(void)
{
    return MONIOS_DLL_ABI_VERSION;
}

MONIOS_DLL_EXPORT
uint64_t monios_syscall0(uint64_t nr)
{
    return syscall0(nr);
}

MONIOS_DLL_EXPORT
uint64_t monios_syscall1(uint64_t nr, uint64_t arg0)
{
    return syscall1(nr, arg0);
}

MONIOS_DLL_EXPORT
uint64_t monios_syscall2(uint64_t nr, uint64_t arg0, uint64_t arg1)
{
    return syscall2(nr, arg0, arg1);
}

MONIOS_DLL_EXPORT
uint64_t monios_syscall3(uint64_t nr, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    return syscall3(nr, arg0, arg1, arg2);
}

MONIOS_DLL_EXPORT
uint64_t monios_get_ticks(void)
{
    return syscall0(SYS_GET_TICKS);
}

MONIOS_DLL_EXPORT
void monios_log_string(const char *text)
{
    (void) syscall1(SYS_LOG_STRING, (uint64_t) text);
}

MONIOS_DLL_EXPORT
int32_t monios_console_set_title(const char *title)
{
    return (int32_t) syscall1(SYS_CONSOLE_SET_TITLE, (uint64_t) title);
}

MONIOS_DLL_EXPORT
int32_t monios_getcwd(char *buffer, uint32_t size)
{
    return (int32_t) syscall2(SYS_GET_CWD, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_get_mouse(app_mouse_snapshot_t *snapshot)
{
    return (int32_t) syscall2(SYS_MOUSE_GET_STATE, (uint64_t) snapshot, sizeof(*snapshot));
}

MONIOS_DLL_EXPORT
int32_t monios_get_system_status(app_system_status_t *status)
{
    return (int32_t) syscall2(SYS_SYSTEM_STATUS, (uint64_t) status, sizeof(*status));
}

MONIOS_DLL_EXPORT
int32_t monios_http_get_url(const char *url, char *buffer, uint32_t buffer_size)
{
    return (int32_t) syscall3(SYS_HTTP_GET_URL,
                              (uint64_t) url,
                              (uint64_t) buffer,
                              buffer_size);
}

MONIOS_DLL_EXPORT
int32_t monios_handle_write(uint64_t handle, const void *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_HANDLE_WRITE, handle, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_handle_read(uint64_t handle, void *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_HANDLE_READ, handle, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_file_read(const char *path, void *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_FILE_READ, (uint64_t) path, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_file_write(const char *path, const void *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_FILE_WRITE, (uint64_t) path, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_file_size(const char *path)
{
    return (int32_t) syscall1(SYS_FILE_SIZE, (uint64_t) path);
}

MONIOS_DLL_EXPORT
int32_t monios_file_exists(const char *path)
{
    return syscall1(SYS_FILE_EXISTS, (uint64_t) path) != 0 ? 1 : 0;
}

MONIOS_DLL_EXPORT
int32_t monios_file_is_dir(const char *path)
{
    return syscall1(SYS_FILE_IS_DIR, (uint64_t) path) != 0 ? 1 : 0;
}

MONIOS_DLL_EXPORT
int32_t monios_file_delete(const char *path)
{
    return (int32_t) syscall1(SYS_FILE_DELETE, (uint64_t) path);
}

MONIOS_DLL_EXPORT
int32_t monios_file_mkdir(const char *path)
{
    return (int32_t) syscall1(SYS_FILE_MKDIR, (uint64_t) path);
}

MONIOS_DLL_EXPORT
int32_t monios_file_rmdir(const char *path)
{
    return (int32_t) syscall1(SYS_FILE_RMDIR, (uint64_t) path);
}

MONIOS_DLL_EXPORT
int32_t monios_file_list_dir(const char *path, char *buffer, uint32_t size)
{
    return (int32_t) syscall3(SYS_FILE_LIST_DIR, (uint64_t) path, (uint64_t) buffer, size);
}

MONIOS_DLL_EXPORT
int32_t monios_audio_play_pcm(const void *data, uint32_t byte_count,
                              uint32_t sample_rate, uint16_t channels,
                              uint16_t bits_per_sample)
{
    audio_pcm_submit_request_t request;

    request.data = data;
    request.byte_count = byte_count;
    request.sample_rate = sample_rate;
    request.channels = channels;
    request.bits_per_sample = bits_per_sample;
    return (int32_t) syscall1(SYS_AUDIO_PLAY_PCM, (uint64_t) &request);
}

MONIOS_DLL_EXPORT
int32_t monios_socket_udp_open(uint16_t local_port)
{
    socket_open_request_t request;

    request.local_port = local_port;
    request.handle = -1;
    if (syscall3(SYS_SOCKET_CALL, SOCKET_CALL_UDP_OPEN, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.handle;
}

MONIOS_DLL_EXPORT
int32_t monios_socket_close(int32_t handle)
{
    return (int32_t) syscall2(SYS_SOCKET_CALL, SOCKET_CALL_CLOSE, (uint64_t) handle);
}

MONIOS_DLL_EXPORT
int32_t monios_socket_sendto(int32_t handle, const char *dst_host,
                             uint16_t dst_port, const void *payload,
                             uint16_t payload_len)
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
    return (int32_t) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_SENDTO, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_socket_recvfrom(int32_t handle, char *src_ip,
                               uint16_t *src_port, void *buffer,
                               uint16_t buffer_size)
{
    socket_recvfrom_request_t request;
    int32_t result;

    request.handle = handle;
    request.src_ip[0] = '\0';
    request.src_port = 0;
    request.buffer = (uint8_t *) buffer;
    request.buffer_size = buffer_size;
    result = (int32_t) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_RECVFROM, (uint64_t) &request, sizeof(request));
    if (result > 0) {
        if (src_ip != NULL) {
            strcpy(src_ip, request.src_ip);
        }
        if (src_port != NULL) {
            *src_port = request.src_port;
        }
    }
    return result;
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_open(uint16_t local_port)
{
    socket_open_request_t request;

    request.local_port = local_port;
    request.handle = -1;
    if (syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_OPEN, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.handle;
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_connect(int32_t handle, const char *dst_host, uint16_t dst_port)
{
    socket_tcp_connect_request_t request;

    request.handle = handle;
    memset(request.dst_host, 0, sizeof(request.dst_host));
    if (dst_host != NULL && strlen(dst_host) < sizeof(request.dst_host)) {
        strcpy(request.dst_host, dst_host);
    }
    request.dst_port = dst_port;
    return (int32_t) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_CONNECT, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_send(int32_t handle, const void *data, uint16_t len)
{
    socket_tcp_send_request_t request;

    request.handle = handle;
    request.data = (const uint8_t *) data;
    request.len = len;
    return (int32_t) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_SEND, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_recv(int32_t handle, void *buffer, uint16_t buffer_size)
{
    socket_tcp_recv_request_t request;

    request.handle = handle;
    request.buffer = (uint8_t *) buffer;
    request.buffer_size = buffer_size;
    return (int32_t) syscall3(SYS_SOCKET_CALL, SOCKET_CALL_TCP_RECV, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_has_data(int32_t handle)
{
    return (int32_t) syscall2(SYS_SOCKET_CALL, SOCKET_CALL_TCP_HAS_DATA, (uint64_t) handle);
}

MONIOS_DLL_EXPORT
int32_t monios_socket_tcp_connected(int32_t handle)
{
    return (int32_t) syscall2(SYS_SOCKET_CALL, SOCKET_CALL_TCP_CONNECTED, (uint64_t) handle);
}

MONIOS_DLL_EXPORT
int32_t monios_futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks)
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

MONIOS_DLL_EXPORT
int32_t monios_futex_wake(uint64_t address, uint32_t count)
{
    futex_wake_request_t request;

    request.address = address;
    request.count = count;
    request.result = 0;
    if (syscall3(SYS_FUTEX_CALL, FUTEX_CALL_WAKE, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return (int32_t) request.result;
}

static void monios_copy_limited(char *dst, uint32_t dst_size, const char *src)
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

MONIOS_DLL_EXPORT
int32_t monios_ipc_create(const char *name)
{
    ipc_create_request_t request;

    monios_copy_limited(request.name, sizeof(request.name), name);
    request.port_id = -1;
    if (syscall3(SYS_IPC_CALL, IPC_CALL_CREATE, (uint64_t) &request, sizeof(request)) != 0) {
        return -1;
    }
    return request.port_id;
}

MONIOS_DLL_EXPORT
int32_t monios_ipc_send(int32_t port_id, const char *text)
{
    ipc_send_request_t request;

    request.port_id = port_id;
    monios_copy_limited(request.text, sizeof(request.text), text);
    return (int32_t) syscall3(SYS_IPC_CALL, IPC_CALL_SEND, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_ipc_recv(int32_t port_id, char *buffer, uint32_t buffer_size)
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

MONIOS_DLL_EXPORT
int32_t monios_signal_send(int32_t pid, uint32_t signo)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = signo;
    request.mask = 0;
    return (int32_t) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_SEND, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
uint32_t monios_signal_pending(int32_t pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_PENDING, (uint64_t) &request, sizeof(request));
    return request.mask;
}

MONIOS_DLL_EXPORT
uint32_t monios_signal_take(int32_t pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_TAKE, (uint64_t) &request, sizeof(request));
    return request.mask;
}

MONIOS_DLL_EXPORT
void monios_signal_clear(int32_t pid)
{
    signal_request_t request;

    request.pid = pid;
    request.signo = 0;
    request.mask = 0;
    (void) syscall3(SYS_SIGNAL_CALL, SIGNAL_CALL_CLEAR, (uint64_t) &request, sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_request_r2(const char *reason)
{
    return (int32_t) syscall1(SYS_REQUEST_R2, (uint64_t) reason);
}

MONIOS_DLL_EXPORT
int32_t monios_request_r0(const char *reason)
{
    return (int32_t) syscall1(SYS_REQUEST_R0, (uint64_t) reason);
}

MONIOS_DLL_EXPORT
int32_t monios_registry_get(const char *key, char *value, uint32_t value_size)
{
    return (int32_t) syscall3(SYS_REGISTRY_GET, (uint64_t) key, (uint64_t) value, value_size);
}

MONIOS_DLL_EXPORT
int32_t monios_registry_set(const char *key, const char *value)
{
    return (int32_t) syscall2(SYS_REGISTRY_SET, (uint64_t) key, (uint64_t) value);
}

MONIOS_DLL_EXPORT
int32_t monios_default_app_get(const char *extension, char *app_path, uint32_t app_path_size)
{
    return (int32_t) syscall3(SYS_DEFAULT_APP_GET, (uint64_t) extension, (uint64_t) app_path, app_path_size);
}

MONIOS_DLL_EXPORT
int32_t monios_default_app_set(const char *extension, const char *app_path)
{
    return (int32_t) syscall2(SYS_DEFAULT_APP_SET, (uint64_t) extension, (uint64_t) app_path);
}

MONIOS_DLL_EXPORT
int32_t monios_defer_exec(const char *path)
{
    return (int32_t) syscall1(SYS_EXEC_DEFER, (uint64_t) path);
}

MONIOS_DLL_EXPORT
int32_t monios_installer_boot_media(void)
{
    return syscall3(SYS_INSTALLER_CALL, APP_INSTALLER_CALL_BOOT_MEDIA, 0, 0) != 0 ? 1 : 0;
}

MONIOS_DLL_EXPORT
int32_t monios_installer_list_targets(app_installer_target_list_t *list)
{
    if (list == NULL) {
        return -1;
    }
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_LIST_TARGETS,
                              (uint64_t) list,
                              sizeof(*list));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_write_disk(const char *source_path,
                                    uint32_t source_offset,
                                    uint32_t disk_lba,
                                    uint32_t byte_count)
{
    app_installer_write_request_t request;

    request.source_path = source_path;
    request.source_offset = source_offset;
    request.disk_lba = disk_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_WRITE_DISK,
                              (uint64_t) &request,
                              sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_write_buffer(const void *data, uint32_t disk_lba, uint32_t byte_count)
{
    app_installer_buffer_write_request_t request;

    request.data = data;
    request.disk_lba = disk_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_WRITE_BUFFER,
                              (uint64_t) &request,
                              sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_write_target_file(const char *target_path,
                                           const void *data,
                                           uint32_t target_lba,
                                           uint32_t byte_count)
{
    app_installer_target_write_request_t request;

    request.target_path = target_path;
    request.data = data;
    request.target_lba = target_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_WRITE_TARGET,
                              (uint64_t) &request,
                              sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_copy_target_file(const char *source_path,
                                          uint32_t source_offset,
                                          uint32_t byte_count,
                                          const char *target_path,
                                          uint32_t target_lba)
{
    app_installer_target_copy_request_t request;

    request.source_path = source_path;
    request.source_offset = source_offset;
    request.target_path = target_path;
    request.target_lba = target_lba;
    request.byte_count = byte_count;
    request.bytes_written = 0;
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_COPY_TARGET,
                              (uint64_t) &request,
                              sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_read_media(const char *source_path,
                                    uint32_t source_offset,
                                    void *data,
                                    uint32_t byte_count)
{
    app_installer_media_read_request_t request;

    request.source_path = source_path;
    request.source_offset = source_offset;
    request.data = data;
    request.byte_count = byte_count;
    request.bytes_read = 0;
    return (int32_t) syscall3(SYS_INSTALLER_CALL,
                              APP_INSTALLER_CALL_READ_MEDIA,
                              (uint64_t) &request,
                              sizeof(request));
}

MONIOS_DLL_EXPORT
int32_t monios_installer_media_size(const char *source_path)
{
    app_installer_media_stat_request_t request;
    int32_t result;

    request.source_path = source_path;
    request.file_size = -1;
    request.exists = 0;
    request.reserved[0] = 0;
    request.reserved[1] = 0;
    request.reserved[2] = 0;
    result = (int32_t) syscall3(SYS_INSTALLER_CALL,
                                APP_INSTALLER_CALL_STAT_MEDIA,
                                (uint64_t) &request,
                                sizeof(request));
    return result >= 0 ? request.file_size : result;
}

MONIOS_DLL_EXPORT
void monios_installer_reboot(void)
{
    (void) syscall3(SYS_INSTALLER_CALL, APP_INSTALLER_CALL_REBOOT, 0, 0);
}

MONIOS_DLL_EXPORT
uint64_t monios_backup_ctl(uint32_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    return syscall5(SYS_BACKUP_CTL, (uint64_t) op, a, b, c, d);
}

MONIOS_DLL_EXPORT
void monios_exit_process(int32_t code)
{
    (void) syscall1(SYS_EXIT_PROCESS, (uint64_t) code);
}


MONIOS_DLL_EXPORT
int32_t monios_draw_icon(uint32_t icon_id, uint16_t x, uint16_t y, uint16_t size)
{
    /* 256x256x4 = 256 KB static scratch; avoids blowing the stack and lets us
     * reuse the buffer across calls. Freestanding: plain loops only. */
    static uint8_t s_icon_scratch[256u * 256u * 4u];
    uint64_t packed;

    if (icon_id >= MONIOS_ICON_COUNT) {
        return -1;
    }
    if (size == 0u || size > 256u) {
        return -1;
    }

    packed = ((uint64_t)x << 48) | ((uint64_t)y << 32) |
             ((uint64_t)size << 16) | (uint64_t)size;

    if (size == MONIOS_ICON_SIZE) {
        /* Fast path: the bundled asset is already 64x64, blit it directly. */
        return (int32_t) syscall3(SYS_GRAPHICS_BLIT,
                                  (uint64_t) g_icon_data[icon_id],
                                  packed, 1u);
    }

    /* Slow path: nearest-neighbor resample the 64x64 source into scratch,
     * then blit the scaled buffer. */
    {
        uint32_t oy;

        /* Edge-complete nearest-neighbour: destination [0,size-1] maps onto the
         * 64x64 source [0,63] inclusive (divide by size-1, guard size==1), so
         * every requested size (16/24/32/48) reproduces the whole icon instead
         * of truncating its right/bottom edge. */
        for (oy = 0; oy < (uint32_t)size; oy++) {
            uint32_t sy = (size <= 1u) ? 0u :
                          (oy * (MONIOS_ICON_SIZE - 1u)) / ((uint32_t)size - 1u);
            uint8_t *dst_row = &s_icon_scratch[(oy * (uint32_t)size) * 4u];
            uint32_t ox;

            for (ox = 0; ox < (uint32_t)size; ox++) {
                uint32_t sx = (size <= 1u) ? 0u :
                              (ox * (MONIOS_ICON_SIZE - 1u)) / ((uint32_t)size - 1u);
                const uint8_t *sp =
                    &g_icon_data[icon_id][((sy * MONIOS_ICON_SIZE) + sx) * 4u];
                uint8_t *dp = &dst_row[ox * 4u];

                dp[0] = sp[0];
                dp[1] = sp[1];
                dp[2] = sp[2];
                dp[3] = sp[3];
            }
        }
    }

    return (int32_t) syscall3(SYS_GRAPHICS_BLIT,
                              (uint64_t) s_icon_scratch,
                              packed, 1u);
}

MONIOS_DLL_EXPORT
uint32_t monios_icon_count(void)
{
    return MONIOS_ICON_COUNT;
}
