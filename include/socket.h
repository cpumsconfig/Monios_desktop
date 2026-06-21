#ifndef _SOCKET_H_
#define _SOCKET_H_

#include "stdbool.h"
#include "stdint.h"

#define SOCKET_PROTO_UDP       17
#define SOCKET_MAX_PAYLOAD     1472
#define SOCKET_CALL_UDP_OPEN   1
#define SOCKET_CALL_CLOSE      2
#define SOCKET_CALL_SENDTO     3
#define SOCKET_CALL_RECVFROM   4

typedef struct {
    uint16_t local_port;
    int32_t handle;
} socket_open_request_t;

typedef struct {
    int32_t handle;
    char dst_host[64];
    uint16_t dst_port;
    const uint8_t *payload;
    uint16_t payload_len;
} socket_sendto_request_t;

typedef struct {
    int32_t handle;
    char src_ip[16];
    uint16_t src_port;
    uint8_t *buffer;
    uint16_t buffer_size;
} socket_recvfrom_request_t;

void socket_init(void);
int32_t socket_udp_open(uint16_t local_port);
bool socket_close(int32_t handle);
int32_t socket_sendto_ipv4(int32_t handle, const char *dst_host, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
int32_t socket_recvfrom_ipv4(int32_t handle, char *src_ip_text, uint16_t *src_port, uint8_t *buffer, uint16_t buffer_size);
void socket_handle_udp(const uint8_t src_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
uint32_t socket_count(void);
const char *socket_status(void);

#endif
