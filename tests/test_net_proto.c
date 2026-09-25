/*
 * test_net_proto.c — host-side (Windows / MinGW) protocol correctness tests
 * for the Monios network stack:
 *
 *   - IP header checksum (RFC 1071 one's-complement sum) via kernel/net/ip.c
 *   - TCP checksum (pseudo-header + segment), mirroring kernel/net/tcp.c's
 *     static tcp_checksum()
 *   - DNS wire-format name encoding, mirroring kernel/net/dns.c's static
 *     dns_encode_name()
 *
 * Build: gcc -std=gnu17 -fno-builtin -I ../include -I . -o test_net_proto.exe
 *        test_net_proto.c stubs.c
 *
 * ip.c is pulled in by #include (same translation unit => its externs are
 * reachable); the TCP/DNS helpers are reproduced here verbatim from the
 * kernel sources because those functions are `static` inside tcp.c/dns.c,
 * which drags in hardware dependencies on the host.
 */

int printf(const char *fmt, ...);

#include "common.h"
#include "stdint.h"
#include "stdbool.h"
#include "string.h"

/* ---- code under test: kernel IP layer (only file.c-free subset) ---- */
#include "../kernel/net/ip.c"

/* ============================================================
 *  Tiny test harness
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
        } else {                                                           \
            g_fail++;                                                      \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);   \
        }                                                                  \
    } while (0)

static void section(const char *name)
{
    printf("\n== %s ==\n", name);
}

/* ============================================================
 *  IP checksum (RFC 1071)
 * ============================================================ */
static void test_ip_putget16(void)
{
    section("ip_put16 / ip_get16");
    uint8_t buf[2];
    ip_put16(buf, 0x1234);
    CHECK(buf[0] == 0x12 && buf[1] == 0x34);       /* network byte order */
    CHECK(ip_get16(buf) == 0x1234);
    ip_put16(buf, 0x0000);
    CHECK(ip_get16(buf) == 0x0000);
    ip_put16(buf, 0xFFFF);
    CHECK(ip_get16(buf) == 0xFFFF);
}

static void test_ip_checksum_basic(void)
{
    section("ip_checksum basic vectors");
    uint8_t zero[2] = { 0x00, 0x00 };
    /* sum of 0x0000 = 0x0000; ~0 = 0xFFFF */
    CHECK(ip_checksum(zero, 2) == 0xFFFF);

    uint8_t one[2] = { 0x00, 0x01 };
    /* sum = 0x0001; ~0x0001 = 0xFFFE */
    CHECK(ip_checksum(one, 2) == 0xFFFE);

    /* odd-length buffer: last byte padded as high-order */
    uint8_t odd[3] = { 0x12, 0x34, 0x56 };
    /* words: 0x1234 + 0x5600 = 0x6834 ; ~0x6834 = 0x97CB */
    CHECK(ip_checksum(odd, 3) == 0x97CB);

    /* adding the checksum to the data that produced it must yield 0xFFFF
     * (i.e. verifying a completed header gives 0). */
    uint8_t block[4] = { 0x01, 0x02, 0x03, 0x04 };
    uint16_t c = ip_checksum(block, 4);
    block[2] ^= 0x00; /* keep data; recompute including c */
    uint8_t withsum[6];
    memcpy(withsum, block, 4);
    withsum[4] = (uint8_t)(c >> 8);
    withsum[5] = (uint8_t)(c & 0xFF);
    CHECK(ip_checksum(withsum, 6) == 0x0000);
}

static void test_ip_checksum_known_header(void)
{
    section("ip_checksum known RFC vector");
    /* Classic textbook IPv4 header (checksum field zeroed at offset 10):
     * 45 00 00 73 00 00 40 00 40 11 00 00 c0 a8 00 01 c0 a8 00 c7
     * expected checksum = 0xB861. */
    uint8_t hdr[20] = {
        0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01,
        0xc0, 0xa8, 0x00, 0xc7
    };
    uint16_t c = ip_checksum(hdr, 20);
    CHECK(c == 0xB861u);

    /* insert it and re-verify: a valid header sums to zero. */
    hdr[10] = (uint8_t)(c >> 8);
    hdr[11] = (uint8_t)(c & 0xFF);
    CHECK(ip_checksum(hdr, 20) == 0x0000);
}

/* ============================================================
 *  TCP checksum — mirror of kernel/net/tcp.c:tcp_checksum()
 * ============================================================ */
static uint16_t ones_complement_sum(const uint8_t *data, uint32_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += ip_get16(data);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint16_t) data[0] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

