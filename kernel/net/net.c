#include "common.h"
#include "dns.h"
#include "e1000.h"
#include "ip.h"
#include "ipv4.h"
#include "kernel.h"
#include "net.h"
#include "pci.h"
#include "pcnet.h"
#include "socket.h"
#include "tcp.h"

#define PCI_CLASS_NETWORK          0x02
#define PCI_SUBCLASS_ETHERNET      0x00

#define NET_ETH_TYPE_ARP           0x0806
#define NET_ETH_TYPE_IPV4          0x0800
#define NET_IP_PROTO_ICMP          1
#define NET_IP_PROTO_TCP           6
#define NET_IP_PROTO_UDP           17
#define NET_ICMP_ECHO_REPLY        0
#define NET_ICMP_ECHO_REQUEST      8
#define NET_ICMP_ID                0x4D4F
#define NET_DHCP_CLIENT_PORT       68
#define NET_DHCP_SERVER_PORT       67
#define NET_DHCP_MAGIC_COOKIE      0x63825363U
#define NET_DHCP_DISCOVER          1
#define NET_DHCP_OFFER             2
#define NET_DHCP_REQUEST           3
#define NET_DHCP_ACK               5

static net_info_t g_net_info;
static char g_net_status[64];
static uint8_t g_mac[6];
static uint8_t g_local_ip[4] = { 192, 168, 58, 120 };
static uint8_t g_gateway_ip[4] = { 192, 168, 58, 2 };
static uint8_t g_host_ip[4] = { 192, 168, 58, 1 };
static uint8_t g_netmask[4] = { 255, 255, 255, 0 };
static uint8_t g_dns_ip[4] = { 0, 0, 0, 0 };
static uint16_t g_ping_seq;
static bool g_ping_waiting;
static uint8_t g_ping_target_ip[4];
#define ARP_CACHE_SIZE 8
static uint8_t g_arp_ip[ARP_CACHE_SIZE][4];
static uint8_t g_arp_mac[ARP_CACHE_SIZE][6];
static bool g_arp_valid[ARP_CACHE_SIZE];
static uint32_t g_arp_next_slot;
static bool g_net_backend_ready;
static bool g_rx_started;
static bool g_dhcp_waiting;
static uint8_t g_dhcp_expected_type;
static uint32_t g_dhcp_xid;
static uint8_t g_dhcp_offered_ip[4];
static uint8_t g_dhcp_server_ip[4];
static uint8_t g_dhcp_offer_mask[4];
static uint8_t g_dhcp_offer_gateway[4];
static uint8_t g_dhcp_offer_dns[4];
static bool g_dhcp_offer_valid;

static bool net_mmio_identity_mapped(uint32_t mmio)
{
    return mmio < 0x40000000u || mmio >= 0xC0000000u;
}

static void net_refresh_ip_texts(void)
{
    ip_to_text(g_local_ip, g_net_info.ip_text);
    ip_to_text(g_gateway_ip, g_net_info.gateway_text);
    ip_to_text(g_dns_ip, g_net_info.dns_text);
}

/* forward declarations */
static bool net_send_icmp_echo(const uint8_t dst_mac[6], const uint8_t dst_ip[4], uint16_t sequence);

static void net_hex_byte(char *out, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    out[0] = hex[(value >> 4) & 0xF];
    out[1] = hex[value & 0xF];
}

static void net_set_mac_text(const uint8_t mac[6])
{
    for (uint32_t i = 0; i < 6; i++) {
        net_hex_byte(&g_net_info.mac_text[i * 3], mac[i]);
        if (i < 5) {
            g_net_info.mac_text[i * 3 + 2] = ':';
        }
    }
    g_net_info.mac_text[17] = '\0';
}

static bool net_is_loopback_ip(const uint8_t ip[4])
{
    return ip != NULL && ip[0] == 127;
}

static bool net_is_local_ip(const uint8_t ip[4])
{
    return ip != NULL && (ip_equal(ip, g_local_ip) || net_is_loopback_ip(ip));
}

static void net_set_last_target(const char *target)
{
    memset(g_net_info.last_target, 0, sizeof(g_net_info.last_target));
    if (target != NULL && strlen(target) < sizeof(g_net_info.last_target)) {
        strcpy(g_net_info.last_target, target);
    } else {
        strcpy(g_net_info.last_target, "target-too-long");
    }
}

static bool net_ping_local_reply(const uint8_t target_ip[4], const char *target)
{
    char ip_text[16];

    net_set_last_target(target);
    g_net_info.ping_requests++;
    g_net_info.ping_replies++;
    g_ping_waiting = false;
    ip_to_text(target_ip, ip_text);
    strcpy(g_net_status, "net: local ping ");
    strcpy(g_net_status + strlen(g_net_status), ip_text);
    log_write(g_net_status);
    strcpy(g_net_status, "net: ping reply from ");
    strcpy(g_net_status + strlen(g_net_status), ip_text);
    log_write(g_net_status);
    return true;
}

