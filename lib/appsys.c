#include "appsys.h"
#include "futex.h"
#include "ipc.h"
#include "signal.h"
#include "socket.h"
#include "stddef.h"
#include "string.h"
#include "syscall.h"

static const app_launch_info_t *g_launch_info;

const app_launch_info_t *app_launch_info(void)
{
    return g_launch_info;
}

void app_runtime_set_launch_info(const app_launch_info_t *info)
{
    g_launch_info = info;
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

void app_enter_graphics_mode(void)
{
    (void) syscall0(SYS_ENTER_GRAPHICS_MODE);
}

void app_open_cube3d_window(void)
{
    (void) syscall0(SYS_OPEN_CUBE3D_WINDOW);
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

void app_exit(int code)
{
    (void) syscall1(SYS_EXIT_PROCESS, (uint64_t) code);
    for (;;) {
    }
}
