#include "common.h"
#include "ip.h"
#include "kernel.h"
#include "net.h"
#include "socket.h"
#include "tcp.h"

#define SOCKET_MAX_SOCKETS 16

typedef enum {
    SOCKET_TYPE_UDP = 0,
    SOCKET_TYPE_TCP = 1
} socket_type_t;

typedef struct {
    bool used;
    socket_type_t type;
    uint16_t local_port;
    uint8_t rx_src_ip[4];
    uint16_t rx_src_port;
    uint8_t rx_buffer[SOCKET_MAX_PAYLOAD];
    uint16_t rx_len;
    bool rx_ready;
    int32_t tcp_handle;
} socket_entry_t;

static socket_entry_t g_sockets[SOCKET_MAX_SOCKETS];
static char g_socket_status[64];
static uint16_t g_next_ephemeral_port;

static socket_entry_t *socket_from_handle(int32_t handle)
{
    if (handle <= 0 || handle > SOCKET_MAX_SOCKETS) {
        return NULL;
    }
    if (!g_sockets[handle - 1].used) {
        return NULL;
    }
    return &g_sockets[handle - 1];
}

static bool socket_port_in_use(uint16_t port)
{
    for (uint32_t i = 0; i < SOCKET_MAX_SOCKETS; i++) {
        if (g_sockets[i].used && g_sockets[i].local_port == port) {
            return true;
        }
    }
    return false;
}

static int32_t socket_alloc_handle(void)
{
    for (uint32_t i = 0; i < SOCKET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].used) {
            return (int32_t) (i + 1);
        }
    }
    return -1;
}

void socket_init(void)
{
    memset(g_sockets, 0, sizeof(g_sockets));
    g_next_ephemeral_port = 49152;
    strcpy(g_socket_status, "socket: ready");
}

int32_t socket_udp_open(uint16_t local_port)
{
    int32_t handle;

    if (local_port == 0) {
        for (uint32_t i = 0; i < 1000; i++) {
            uint16_t candidate = g_next_ephemeral_port++;
            if (g_next_ephemeral_port < 49152 || g_next_ephemeral_port > 65535) {
                g_next_ephemeral_port = 49152;
            }
            if (!socket_port_in_use(candidate)) {
                local_port = candidate;
                break;
            }
        }
    }
    if (local_port == 0 || socket_port_in_use(local_port)) {
        strcpy(g_socket_status, "socket: port busy");
        return -1;
    }

    handle = socket_alloc_handle();
    if (handle < 0) {
        strcpy(g_socket_status, "socket: table full");
        return -1;
    }

    memset(&g_sockets[handle - 1], 0, sizeof(g_sockets[handle - 1]));
    g_sockets[handle - 1].used = true;
    g_sockets[handle - 1].type = SOCKET_TYPE_UDP;
    g_sockets[handle - 1].local_port = local_port;

    strcpy(g_socket_status, "socket: udp open");
    return handle;
}

int32_t socket_tcp_open(uint16_t local_port)
{
    int32_t handle;

    if (local_port == 0) {
        for (uint32_t i = 0; i < 1000; i++) {
            uint16_t candidate = g_next_ephemeral_port++;
            if (g_next_ephemeral_port < 49152 || g_next_ephemeral_port > 65535) {
                g_next_ephemeral_port = 49152;
            }
            if (!socket_port_in_use(candidate)) {
                local_port = candidate;
                break;
            }
        }
    }
    if (local_port == 0 || socket_port_in_use(local_port)) {
        strcpy(g_socket_status, "socket: port busy");
        return -1;
    }

    handle = socket_alloc_handle();
    if (handle < 0) {
        strcpy(g_socket_status, "socket: table full");
        return -1;
    }

    memset(&g_sockets[handle - 1], 0, sizeof(g_sockets[handle - 1]));
    g_sockets[handle - 1].used = true;
    g_sockets[handle - 1].type = SOCKET_TYPE_TCP;
    g_sockets[handle - 1].local_port = local_port;
    g_sockets[handle - 1].tcp_handle = -1;

    strcpy(g_socket_status, "socket: tcp open");
    return handle;
}

bool socket_tcp_connect(int32_t handle, const char *dst_host, uint16_t dst_port)
{
    socket_entry_t *sock = socket_from_handle(handle);
    uint8_t dst_ip[4];
    int32_t tcp_handle;

    if (sock == NULL || sock->type != SOCKET_TYPE_TCP) {
        strcpy(g_socket_status, "socket: bad handle");
        return false;
    }

    if (dst_host == NULL || dst_port == 0) {
        strcpy(g_socket_status, "socket: bad connect");
        return false;
    }

    if (!net_resolve_ipv4(dst_host, dst_ip)) {
        strcpy(g_socket_status, "socket: resolve failed");
        return false;
    }

    tcp_handle = tcp_connect(dst_ip, dst_port, sock->local_port);
    if (tcp_handle < 0) {
        strcpy(g_socket_status, "socket: connect failed");
        return false;
    }

    sock->tcp_handle = tcp_handle;
    strcpy(g_socket_status, "socket: tcp connecting");
    return true;
}