static uint16_t tcp_checksum(const uint8_t src[4], const uint8_t dst[4],
                             const uint8_t *segment, uint16_t length)
{
    /* pseudo-header: src(4) dst(4) zero(1) proto(1)=6 tcp_len(2) */
    uint8_t pseudo[12];
    uint32_t sum = 0;
    uint16_t i;

    memcpy(pseudo, src, 4);
    memcpy(pseudo + 4, dst, 4);
    pseudo[8] = 0;
    pseudo[9] = 6;
    pseudo[10] = (uint8_t)(length >> 8);
    pseudo[11] = (uint8_t)(length & 0xFF);

    for (i = 0; i < 12; i += 2) {
        sum += ip_get16(pseudo + i);
    }
    while (length > 1) {
        sum += ip_get16(segment);
        segment += 2;
        length -= 2;
    }
    if (length) {
        sum += (uint16_t) segment[0] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

static void test_tcp_checksum(void)
{
    section("tcp checksum");
    const uint8_t src[4] = { 192, 168, 1, 1 };
    const uint8_t dst[4] = { 192, 168, 0, 201 };

    /* minimal TCP SYN-like segment: sport dport seq ack flags win cksum urg */
    uint8_t seg[20] = {
        0xC6, 0x5B, 0x00, 0x50,  /* src port 50779, dst port 80 */
        0x00, 0x00, 0x00, 0x00,  /* sequence */
        0x00, 0x00, 0x00, 0x00,  /* ack */
        0x50, 0x02, 0x72, 0x10,  /* header len, SYN, window 29200 */
        0x00, 0x00,              /* checksum (zeroed for computation) */
        0x00, 0x00               /* urgent */
    };

    uint16_t c = tcp_checksum(src, dst, seg, sizeof(seg));
    CHECK(c != 0x0000 && c != 0xFFFF);

    /* self-consistency: put the computed checksum back, re-verify == 0 */
    seg[16] = (uint8_t)(c >> 8);
    seg[17] = (uint8_t)(c & 0xFF);
    CHECK(tcp_checksum(src, dst, seg, sizeof(seg)) == 0x0000);

    /* corrupt one byte -> must no longer verify */
    seg[12] ^= 0x01;
    CHECK(tcp_checksum(src, dst, seg, sizeof(seg)) != 0x0000);

    /* the generic ones_complement_sum must agree with ip_checksum on the
     * same buffer (guards the local mirror). */
    uint8_t buf[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    CHECK(ones_complement_sum(buf, 8) == ip_checksum(buf, 8));
}

/* ============================================================
 *  DNS name encoding — mirror of kernel/net/dns.c:dns_encode_name()
 * ============================================================ */
static bool dns_encode_name(uint8_t *packet, uint16_t *pos, uint16_t max,
                            const char *name)
{
    const char *label = name;
    const char *p = name;

    for (;;) {
        uint32_t len = 0;
        while (p[len] != '\0' && p[len] != '.') {
            len++;
        }
        if (len == 0 || len > 63 || *pos + len + 1 >= max) {
            return false;
        }
        packet[(*pos)++] = (uint8_t) len;
        for (uint32_t i = 0; i < len; i++) {
            packet[(*pos)++] = (uint8_t) label[i];
        }
        if (p[len] == '\0') {
            break;
        }
        p += len + 1;
        label = p;
    }
    if (*pos >= max) {
        return false;
    }
    packet[(*pos)++] = 0;
    return true;
}

static void test_dns_encode(void)
{
    section("dns name encoding");
    uint8_t pkt[128];
    uint16_t pos = 0;

    /* www.example.com -> 3www 7example 3com 0 */
    memset(pkt, 0, sizeof(pkt));
    pos = 0;
    CHECK(dns_encode_name(pkt, &pos, sizeof(pkt), "www.example.com") == true);
    CHECK(pos == 1 + 3 + 1 + 7 + 1 + 3 + 1);   /* = 17 */
    CHECK(pkt[0] == 3 && pkt[1] == 'w' && pkt[3] == 'w');
    CHECK(pkt[4] == 7 && pkt[12] == 3 && pkt[16] == 0);

    /* single label localhost -> 9localhost 0 */
    pos = 0;
    memset(pkt, 0, sizeof(pkt));
    CHECK(dns_encode_name(pkt, &pos, sizeof(pkt), "localhost") == true);
    CHECK(pos == 1 + 9 + 1);
    CHECK(pkt[0] == 9 && pkt[10] == 0);

    /* trailing dot (root) yields a final empty label -> reject by kernel rule */
    pos = 0;
    CHECK(dns_encode_name(pkt, &pos, sizeof(pkt), "example.") == false);

    /* over-long label (>63 chars) rejected */
    char longname[80];
    for (int i = 0; i < 70; i++) { longname[i] = 'a'; }
    longname[70] = '\0';
    pos = 0;
    CHECK(dns_encode_name(pkt, &pos, sizeof(pkt), longname) == false);

    /* tiny buffer rejected */
    pos = 0;
    uint8_t tiny[4];
    CHECK(dns_encode_name(tiny, &pos, sizeof(tiny), "example.com") == false);
}

/* ============================================================
 *  IP text parsing / formatting (supporting cast for nettest dns)
 * ============================================================ */
static void test_ip_text(void)
{
    section("ip_parse_ipv4 / ip_to_text");
    uint8_t out[4];
    char text[16];

    CHECK(ip_parse_ipv4("192.168.1.10", out) == true);
    CHECK(out[0] == 192 && out[1] == 168 && out[2] == 1 && out[3] == 10);

    ip_to_text(out, text);
    CHECK(text[0] == '1' && strcmp(text, "192.168.1.10") == 0);

    CHECK(ip_parse_ipv4("256.1.1.1", out) == false);   /* octet > 255 */
    CHECK(ip_parse_ipv4("1.2.3", out) == false);      /* too few parts */
    CHECK(ip_parse_ipv4(NULL, out) == false);
    CHECK(ip_parse_ipv4("1.2.3.4.5", out) == false);  /* too many parts */
}

/* ============================================================
 *  Entry point
 * ============================================================ */
int main(void)
{
    printf("Monios network protocol host test\n");

    test_ip_putget16();
    test_ip_checksum_basic();
    test_ip_checksum_known_header();
    test_tcp_checksum();
    test_dns_encode();
    test_ip_text();

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
