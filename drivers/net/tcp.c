#include "common.h"
#include "ip.h"
#include "kernel.h"
#include "net.h"
#include "tcp.h"

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

static uint32_t g_tcp_packets;

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

static bool tcp_send_segment(const uint8_t dst_ip[4], const uint8_t src_port[2], const uint8_t dst_port[2], uint32_t seq, uint32_t ack, uint8_t flags)
{
    uint8_t tcp[20];
    const uint8_t *src_ip = net_local_ip();

    if (src_ip == NULL) {
        return false;
    }
    memset(tcp, 0, sizeof(tcp));
    tcp[0] = dst_port[0];
    tcp[1] = dst_port[1];
    tcp[2] = src_port[0];
    tcp[3] = src_port[1];
    tcp[4] = (uint8_t) (seq >> 24);
    tcp[5] = (uint8_t) (seq >> 16);
    tcp[6] = (uint8_t) (seq >> 8);
    tcp[7] = (uint8_t) seq;
    tcp[8] = (uint8_t) (ack >> 24);
    tcp[9] = (uint8_t) (ack >> 16);
    tcp[10] = (uint8_t) (ack >> 8);
    tcp[11] = (uint8_t) ack;
    tcp[12] = 0x50;
    tcp[13] = flags;
    tcp[14] = 0x20;
    tcp[15] = 0x00;
    tcp[16] = 0;
    tcp[17] = 0;
    tcp[18] = 0;
    tcp[19] = 0;
    ip_put16(tcp + 16, tcp_checksum(src_ip, dst_ip, tcp, 20));
    return net_send_ipv4_packet(dst_ip, 6, tcp, 20);
}

void tcp_init(void)
{
    g_tcp_packets = 0;
    log_write("tcp: ipv4 stack ready");
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
    uint8_t src_ip[4];

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
    if (!ip_equal(ip + 16, net_local_ip())) {
        return;
    }
    tcp = ip + 20;
    src_port = ip_get16(tcp + 0);
    dst_port = ip_get16(tcp + 2);
    seq = ((uint32_t) tcp[4] << 24) | ((uint32_t) tcp[5] << 16) | ((uint32_t) tcp[6] << 8) | tcp[7];
    ack = ((uint32_t) tcp[8] << 24) | ((uint32_t) tcp[9] << 16) | ((uint32_t) tcp[10] << 8) | tcp[11];
    flags = tcp[13];
    memcpy(src_ip, ip + 12, 4);

    g_tcp_packets++;
    if ((flags & TCP_FLAG_SYN) != 0 && (flags & TCP_FLAG_ACK) == 0) {
        uint8_t rst_flags = TCP_FLAG_RST | TCP_FLAG_ACK;
        uint8_t src_port_bytes[2] = { (uint8_t) (dst_port >> 8), (uint8_t) dst_port };
        uint8_t dst_port_bytes[2] = { (uint8_t) (src_port >> 8), (uint8_t) src_port };

        (void) tcp_send_segment(src_ip, src_port_bytes, dst_port_bytes, 0, seq + 1, rst_flags);
        log_write("tcp: rst sent");
        return;
    }
    if ((flags & TCP_FLAG_ACK) != 0) {
        log_write("tcp: ack observed");
    }
}