static void net_write_ether_header(uint8_t *packet, const uint8_t dst[6], uint16_t ether_type)
{
    memcpy(packet, dst, 6);
    memcpy(packet + 6, g_mac, 6);
    ip_put16(packet + 12, ether_type);
}

static bool net_send_frame(const uint8_t *packet, uint16_t length)
{
    if (!e1000_send_frame(packet, length)) {
        strcpy(g_net_status, "net: tx failed");
        log_write(g_net_status);
        e1000_debug_state("net tx failed");
        return false;
    }
    log_write("net: tx submitted");
    return true;
}

static bool net_send_arp_request(const uint8_t target_ip[4])
{
    uint8_t packet[60];
    static const uint8_t broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, broadcast, NET_ETH_TYPE_ARP);
    ip_put16(packet + 14, 1);
    ip_put16(packet + 16, NET_ETH_TYPE_IPV4);
    packet[18] = 6;
    packet[19] = 4;
    ip_put16(packet + 20, 1);
    memcpy(packet + 22, g_mac, 6);
    memcpy(packet + 28, g_local_ip, 4);
    memset(packet + 32, 0, 6);
    memcpy(packet + 38, target_ip, 4);
    {
        char ip_text[16];
        strcpy(g_net_status, "net: arp req ");
        ip_to_text(target_ip, ip_text);
        strcpy(g_net_status + strlen(g_net_status), ip_text);
        log_write(g_net_status);
    }
    return net_send_frame(packet, sizeof(packet));
}

static bool net_send_arp_reply(const uint8_t dst_mac[6], const uint8_t dst_ip[4])
{
    uint8_t packet[60];

    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, dst_mac, NET_ETH_TYPE_ARP);
    ip_put16(packet + 14, 1);
    ip_put16(packet + 16, NET_ETH_TYPE_IPV4);
    packet[18] = 6;
    packet[19] = 4;
    ip_put16(packet + 20, 2);
    memcpy(packet + 22, g_mac, 6);
    memcpy(packet + 28, g_local_ip, 4);
    memcpy(packet + 32, dst_mac, 6);
    memcpy(packet + 38, dst_ip, 4);
    {
        char ip_text[16];
        strcpy(g_net_status, "net: arp reply ");
        ip_to_text(dst_ip, ip_text);
        strcpy(g_net_status + strlen(g_net_status), ip_text);
        log_write(g_net_status);
    }
    return net_send_frame(packet, sizeof(packet));
}

static uint16_t net_udp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], const uint8_t *udp, uint16_t length)
{
    uint32_t sum = 0;
    uint16_t i;

    sum += ip_get16(src_ip);
    sum += ip_get16(src_ip + 2);
    sum += ip_get16(dst_ip);
    sum += ip_get16(dst_ip + 2);
    sum += NET_IP_PROTO_UDP;
    sum += length;
    for (i = 0; i + 1 < length; i += 2) {
        sum += ip_get16(udp + i);
    }
    if (i < length) {
        sum += (uint16_t) udp[i] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

static bool net_send_udp_frame(const uint8_t dst_mac[6],
                               const uint8_t src_ip[4],
                               const uint8_t dst_ip[4],
                               uint16_t src_port,
                               uint16_t dst_port,
                               const uint8_t *payload,
                               uint16_t payload_len,
                               uint16_t ident)
{
    uint8_t packet[1514];
    uint8_t *udp = packet + 34;
    uint16_t udp_len;
    uint16_t frame_len;

    if (dst_mac == NULL || src_ip == NULL || dst_ip == NULL || payload == NULL || payload_len > 1472) {
        return false;
    }
    udp_len = (uint16_t) (8 + payload_len);
    frame_len = (uint16_t) (42 + payload_len);
    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, dst_mac, NET_ETH_TYPE_IPV4);
    ip_write_header(packet + 14, NET_IP_PROTO_UDP, src_ip, dst_ip, udp_len, ident);
    ip_put16(udp + 0, src_port);
    ip_put16(udp + 2, dst_port);
    ip_put16(udp + 4, udp_len);
    ip_put16(udp + 6, 0);
    memcpy(udp + 8, payload, payload_len);
    ip_put16(udp + 6, net_udp_checksum(src_ip, dst_ip, udp, udp_len));
    return net_send_frame(packet, frame_len);
}

static uint32_t net_make_dhcp_xid(void)
{
    return 0x4D4F0000u | ((uint32_t) g_mac[4] << 8) | g_mac[5];
}

static uint32_t net_read32_be(const uint8_t *data)
{
    return ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) | ((uint32_t) data[2] << 8) | data[3];
}

