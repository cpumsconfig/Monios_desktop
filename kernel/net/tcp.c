#include "common.h"
#include "cpu.h"
#include "ip.h"
#include "kernel.h"
#include "net.h"
#include "tcp.h"

static tcp_connection_t g_connections[TCP_MAX_CONNECTIONS];
static uint32_t g_tcp_packets;
static uint16_t g_next_ephemeral_port;
static uint32_t g_seq_secret;

/* Passive-open listeners: a SYN arriving on one of these ports creates a new
 * server-side connection that later shows up in tcp_accept_ready(). */
typedef struct {
    bool used;
    uint16_t local_port;
} tcp_listener_t;

static tcp_listener_t g_listeners[TCP_MAX_LISTENERS];

static bool tcp_port_has_listener(uint16_t port)
{
    for (uint32_t i = 0; i < TCP_MAX_LISTENERS; i++) {
        if (g_listeners[i].used && g_listeners[i].local_port == port) {
            return true;
        }
    }
    return false;
}

static uint32_t tcp_mix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

static bool tcp_seq_between(uint32_t value, uint32_t start, uint32_t end)
{
    return (int32_t) (value - start) >= 0 && (int32_t) (end - value) >= 0;
}

static bool tcp_ack_valid(const tcp_connection_t *conn, uint32_t ack)
{
    if (conn == NULL) {
        return false;
    }
    return tcp_seq_between(ack, conn->remote_ack, conn->seq_num);
}

static uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], const uint8_t *segment, uint16_t length)
{
    uint32_t sum = 0;
    uint16_t i;

    sum += ip_get16(src_ip);
    sum += ip_get16(src_ip + 2);
    sum += ip_get16(dst_ip);
    sum += ip_get16(dst_ip + 2);
    sum += 0x0006;
    sum += length;

    for (i = 0; i + 1 < length; i += 2) {
        sum += ip_get16(segment + i);
    }
    if (i < length) {
        sum += (uint16_t) segment[i] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

static bool tcp_send_segment(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port, 
                              uint32_t seq, uint32_t ack, uint8_t flags, 
                              const uint8_t *data, uint16_t data_len)
{
    uint8_t tcp[20 + TCP_MAX_SEGMENT_SIZE];
    uint16_t total_len;
    const uint8_t *src_ip = net_local_ip();
    uint8_t data_offset = 5;

    if (src_ip == NULL) {
        return false;
    }

    total_len = data_offset * 4 + data_len;
    if (total_len > sizeof(tcp)) {
        return false;
    }

    memset(tcp, 0, sizeof(tcp));
    tcp[0] = (uint8_t) (src_port >> 8);
    tcp[1] = (uint8_t) src_port;
    tcp[2] = (uint8_t) (dst_port >> 8);
    tcp[3] = (uint8_t) dst_port;
    tcp[4] = (uint8_t) (seq >> 24);
    tcp[5] = (uint8_t) (seq >> 16);
    tcp[6] = (uint8_t) (seq >> 8);
    tcp[7] = (uint8_t) seq;
    tcp[8] = (uint8_t) (ack >> 24);
    tcp[9] = (uint8_t) (ack >> 16);
    tcp[10] = (uint8_t) (ack >> 8);
    tcp[11] = (uint8_t) ack;
    tcp[12] = (uint8_t) (data_offset << 4);
    tcp[13] = flags;
    tcp[14] = (uint8_t) (TCP_WINDOW_SIZE >> 8);
    tcp[15] = (uint8_t) TCP_WINDOW_SIZE;
    tcp[16] = 0;
    tcp[17] = 0;
    tcp[18] = 0;
    tcp[19] = 0;

    if (data != NULL && data_len > 0) {
        memcpy(tcp + data_offset * 4, data, data_len);
    }

    ip_put16(tcp + 16, tcp_checksum(src_ip, dst_ip, tcp, total_len));
    return net_send_ipv4_packet(dst_ip, 6, tcp, total_len);
}

static tcp_connection_t *tcp_get_connection(int32_t handle)
{
    if (handle <= 0 || handle > TCP_MAX_CONNECTIONS) {
        return NULL;
    }
    if (!g_connections[handle - 1].used) {
        return NULL;
    }
    return &g_connections[handle - 1];
}

static bool tcp_port_in_use(uint16_t port)
{
    for (uint32_t i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (g_connections[i].used && g_connections[i].local_port == port) {
            return true;
        }
    }
    return false;
}

static uint16_t tcp_alloc_ephemeral_port(void)
{
    for (uint32_t i = 0; i < 1000; i++) {
        uint16_t port = g_next_ephemeral_port++;
        if (g_next_ephemeral_port < 49152) {
            g_next_ephemeral_port = 49152;
        }
        if (!tcp_port_in_use(port)) {
            return port;
        }
    }
    return 0;
}

static uint32_t tcp_generate_seq(void)
{
    uint64_t tsc = cpu_read_tsc();

    g_seq_secret ^= (uint32_t) tsc;
    g_seq_secret ^= (uint32_t) (tsc >> 32);
    g_seq_secret = tcp_mix32(g_seq_secret + 0x9E3779B9u);
    return g_seq_secret;
}

static uint32_t tcp_generate_conn_seq(const uint8_t remote_ip[4], uint16_t remote_port, uint16_t local_port)
{
    uint32_t seq = tcp_generate_seq();

    seq ^= ((uint32_t) remote_ip[0] << 24) |
           ((uint32_t) remote_ip[1] << 16) |
           ((uint32_t) remote_ip[2] << 8) |
           remote_ip[3];
    seq ^= ((uint32_t) remote_port << 16) | local_port;
    seq ^= (uint32_t) timer_ticks();
    return seq;
}

void tcp_init(void)
{
    memset(g_connections, 0, sizeof(g_connections));
    g_tcp_packets = 0;
    g_next_ephemeral_port = 49152;
    {
        uint64_t tsc = cpu_read_tsc();

        g_seq_secret = tcp_mix32((uint32_t) timer_ticks() ^
                                 (uint32_t) tsc ^
                                 (uint32_t) (tsc >> 32) ^
                                 0xA5366B4Du);
    }
    log_write("tcp: ipv4 stack ready");
}

int32_t tcp_listen_port(uint16_t local_port)
{
    if (local_port == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < TCP_MAX_LISTENERS; i++) {
        if (!g_listeners[i].used) {
            g_listeners[i].used = true;
            g_listeners[i].local_port = local_port;
            log_write("tcp: listening port");
            return 0;
        }
    }
    return -1;
}

void tcp_unlisten_port(uint16_t local_port)
{
    for (uint32_t i = 0; i < TCP_MAX_LISTENERS; i++) {
        if (g_listeners[i].used && g_listeners[i].local_port == local_port) {
            g_listeners[i].used = false;
            g_listeners[i].local_port = 0;
            return;
        }
    }
}

int32_t tcp_accept_ready(uint16_t local_port, uint8_t remote_ip_out[4], uint16_t *remote_port_out)
{
    for (uint32_t i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        tcp_connection_t *conn = &g_connections[i];

        if (!conn->used || !conn->passive || conn->accepted) {
            continue;
        }
        if (conn->local_port != local_port) {
            continue;
        }
        if (conn->state != TCP_STATE_ESTABLISHED) {
            continue;
        }
        conn->accepted = true;
        if (remote_ip_out != NULL) {
            memcpy(remote_ip_out, conn->remote_ip, 4);
        }
        if (remote_port_out != NULL) {
            *remote_port_out = conn->remote_port;
        }
        return (int32_t) (i + 1);
    }
    return -1;
}

/* Create a server-side connection entry in response to an inbound SYN and
 * answer SYN/ACK. The connection reaches ESTABLISHED once the client ACKs. */
static tcp_connection_t *tcp_passive_open(const uint8_t remote_ip[4], uint16_t remote_port,
                                           uint16_t local_port, uint32_t client_seq)
{
    int32_t handle = -1;
    tcp_connection_t *conn = NULL;

    for (uint32_t i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!g_connections[i].used) {
            handle = (int32_t) (i + 1);
            conn = &g_connections[i];
            break;
        }
    }
    if (handle < 0) {
        log_write("tcp: listener backlog full");
        return NULL;
    }

    memset(conn, 0, sizeof(*conn));
    conn->used = true;
    conn->passive = true;
    conn->accepted = false;
    conn->state = TCP_STATE_SYN_RCVD;
    memcpy(conn->remote_ip, remote_ip, 4);
    conn->remote_port = remote_port;
    conn->local_port = local_port;
    conn->seq_num = tcp_generate_conn_seq(remote_ip, remote_port, local_port);
    conn->remote_seq = client_seq;
    conn->ack_num = client_seq + 1;
    conn->remote_ack = 0;

    /* Send SYN/ACK: our seq = conn->seq_num, ack = client_seq+1. */
    if (!tcp_send_segment(remote_ip, local_port, remote_port,
                          conn->seq_num, conn->ack_num,
                          TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0)) {
        memset(conn, 0, sizeof(*conn));
        return NULL;
    }
    conn->seq_num++; /* SYN consumes one sequence number */
    log_write("tcp: inbound syn");
    return conn;
}

int32_t tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port, uint16_t local_port)
{
    tcp_connection_t *conn;
    int32_t handle = -1;

    if (remote_ip == NULL || remote_port == 0) {
        return -1;
    }

    if (local_port == 0) {
        local_port = tcp_alloc_ephemeral_port();
        if (local_port == 0) {
            return -1;
        }
    } else if (tcp_port_in_use(local_port)) {
        return -1;
    }

    for (uint32_t i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (!g_connections[i].used) {
            handle = (int32_t) (i + 1);
            conn = &g_connections[i];
            break;
        }
    }

    if (handle < 0) {
        return -1;
    }

    memset(conn, 0, sizeof(*conn));
    conn->used = true;
    conn->state = TCP_STATE_SYN_SENT;
    memcpy(conn->remote_ip, remote_ip, 4);
    conn->remote_port = remote_port;
    conn->local_port = local_port;
    conn->seq_num = tcp_generate_conn_seq(remote_ip, remote_port, local_port);
    conn->ack_num = 0;
    conn->rx_len = 0;
    conn->rx_read_pos = 0;
    conn->rx_ready = false;

    if (!tcp_send_segment(remote_ip, local_port, remote_port, conn->seq_num, 0, TCP_FLAG_SYN, NULL, 0)) {
        conn->used = false;
        return -1;
    }

    conn->seq_num++;

    return handle;
}