int32_t socket_tcp_send(int32_t handle, const uint8_t *data, uint16_t len)
{
    socket_entry_t *sock = socket_from_handle(handle);

    if (sock == NULL || sock->type != SOCKET_TYPE_TCP) {
        strcpy(g_socket_status, "socket: bad handle");
        return -1;
    }

    if (sock->tcp_handle < 0) {
        strcpy(g_socket_status, "socket: not connected");
        return -1;
    }

    int32_t result = tcp_send(sock->tcp_handle, data, len);
    if (result < 0) {
        strcpy(g_socket_status, "socket: send failed");
    } else {
        strcpy(g_socket_status, "socket: tcp sent");
    }
    return result;
}

int32_t socket_tcp_recv(int32_t handle, uint8_t *buffer, uint16_t buffer_size)
{
    socket_entry_t *sock = socket_from_handle(handle);

    if (sock == NULL || sock->type != SOCKET_TYPE_TCP) {
        strcpy(g_socket_status, "socket: bad handle");
        return -1;
    }

    if (sock->tcp_handle < 0) {
        strcpy(g_socket_status, "socket: not connected");
        return -1;
    }

    int32_t result = tcp_recv(sock->tcp_handle, buffer, buffer_size);
    if (result < 0) {
        strcpy(g_socket_status, "socket: recv failed");
    } else if (result > 0) {
        strcpy(g_socket_status, "socket: tcp received");
    } else {
        strcpy(g_socket_status, "socket: no data");
    }
    return result;
}

bool socket_tcp_is_connected(int32_t handle)
{
    socket_entry_t *sock = socket_from_handle(handle);

    if (sock == NULL || sock->type != SOCKET_TYPE_TCP) {
        return false;
    }

    if (sock->tcp_handle < 0) {
        return false;
    }

    return tcp_is_connected(sock->tcp_handle);
}

bool socket_tcp_has_data(int32_t handle)
{
    socket_entry_t *sock = socket_from_handle(handle);

    if (sock == NULL || sock->type != SOCKET_TYPE_TCP) {
        return false;
    }

    if (sock->tcp_handle < 0) {
        return false;
    }

    return tcp_has_data(sock->tcp_handle);
}

bool socket_close(int32_t handle)
{
    socket_entry_t *sock = socket_from_handle(handle);

    if (sock == NULL) {
        strcpy(g_socket_status, "socket: bad handle");
        return false;
    }

    if (sock->type == SOCKET_TYPE_TCP && sock->tcp_handle >= 0) {
        tcp_close(sock->tcp_handle);
    }

    memset(sock, 0, sizeof(*sock));
    strcpy(g_socket_status, "socket: closed");
    return true;
}

int32_t socket_sendto_ipv4(int32_t handle, const char *dst_host, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    socket_entry_t *sock = socket_from_handle(handle);
    uint8_t dst_ip[4];

    if (sock == NULL || sock->type != SOCKET_TYPE_UDP) {
        strcpy(g_socket_status, "socket: bad handle");
        return -1;
    }
    if (dst_port == 0 || payload == NULL || payload_len > SOCKET_MAX_PAYLOAD) {
        strcpy(g_socket_status, "socket: bad send");
        return -1;
    }
    if (!net_resolve_ipv4(dst_host, dst_ip)) {
        strcpy(g_socket_status, "socket: resolve failed");
        return -1;
    }
    if (!net_udp_send_to(dst_ip, sock->local_port, dst_port, payload, payload_len)) {
        strcpy(g_socket_status, "socket: send failed");
        return -1;
    }
    strcpy(g_socket_status, "socket: sent");
    return payload_len;
}

int32_t socket_recvfrom_ipv4(int32_t handle, char *src_ip_text, uint16_t *src_port, uint8_t *buffer, uint16_t buffer_size)
{
    socket_entry_t *sock = socket_from_handle(handle);
    uint16_t copy_len;

    if (sock == NULL || sock->type != SOCKET_TYPE_UDP || buffer == NULL) {
        strcpy(g_socket_status, "socket: bad recv");
        return -1;
    }
    if (!sock->rx_ready) {
        strcpy(g_socket_status, "socket: no data");
        return 0;
    }
    copy_len = sock->rx_len;
    if (copy_len > buffer_size) {
        copy_len = buffer_size;
    }
    memcpy(buffer, sock->rx_buffer, copy_len);
    if (src_ip_text != NULL) {
        ip_to_text(sock->rx_src_ip, src_ip_text);
    }
    if (src_port != NULL) {
        *src_port = sock->rx_src_port;
    }
    sock->rx_ready = false;
    strcpy(g_socket_status, "socket: received");
    return copy_len;
}

void socket_handle_udp(const uint8_t src_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    if (payload == NULL || payload_len > SOCKET_MAX_PAYLOAD) {
        return;
    }
    for (uint32_t i = 0; i < SOCKET_MAX_SOCKETS; i++) {
        if (g_sockets[i].used && g_sockets[i].type == SOCKET_TYPE_UDP && g_sockets[i].local_port == dst_port) {
            memcpy(g_sockets[i].rx_src_ip, src_ip, 4);
            g_sockets[i].rx_src_port = src_port;
            memcpy(g_sockets[i].rx_buffer, payload, payload_len);
            g_sockets[i].rx_len = payload_len;
            g_sockets[i].rx_ready = true;
            strcpy(g_socket_status, "socket: packet queued");
            return;
        }
    }
}

uint32_t socket_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < SOCKET_MAX_SOCKETS; i++) {
        if (g_sockets[i].used) {
            count++;
        }
    }
    return count;
}

const char *socket_status(void)
{
    return g_socket_status;
}
