#ifndef _NET_H_
#define _NET_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool connected;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint32_t mmio_base;
    uint32_t io_base;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t ping_requests;
    uint32_t ping_replies;
    bool dhcp_configured;
    char driver[16];
    char mac_text[18];
    char ip_text[16];
    char gateway_text[16];
    char dns_text[16];
    char last_target[64];
} net_info_t;

void net_init(void);
void net_update(void);
bool net_ping(const char *target);
bool net_dhcp_request(void);
const uint8_t *net_local_ip(void);
bool net_get_dns_ip(uint8_t out[4]);
bool net_resolve_ipv4(const char *target, uint8_t out[4]);
bool net_send_ipv4_packet(const uint8_t dst_ip[4], uint8_t proto, const uint8_t *payload, uint16_t payload_len);
bool net_udp_send_to(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
bool net_udp_send(const char *dst_ip_text, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
bool net_connected(void);
const char *net_status(void);
void net_shutdown(void);
const net_info_t *net_info(void);

#endif
