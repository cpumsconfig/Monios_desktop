#ifndef _NET_H_
#define _NET_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool connected;
    bool onboard;
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
    char netmask_text[16];
    char last_target[64];
} net_info_t;

void net_init(void);
void net_update(void);
bool net_ping(const char *target);
bool net_dhcp_request(void);
bool net_configure_static(const char *ip, const char *mask, const char *gateway, const char *dns);
uint32_t net_arp_table(char *buffer, uint32_t buffer_size);
void net_route_summary(char *buffer, uint32_t buffer_size);
const uint8_t *net_local_ip(void);
bool net_get_dns_ip(uint8_t out[4]);
uint32_t net_dns_server_count(void);
bool net_get_dns_server(uint32_t index, uint8_t out[4]);
bool net_resolve_ipv4(const char *target, uint8_t out[4]);
bool net_send_ipv4_packet(const uint8_t dst_ip[4], uint8_t proto, const uint8_t *payload, uint16_t payload_len);
bool net_udp_send_to(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
bool net_udp_send(const char *dst_ip_text, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len);
bool net_connected(void);
const char *net_status(void);
void net_shutdown(void);
const net_info_t *net_info(void);

/* ============================================================
 *  防火墙钩子注册（security group）
 *
 *  入站/出站判定回调，签名与 firewall.h 中 firewall_check_inbound/
 *  outbound 一致：返回 true=放行，false=拦截。
 *  传入 NULL 表示清除钩子（恢复全部放行）。
 *  未注册任何钩子前，net.c 的收/发路径一律放行（宽容模式）。
 * ============================================================ */
typedef bool (*net_firewall_check_fn)(uint8_t protocol,
                                      const uint8_t src_ip[4],
                                      const uint8_t dst_ip[4],
                                      uint16_t src_port, uint16_t dst_port);

void net_register_firewall_hooks(net_firewall_check_fn inbound,
                                 net_firewall_check_fn outbound);

#endif