static void net_put32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t) (value >> 24);
    data[1] = (uint8_t) (value >> 16);
    data[2] = (uint8_t) (value >> 8);
    data[3] = (uint8_t) value;
}

static bool net_send_dhcp_message(uint8_t message_type, const uint8_t requested_ip[4], const uint8_t server_ip[4])
{
    uint8_t payload[300];
    uint8_t *opt;
    uint16_t payload_len;
    static const uint8_t broadcast_mac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t zero_ip[4] = { 0, 0, 0, 0 };
    static const uint8_t broadcast_ip[4] = { 255, 255, 255, 255 };

    memset(payload, 0, sizeof(payload));
    payload[0] = 1;
    payload[1] = 1;
    payload[2] = 6;
    net_put32_be(payload + 4, g_dhcp_xid);
    ip_put16(payload + 10, 0x8000);
    memcpy(payload + 28, g_mac, 6);
    net_put32_be(payload + 236, NET_DHCP_MAGIC_COOKIE);

    opt = payload + 240;
    *opt++ = 53;
    *opt++ = 1;
    *opt++ = message_type;
    if (requested_ip != NULL) {
        *opt++ = 50;
        *opt++ = 4;
        memcpy(opt, requested_ip, 4);
        opt += 4;
    }
    if (server_ip != NULL) {
        *opt++ = 54;
        *opt++ = 4;
        memcpy(opt, server_ip, 4);
        opt += 4;
    }
    *opt++ = 55;
    *opt++ = 4;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 6;
    *opt++ = 51;
    *opt++ = 12;
    *opt++ = 6;
    memcpy(opt, "monios", 6);
    opt += 6;
    *opt++ = 255;
    payload_len = (uint16_t) (opt - payload);

    strcpy(g_net_status, message_type == NET_DHCP_DISCOVER ? "net: dhcp discover" : "net: dhcp request");
    log_write(g_net_status);
    return net_send_udp_frame(broadcast_mac,
                              zero_ip,
                              broadcast_ip,
                              NET_DHCP_CLIENT_PORT,
                              NET_DHCP_SERVER_PORT,
                              payload,
                              payload_len,
                              (uint16_t) g_dhcp_xid);
}

static bool net_send_icmp_reply(const uint8_t dst_mac[6], const uint8_t dst_ip[4], const uint8_t *request, uint16_t request_size)
{
    uint8_t packet[128];
    uint8_t *ip = packet + 14;
    uint8_t *icmp = packet + 34;
    uint16_t icmp_size;

    if (request_size < 34 + 8 || request_size > sizeof(packet)) {
        return false;
    }
    icmp_size = (uint16_t) (request_size - 34);
    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, dst_mac, NET_ETH_TYPE_IPV4);
    ip[0] = 0x45;
    ip_put16(ip + 2, (uint16_t) (20 + icmp_size));
    ip_put16(ip + 4, ip_get16(request + 18));
    ip_put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = NET_IP_PROTO_ICMP;
    memcpy(ip + 12, g_local_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    ip_put16(ip + 10, ip_checksum(ip, 20));
    memcpy(icmp, request + 34, icmp_size);
    icmp[0] = NET_ICMP_ECHO_REPLY;
    ip_put16(icmp + 2, 0);
    ip_put16(icmp + 2, ip_checksum(icmp, icmp_size));
    return net_send_frame(packet, (uint16_t) (34 + icmp_size));
}

static bool net_send_icmp_echo(const uint8_t dst_mac[6], const uint8_t dst_ip[4], uint16_t sequence)
{
    uint8_t packet[74];
    uint8_t *ip = packet + 14;
    uint8_t *icmp = packet + 34;

    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, dst_mac, NET_ETH_TYPE_IPV4);
    ip[0] = 0x45;
    ip[1] = 0;
    ip_put16(ip + 2, 60);
    ip_put16(ip + 4, sequence);
    ip_put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = NET_IP_PROTO_ICMP;
    memcpy(ip + 12, g_local_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    ip_put16(ip + 10, ip_checksum(ip, 20));

    icmp[0] = NET_ICMP_ECHO_REQUEST;
    icmp[1] = 0;
    ip_put16(icmp + 4, NET_ICMP_ID);
    ip_put16(icmp + 6, sequence);
    memcpy(icmp + 8, "MONIOS-PING-VMWARE-E1000-ICMP-PAYLOAD", 32);
    ip_put16(icmp + 2, ip_checksum(icmp, 40));
    {
        char ip_text[16];
        strcpy(g_net_status, "net: icmp echo ");
        ip_to_text(dst_ip, ip_text);
        strcpy(g_net_status + strlen(g_net_status), ip_text);
        log_write(g_net_status);
    }
    return net_send_frame(packet, sizeof(packet));
}

