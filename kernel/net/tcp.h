#ifndef _TCP_H_
#define _TCP_H_

#include "stdbool.h"
#include "stdint.h"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

#define TCP_STATE_CLOSED       0
#define TCP_STATE_SYN_SENT     1
#define TCP_STATE_ESTABLISHED  2
#define TCP_STATE_FIN_WAIT_1   3
#define TCP_STATE_FIN_WAIT_2   4
#define TCP_STATE_CLOSE_WAIT   5
#define TCP_STATE_LAST_ACK     6
#define TCP_STATE_TIME_WAIT    7

#define TCP_MAX_CONNECTIONS    4
#define TCP_MAX_SEGMENT_SIZE   1460
#define TCP_WINDOW_SIZE        65535

typedef struct {
    bool used;
    uint8_t state;
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint32_t remote_seq;
    uint32_t remote_ack;
    uint16_t remote_window;
    uint8_t rx_buffer[8192];
    uint32_t rx_len;
    uint32_t rx_read_pos;
    bool rx_ready;
} tcp_connection_t;

void tcp_init(void);
void tcp_handle_ipv4(const uint8_t *packet, uint16_t length);

int32_t tcp_connect(const uint8_t remote_ip[4], uint16_t remote_port, uint16_t local_port);
bool tcp_close(int32_t handle);
int32_t tcp_send(int32_t handle, const uint8_t *data, uint32_t len);
int32_t tcp_recv(int32_t handle, uint8_t *buffer, uint32_t buffer_size);
bool tcp_is_connected(int32_t handle);
bool tcp_has_data(int32_t handle);

#endif