bool tcp_close(int32_t handle)
{
    tcp_connection_t *conn = tcp_get_connection(handle);

    if (conn == NULL) {
        return false;
    }

    if (conn->state == TCP_STATE_ESTABLISHED) {
        conn->state = TCP_STATE_FIN_WAIT_1;
        tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port, 
                         conn->seq_num, conn->ack_num, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        conn->seq_num++;
    } else if (conn->state == TCP_STATE_CLOSE_WAIT) {
        conn->state = TCP_STATE_LAST_ACK;
        tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port, 
                         conn->seq_num, conn->ack_num, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        conn->seq_num++;
    } else {
        memset(conn, 0, sizeof(*conn));
    }

    return true;
}

int32_t tcp_send(int32_t handle, const uint8_t *data, uint32_t len)
{
    tcp_connection_t *conn = tcp_get_connection(handle);
    uint32_t offset = 0;

    if (conn == NULL || data == NULL || len == 0) {
        return -1;
    }

    if (conn->state != TCP_STATE_ESTABLISHED) {
        return -1;
    }

    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > TCP_MAX_SEGMENT_SIZE) {
            chunk = TCP_MAX_SEGMENT_SIZE;
        }

        if (!tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                              conn->seq_num, conn->ack_num, TCP_FLAG_PSH | TCP_FLAG_ACK,
                              data + offset, (uint16_t) chunk)) {
            if (offset == 0) {
                return -1;
            }
            break;
        }

        conn->seq_num += chunk;
        offset += chunk;
    }

    return (int32_t) offset;
}