static void net_handle_dhcp(const uint8_t *udp_payload, uint16_t udp_payload_len)
{
    const uint8_t *opt;
    const uint8_t *end;
    uint8_t message_type = 0;
    uint8_t server_ip[4] = { 0, 0, 0, 0 };
    uint8_t offered_mask[4] = { 255, 255, 255, 0 };
    uint8_t offered_gateway[4] = { 0, 0, 0, 0 };
    uint8_t offered_dns[4] = { 0, 0, 0, 0 };
    bool have_server = false;

    if (!g_dhcp_waiting || udp_payload == NULL || udp_payload_len < 240) {
        return;
    }
    if (udp_payload[0] != 2 || udp_payload[1] != 1 || udp_payload[2] != 6) {
        return;
    }
    if (net_read32_be(udp_payload + 4) != g_dhcp_xid) {
        return;
    }
    if (memcmp(udp_payload + 28, g_mac, 6) != 0 || net_read32_be(udp_payload + 236) != NET_DHCP_MAGIC_COOKIE) {
        return;
    }

    opt = udp_payload + 240;
    end = udp_payload + udp_payload_len;
    while (opt < end && *opt != 255) {
        uint8_t code;
        uint8_t len;

        code = *opt++;
        if (code == 0) {
            continue;
        }
        if (opt >= end) {
            break;
        }
        len = *opt++;
        if (opt + len > end) {
            break;
        }
        if (code == 53 && len >= 1) {
            message_type = opt[0];
        } else if (code == 54 && len >= 4) {
            memcpy(server_ip, opt, 4);
            have_server = true;
        } else if (code == 1 && len >= 4) {
            memcpy(offered_mask, opt, 4);
        } else if (code == 3 && len >= 4) {
            memcpy(offered_gateway, opt, 4);
        } else if (code == 6 && len >= 4) {
            memcpy(offered_dns, opt, 4);
        }
        opt += len;
    }

    if (message_type != g_dhcp_expected_type) {
        return;
    }
    if (message_type == NET_DHCP_OFFER) {
        memcpy(g_dhcp_offered_ip, udp_payload + 16, 4);
        memcpy(g_dhcp_server_ip, have_server ? server_ip : udp_payload + 20, 4);
        memcpy(g_dhcp_offer_mask, offered_mask, 4);
        memcpy(g_dhcp_offer_gateway, offered_gateway, 4);
        memcpy(g_dhcp_offer_dns, offered_dns, 4);
        g_dhcp_offer_valid = true;
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp offer ");
        ip_to_text(g_dhcp_offered_ip, g_net_status + strlen(g_net_status));
        log_write(g_net_status);
        return;
    }
    if (message_type == NET_DHCP_ACK) {
        memcpy(g_local_ip, udp_payload + 16, 4);
        memcpy(g_netmask, offered_mask, 4);
        if (offered_gateway[0] != 0 || offered_gateway[1] != 0 || offered_gateway[2] != 0 || offered_gateway[3] != 0) {
            memcpy(g_gateway_ip, offered_gateway, 4);
        }
        memcpy(g_dns_ip, offered_dns, 4);
        g_net_info.dhcp_configured = true;
        net_refresh_ip_texts();
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp ack ip ");
        strcpy(g_net_status + strlen(g_net_status), g_net_info.ip_text);
        log_write(g_net_status);
    }
}

static void net_handle_udp(const uint8_t *packet, uint16_t length)
{
    const uint8_t *ip;
    const uint8_t *udp;
    const uint8_t *udp_payload;
    uint16_t ip_total;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t udp_len;

    if (packet == NULL || length < 42) {
        return;
    }
    ip = packet + 14;
    ip_total = ip_get16(ip + 2);
    if (ip_total < 28 || (uint32_t) ip_total + 14 > length) {
        return;
    }
    udp = ip + 20;
    src_port = ip_get16(udp + 0);
    dst_port = ip_get16(udp + 2);
    udp_len = ip_get16(udp + 4);
    if (udp_len < 8 || (uint32_t) udp_len + 20 > ip_total) {
        return;
    }
    udp_payload = udp + 8;
    if (src_port == NET_DHCP_SERVER_PORT && dst_port == NET_DHCP_CLIENT_PORT) {
        net_handle_dhcp(udp_payload, (uint16_t) (udp_len - 8));
        return;
    }
    dns_handle_udp(ip + 12, src_port, udp_payload, (uint16_t) (udp_len - 8));
    socket_handle_udp(ip + 12, src_port, dst_port, udp_payload, (uint16_t) (udp_len - 8));
}

static bool arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6])
{
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_valid[i] && ip_equal(g_arp_ip[i], ip)) {
            if (out_mac != NULL) {
                memcpy(out_mac, g_arp_mac[i], 6);
            }
            return true;
        }
    }
    return false;
}

