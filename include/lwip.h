#ifndef _LWIP_COMPAT_H_
#define _LWIP_COMPAT_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool netif_up;
    bool dns_configured;
    bool dhcp_configured;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t udp_sockets;
    uint32_t tcp_pcbs;
    char ip[16];
    char gateway[16];
    char dns[16];
    char driver[16];
    char status[64];
} lwip_info_t;

void lwip_init(void);
void lwip_update(void);
bool lwip_udp_send(const char *dst_host, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
const lwip_info_t *lwip_info(void);
const char *lwip_status(void);

#endif