int32_t tcp_recv(int32_t handle, uint8_t *buffer, uint32_t buffer_size)
{
    tcp_connection_t *conn = tcp_get_connection(handle);
    uint32_t available;

    if (conn == NULL || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    if (conn->rx_read_pos >= conn->rx_len) {
        return 0;
    }

    available = conn->rx_len - conn->rx_read_pos;
    if (available > buffer_size) {
        available = buffer_size;
    }

    memcpy(buffer, conn->rx_buffer + conn->rx_read_pos, available);
    conn->rx_read_pos += available;

    if (conn->rx_read_pos >= conn->rx_len) {
        conn->rx_len = 0;
        conn->rx_read_pos = 0;
        conn->rx_ready = false;
    }

    return (int32_t) available;
}

bool tcp_is_connected(int32_t handle)
{
    tcp_connection_t *conn = tcp_get_connection(handle);

    if (conn == NULL) {
        return false;
    }

    return conn->state == TCP_STATE_ESTABLISHED;
}

bool tcp_has_data(int32_t handle)
{
    tcp_connection_t *conn = tcp_get_connection(handle);

    if (conn == NULL) {
        return false;
    }

    return conn->rx_ready && conn->rx_read_pos < conn->rx_len;
}

void tcp_handle_ipv4(const uint8_t *packet, uint16_t length)
{
    const uint8_t *ip;
    const uint8_t *tcp;
    uint16_t ip_total;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t flags;
    uint16_t window;
    uint8_t data_offset;
    uint16_t data_len;
    const uint8_t *data;
    uint8_t src_ip[4];
    tcp_connection_t *conn = NULL;

    if (packet == NULL || length < 54) {
        return;
    }
    ip = packet + 14;
    if ((ip[0] >> 4) != 4 || (ip[0] & 0x0F) != 5 || ip[9] != 6) {
        return;
    }
    ip_total = ip_get16(ip + 2);
    if (ip_total < 40 || (uint32_t) ip_total + 14 > length) {
        return;
    }
    if (ip_checksum(ip, 20) != 0 || (ip_get16(ip + 6) & 0x3FFFu) != 0) {
        return;
    }
    if (!ip_equal(ip + 16, net_local_ip())) {
        return;
    }

    tcp = ip + 20;
    if (tcp_checksum(ip + 12, ip + 16, tcp, (uint16_t) (ip_total - 20)) != 0) {
        return;
    }
    src_port = ip_get16(tcp + 0);
    dst_port = ip_get16(tcp + 2);
    seq = ((uint32_t) tcp[4] << 24) | ((uint32_t) tcp[5] << 16) | ((uint32_t) tcp[6] << 8) | tcp[7];
    ack = ((uint32_t) tcp[8] << 24) | ((uint32_t) tcp[9] << 16) | ((uint32_t) tcp[10] << 8) | tcp[11];
    data_offset = (tcp[12] >> 4) & 0x0F;
    if (data_offset < 5 || (uint32_t) data_offset * 4u > (uint32_t) ip_total - 20u) {
        return;
    }
    flags = tcp[13];
    window = ip_get16(tcp + 14);
    data = tcp + data_offset * 4;
    data_len = ip_total - 20 - data_offset * 4;

    memcpy(src_ip, ip + 12, 4);

    g_tcp_packets++;

    for (uint32_t i = 0; i < TCP_MAX_CONNECTIONS; i++) {
        if (g_connections[i].used &&
            g_connections[i].local_port == dst_port &&
            g_connections[i].remote_port == src_port &&
            ip_equal(g_connections[i].remote_ip, src_ip)) {
            conn = &g_connections[i];
            break;
        }
    }

    if (conn == NULL) {
        if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) == 0) {
            if (tcp_port_has_listener(dst_port)) {
                tcp_passive_open(src_ip, src_port, dst_port, seq);
            } else {
                uint8_t rst_flags = TCP_FLAG_RST | TCP_FLAG_ACK;
                tcp_send_segment(src_ip, dst_port, src_port, 0, seq + 1, rst_flags, NULL, 0);
                log_write("tcp: rst sent");
            }
        }
        return;
    }

    switch (conn->state) {
        case TCP_STATE_SYN_SENT:
            if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) != 0 && ack == conn->seq_num) {
                conn->state = TCP_STATE_ESTABLISHED;
                conn->ack_num = seq + 1;
                conn->remote_seq = seq + 1;
                conn->remote_ack = ack;
                conn->remote_window = window;
                tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                 conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                log_write("tcp: connection established");
            } else if ((flags & TCP_FLAG_RST) != 0 && tcp_ack_valid(conn, ack)) {
                memset(conn, 0, sizeof(*conn));
                log_write("tcp: connection reset");
            }
            break;

        case TCP_STATE_SYN_RCVD:
            /* Client ACKs our SYN/ACK -> established. A stray RST tears us down. */
            if ((flags & TCP_FLAG_RST) != 0) {
                memset(conn, 0, sizeof(*conn));
                log_write("tcp: syn rcv reset");
                break;
            }
            if ((flags & TCP_FLAG_ACK) != 0 && ack == conn->seq_num && seq == conn->ack_num) {
                conn->state = TCP_STATE_ESTABLISHED;
                conn->remote_ack = ack;
                conn->remote_window = window;
                log_write("tcp: inbound connection established");
            }
            break;

        case TCP_STATE_ESTABLISHED:
            if ((flags & TCP_FLAG_RST) != 0) {
                if (seq == conn->ack_num || tcp_ack_valid(conn, ack)) {
                    memset(conn, 0, sizeof(*conn));
                    log_write("tcp: connection reset");
                }
                break;
            }

            if ((flags & TCP_FLAG_ACK) != 0 && !tcp_ack_valid(conn, ack)) {
                break;
            }
            if (seq != conn->ack_num) {
                tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                 conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                break;
            }

            if ((flags & TCP_FLAG_FIN) != 0) {
                conn->state = TCP_STATE_CLOSE_WAIT;
                conn->ack_num = seq + 1;
                conn->remote_seq = conn->ack_num;
                tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                 conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                log_write("tcp: close wait");
                break;
            }

            if (data_len > 0) {
                uint32_t space = sizeof(conn->rx_buffer) - conn->rx_len;
                if (data_len <= space) {
                    memcpy(conn->rx_buffer + conn->rx_len, data, data_len);
                    conn->rx_len += data_len;
                    conn->rx_ready = true;
                    conn->ack_num = seq + data_len;
                    conn->remote_seq = conn->ack_num;
                    tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                     conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                }
            }
            break;

        case TCP_STATE_FIN_WAIT_1:
            if ((flags & TCP_FLAG_ACK) != 0 && ack == conn->seq_num) {
                conn->state = TCP_STATE_FIN_WAIT_2;
            }
            if ((flags & TCP_FLAG_FIN) != 0 && seq == conn->ack_num) {
                conn->ack_num = seq + 1;
                tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                 conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_TIME_WAIT;
                memset(conn, 0, sizeof(*conn));
                log_write("tcp: connection closed");
            }
            break;

        case TCP_STATE_FIN_WAIT_2:
            if ((flags & TCP_FLAG_FIN) != 0 && seq == conn->ack_num) {
                conn->ack_num = seq + 1;
                tcp_send_segment(conn->remote_ip, conn->local_port, conn->remote_port,
                                 conn->seq_num, conn->ack_num, TCP_FLAG_ACK, NULL, 0);
                conn->state = TCP_STATE_TIME_WAIT;
                memset(conn, 0, sizeof(*conn));
                log_write("tcp: connection closed");
            }
            break;

        case TCP_STATE_CLOSE_WAIT:
            break;

        case TCP_STATE_LAST_ACK:
            if ((flags & TCP_FLAG_ACK) != 0 && ack == conn->seq_num) {
                memset(conn, 0, sizeof(*conn));
                log_write("tcp: connection closed");
            }
            break;

        default:
            break;
    }
}