static void arp_cache_add(const uint8_t ip[4], const uint8_t mac[6])
{
    uint32_t slot = g_arp_next_slot;

    memcpy(g_arp_ip[slot], ip, 4);
    memcpy(g_arp_mac[slot], mac, 6);
    g_arp_valid[slot] = true;
    g_arp_next_slot = (g_arp_next_slot + 1) % ARP_CACHE_SIZE;
}

static void net_handle_arp(const uint8_t *packet, uint16_t length)
{
    uint16_t opcode;
    const uint8_t *sender_mac;
    const uint8_t *sender_ip;
    const uint8_t *target_ip;

    if (length < 42 || ip_get16(packet + 14) != 1 || ip_get16(packet + 16) != NET_ETH_TYPE_IPV4 ||
        packet[18] != 6 || packet[19] != 4) {
        return;
    }
    opcode = ip_get16(packet + 20);
    sender_mac = packet + 22;
    sender_ip = packet + 28;
    target_ip = packet + 38;

    if (opcode == 2) {
        /* ARP reply: cache sender IP/MAC and log details */
        arp_cache_add(sender_ip, sender_mac);
        {
            char mac_text[18];
            char ip_text[16];
            for (uint32_t i = 0; i < 6; i++) {
                net_hex_byte(&mac_text[i * 3], sender_mac[i]);
                if (i < 5) mac_text[i * 3 + 2] = ':';
            }
            mac_text[17] = '\0';
            ip_to_text(sender_ip, ip_text);
            strcpy(g_net_status, "net: arp reply cached ");
            strcpy(g_net_status + strlen(g_net_status), ip_text);
            log_write(g_net_status);
            strcpy(g_net_status, "net: arp reply mac ");
            strcpy(g_net_status + strlen(g_net_status), mac_text);
            log_write(g_net_status);
        }
        return;
    }

    if (opcode == 1 && ip_equal(target_ip, g_local_ip)) {
        /* ARP request for our IP: log requester info then reply */
        char mac_text[18];
        char ip_text[16];
        for (uint32_t i = 0; i < 6; i++) {
            net_hex_byte(&mac_text[i * 3], sender_mac[i]);
            if (i < 5) mac_text[i * 3 + 2] = ':';
        }
        mac_text[17] = '\0';
        ip_to_text(sender_ip, ip_text);
        strcpy(g_net_status, "net: arp req from ");
        strcpy(g_net_status + strlen(g_net_status), ip_text);
        log_write(g_net_status);
        strcpy(g_net_status, "net: arp req mac ");
        strcpy(g_net_status + strlen(g_net_status), mac_text);
        log_write(g_net_status);
        net_send_arp_reply(sender_mac, sender_ip);
    }
}

static void net_handle_ipv4(const uint8_t *packet, uint16_t length)
{
    const uint8_t *ip;
    const uint8_t *icmp;
    uint16_t ip_total;
    uint16_t sequence;
    uint16_t ident;

    if (length < 34) {
        return;
    }
    ip = packet + 14;
    if ((ip[0] >> 4) != 4 || (ip[0] & 0x0F) != 5) {
        return;
    }
    if (!ip_equal(ip + 16, g_local_ip) &&
        !(ip[16] == 255 && ip[17] == 255 && ip[18] == 255 && ip[19] == 255)) {
        return;
    }
    if (ip[9] == NET_IP_PROTO_UDP) {
        net_handle_udp(packet, length);
        return;
    }
    if (ip[9] == NET_IP_PROTO_TCP) {
        tcp_handle_ipv4(packet, length);
        return;
    }
    if (ip[9] != NET_IP_PROTO_ICMP) {
        return;
    }
    ip_total = ip_get16(ip + 2);
    if (ip_total < 28 || (uint32_t) ip_total + 14 > length) {
        return;
    }
    icmp = ip + 20;
    ident = ip_get16(icmp + 4);
    sequence = ip_get16(icmp + 6);

    if (icmp[0] == NET_ICMP_ECHO_REPLY && ident == NET_ICMP_ID && g_ping_waiting &&
        sequence == g_ping_seq && ip_equal(ip + 12, g_ping_target_ip)) {
        g_ping_waiting = false;
        g_net_info.ping_replies++;
        strcpy(g_net_status, "net: ping reply from ");
        ip_to_text(ip + 12, g_net_status + strlen(g_net_status));
        return;
    }
    if (icmp[0] == NET_ICMP_ECHO_REQUEST) {
        net_send_icmp_reply(packet + 6, ip + 12, packet, (uint16_t) (14 + ip_total));
    }
}

static void net_packet_handler(const uint8_t *packet, uint16_t length)
{
    uint16_t ether_type;

    if (packet == NULL || length < 14) {
        return;
    }
    ether_type = ip_get16(packet + 12);
    if (ether_type == NET_ETH_TYPE_ARP) {
        net_handle_arp(packet, length);
    } else if (ether_type == NET_ETH_TYPE_IPV4) {
        net_handle_ipv4(packet, length);
    }
}

static void net_poll(void)
{
    e1000_poll(net_packet_handler);
}

static void net_start_rx_if_ready(void)
{
    if (g_net_backend_ready && g_net_info.connected && !g_rx_started) {
        e1000_rx_start();
        g_rx_started = true;
    }
}

bool net_dhcp_request(void)
{
    if (!g_net_info.present || !g_net_backend_ready || !g_net_info.connected) {
        strcpy(g_net_status, "net: dhcp unavailable");
        log_write(g_net_status);
        return false;
    }

    net_start_rx_if_ready();
    g_dhcp_xid = net_make_dhcp_xid();
    g_dhcp_offer_valid = false;
    g_dhcp_expected_type = NET_DHCP_OFFER;
    g_dhcp_waiting = true;
    if (!net_send_dhcp_message(NET_DHCP_DISCOVER, NULL, NULL)) {
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp discover failed");
        log_write(g_net_status);
        return false;
    }
    for (uint32_t i = 0; i < 800000 && g_dhcp_waiting; i++) {
        net_poll();
        io_wait();
    }
    if (!g_dhcp_offer_valid) {
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp timeout");
        log_write(g_net_status);
        return false;
    }

    g_dhcp_expected_type = NET_DHCP_ACK;
    g_dhcp_waiting = true;
    if (!net_send_dhcp_message(NET_DHCP_REQUEST, g_dhcp_offered_ip, g_dhcp_server_ip)) {
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp request failed");
        log_write(g_net_status);
        return false;
    }
    for (uint32_t i = 0; i < 800000 && g_dhcp_waiting; i++) {
        net_poll();
        io_wait();
    }
    if (g_dhcp_waiting) {
        g_dhcp_waiting = false;
        strcpy(g_net_status, "net: dhcp ack timeout");
        log_write(g_net_status);
        return false;
    }
    return g_net_info.dhcp_configured;
}

bool net_driver_init(void)
{
    pci_device_info_t info;

    memset(&g_net_info, 0, sizeof(g_net_info));
    memset(g_mac, 0, sizeof(g_mac));
    g_net_backend_ready = false;
    g_rx_started = false;
    g_ping_waiting = false;
    g_arp_next_slot = 0;
    for (uint32_t i = 0; i < ARP_CACHE_SIZE; i++) {
        g_arp_valid[i] = false;
    }
    g_dhcp_waiting = false;
    g_dhcp_offer_valid = false;
    g_ping_seq = 0;
    tcp_init();
    dns_init();
    socket_init();
    strcpy(g_net_info.driver, "none");
    strcpy(g_net_info.mac_text, "--:--:--:--:--:--");
    net_refresh_ip_texts();
    if (!pci_find_first(PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET, &info)) {
        strcpy(g_net_status, "net: no ethernet device detected");
        log_write(g_net_status);
        return true;
    }

    g_net_info.present = true;
    g_net_info.connected = true;
    g_net_info.vendor_id = info.vendor_id;
    g_net_info.device_id = info.device_id;
    g_net_info.bus = info.bus;
    g_net_info.slot = info.slot;
    g_net_info.func = info.func;
    g_net_info.irq = info.interrupt_line;
    /* Only touch MMIO ranges that the early page tables identity-map. */
    uint32_t mmio_bar = info.bar0 & 0xFFFFFFF0u;
    if (e1000_supported(&info) && mmio_bar != 0 && net_mmio_identity_mapped(mmio_bar) &&
        e1000_init(&info, &g_net_info, g_mac)) {
        g_net_backend_ready = e1000_ready();
        strcpy(g_net_status, "net: e1000 ring ready");
    } else if (pcnet_supported(&info) && pcnet_init(&info, &g_net_info, g_mac)) {
        g_net_backend_ready = pcnet_ready();
        strcpy(g_net_status, pcnet_status());
    } else {
        if (e1000_supported(&info) && mmio_bar != 0 && !net_mmio_identity_mapped(mmio_bar)) {
            log_write("net: e1000 skipped unmapped MMIO BAR");
        }
        strcpy(g_net_info.driver, "onboard-ether");
        g_net_info.mmio_base = 0;
        g_net_info.io_base = 0;
        strcpy(g_net_status, "net: onboard ethernet link ready");
    }
    net_set_mac_text(g_mac);
    log_write(g_net_status);
    return true;
}

void net_init(void)
{
    char ip_text[16];

    if (!g_net_info.present) {
        strcpy(g_net_status, "net: offline");
        return;
    }
    if (g_net_backend_ready) {
        g_net_info.connected = e1000_link_up();
    }
    if (!g_net_info.connected) {
        strcpy(g_net_status, "net: link down");
        log_write(g_net_status);
        return;
    }
    net_start_rx_if_ready();
    if (!net_dhcp_request()) {
        log_write("net: dhcp fallback static ip");
        g_net_info.dhcp_configured = false;
        net_refresh_ip_texts();
    }
    ip_to_text(g_local_ip, ip_text);
    strcpy(g_net_status, "net: link up ip ");
    strcpy(g_net_status + strlen(g_net_status), ip_text);
    log_write(g_net_status);
}

void net_update(void)
{
    if (g_net_backend_ready) {
        g_net_info.connected = e1000_link_up();
        if (g_net_info.connected) {
            net_start_rx_if_ready();
            net_poll();
        }
    }
}

const uint8_t *net_local_ip(void)
{
    return g_local_ip;
}

bool net_get_dns_ip(uint8_t out[4])
{
    if (out == NULL || (g_dns_ip[0] == 0 && g_dns_ip[1] == 0 && g_dns_ip[2] == 0 && g_dns_ip[3] == 0)) {
        return false;
    }
    memcpy(out, g_dns_ip, 4);
    return true;
}

bool net_resolve_ipv4(const char *target, uint8_t out[4])
{
    if (target == NULL || out == NULL) {
        return false;
    }
    if (strcmp(target, "gateway") == 0) {
        memcpy(out, g_gateway_ip, 4);
        return true;
    }
    if (strcmp(target, "host") == 0) {
        memcpy(out, g_host_ip, 4);
        return true;
    }
    if (strcmp(target, "local") == 0 || strcmp(target, "self") == 0) {
        memcpy(out, g_local_ip, 4);
        return true;
    }
    return dns_resolve_ipv4(target, out);
}

bool net_send_ipv4_packet(const uint8_t dst_ip[4], uint8_t proto, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t packet[1514];
    uint8_t next_hop_ip[4];
    uint16_t frame_len;

    if (dst_ip == NULL || payload == NULL || payload_len > 1480 || !g_net_backend_ready || !g_net_info.connected) {
        log_write("net: ipv4 tx unavailable");
        return false;
    }
    if (net_is_local_ip(dst_ip)) {
        log_write("net: ipv4 local delivery");
        return true;
    }
    if (ip_same_subnet(g_local_ip, dst_ip, g_netmask)) {
        memcpy(next_hop_ip, dst_ip, 4);
    } else {
        memcpy(next_hop_ip, g_gateway_ip, 4);
    }

    uint8_t next_hop_mac[6];

    if (arp_cache_lookup(next_hop_ip, next_hop_mac)) {
        log_write("net: ipv4 arp cache hit");
    } else {
        {
            char ip_text[16];
            strcpy(g_net_status, "net: ipv4 next-hop ");
            ip_to_text(next_hop_ip, ip_text);
            strcpy(g_net_status + strlen(g_net_status), ip_text);
            log_write(g_net_status);
        }
        net_start_rx_if_ready();
        if (!net_send_arp_request(next_hop_ip)) {
            log_write("net: ipv4 arp send failed");
            return false;
        }
        for (uint32_t i = 0; i < 200000; i++) {
            net_poll();
            if (arp_cache_lookup(next_hop_ip, next_hop_mac)) {
                log_write("net: ipv4 arp resolved");
                break;
            }
            io_wait();
        }
        if (!arp_cache_lookup(next_hop_ip, next_hop_mac)) {
            log_write("net: ipv4 arp timeout");
            return false;
        }
    }

    memset(packet, 0, sizeof(packet));
    net_write_ether_header(packet, next_hop_mac, NET_ETH_TYPE_IPV4);
    ip_write_header(packet + 14, proto, g_local_ip, dst_ip, payload_len, (uint16_t) (g_ping_seq + 1));
    memcpy(packet + 34, payload, payload_len);
    frame_len = (uint16_t) (34 + payload_len);
    return net_send_frame(packet, frame_len);
}

bool net_udp_send(const char *dst_ip_text, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t dst_ip[4];

    if (dst_ip_text == NULL || payload == NULL || payload_len > 1472 || dst_port == 0) {
        strcpy(g_net_status, "net: udp invalid");
        return false;
    }
    if (!net_resolve_ipv4(dst_ip_text, dst_ip)) {
        strcpy(g_net_status, "net: udp resolve failed");
        return false;
    }
    return net_udp_send_to(dst_ip, 49152, dst_port, payload, payload_len);
}

bool net_udp_send_to(const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t udp[1480];
    uint16_t udp_len;

    if (dst_ip == NULL || payload == NULL || payload_len > 1472 || src_port == 0 || dst_port == 0) {
        strcpy(g_net_status, "net: udp invalid");
        return false;
    }
    udp_len = (uint16_t) (8 + payload_len);
    memset(udp, 0, sizeof(udp));
    ip_put16(udp + 0, src_port);
    ip_put16(udp + 2, dst_port);
    ip_put16(udp + 4, udp_len);
    ip_put16(udp + 6, 0);
    memcpy(udp + 8, payload, payload_len);
    ip_put16(udp + 6, net_udp_checksum(g_local_ip, dst_ip, udp, udp_len));
    if (!net_send_ipv4_packet(dst_ip, NET_IP_PROTO_UDP, udp, udp_len)) {
        strcpy(g_net_status, "net: udp tx failed");
        return false;
    }
    strcpy(g_net_status, "net: udp sent");
    return true;
}

const char *net_status(void)
{
    return g_net_status;
}

bool net_connected(void)
{
    return g_net_info.present && g_net_info.connected;
}

bool net_ping(const char *target)
{
    uint8_t target_ip[4];
    uint8_t next_hop_ip[4];
    char ip_text[16];

    if (target == NULL || target[0] == '\0') {
        return false;
    }
    if (strcmp(target, "localhost") == 0 || strcmp(target, "loopback") == 0) {
        uint8_t loopback_ip[4] = { 127, 0, 0, 1 };

        return net_ping_local_reply(loopback_ip, target);
    }
    if (ip_parse_ipv4(target, target_ip) && net_is_loopback_ip(target_ip)) {
        return net_ping_local_reply(target_ip, target);
    }
    if (!g_net_info.present || !g_net_info.connected || !g_net_backend_ready) {
        strcpy(g_net_status, "net: ping unavailable");
        log_write(g_net_status);
        return false;
    }
    if (!net_resolve_ipv4(target, target_ip)) {
        strcpy(g_net_status, dns_status());
        log_write(g_net_status);
        return false;
    }

    if (net_is_local_ip(target_ip)) {
        return net_ping_local_reply(target_ip, target);
    }
    net_set_last_target(target);

    if (ip_same_subnet(g_local_ip, target_ip, g_netmask)) {
        memcpy(next_hop_ip, target_ip, 4);
    } else {
        memcpy(next_hop_ip, g_gateway_ip, 4);
    }
    ip_to_text(next_hop_ip, ip_text);
    strcpy(g_net_status, "net: arp ");
    strcpy(g_net_status + strlen(g_net_status), ip_text);
    log_write(g_net_status);
    strcpy(g_net_status, "net: ping target ");
    ip_to_text(target_ip, ip_text);
    strcpy(g_net_status + strlen(g_net_status), ip_text);
    log_write(g_net_status);

    uint8_t next_hop_mac[6];

    if (!arp_cache_lookup(next_hop_ip, next_hop_mac)) {
        net_start_rx_if_ready();
        if (!net_send_arp_request(next_hop_ip)) {
            strcpy(g_net_status, "net: arp send failed");
            log_write(g_net_status);
            return false;
        }
        for (uint32_t i = 0; i < 200000; i++) {
            net_poll();
            if (arp_cache_lookup(next_hop_ip, next_hop_mac)) {
                break;
            }
            io_wait();
        }
        if (!arp_cache_lookup(next_hop_ip, next_hop_mac)) {
            strcpy(g_net_status, "net: arp timeout");
            log_write(g_net_status);
            return false;
        }
    }
    log_write("net: ping arp resolved");

    g_net_info.ping_requests++;
    g_ping_seq++;
    memcpy(g_ping_target_ip, target_ip, 4);
    g_ping_waiting = true;
    if (!net_send_icmp_echo(next_hop_mac, target_ip, g_ping_seq)) {
        strcpy(g_net_status, "net: ping tx failed");
        g_ping_waiting = false;
        log_write(g_net_status);
        return false;
    }

    strcpy(g_net_status, "net: ping sent to ");
    ip_to_text(target_ip, g_net_status + strlen(g_net_status));
    log_write(g_net_status);
    for (uint32_t i = 0; i < 250000; i++) {
        net_poll();
        if (!g_ping_waiting) {
            log_write(g_net_status);
            return true;
        }
        io_wait();
    }
    g_ping_waiting = false;
    strcpy(g_net_status, "net: ping timeout");
    log_write(g_net_status);
    return false;
}

void net_shutdown(void)
{
    if (g_net_info.present) {
        e1000_shutdown();
        pcnet_shutdown();
        strcpy(g_net_status, "net: shutdown");
        g_net_info.connected = false;
        g_rx_started = false;
        log_write(g_net_status);
    }
}

const net_info_t *net_info(void)
{
    return &g_net_info;
}
