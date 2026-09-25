/*
 * test_net.c — host-side (Windows / MinGW) regression tests for the
 * Monios network stack pure logic:
 *   - kernel/net/ip.c     : IPv4 checksum, header build, text parsing
 *   - kernel/net/tcp.c    : connection state machine (SYN -> ESTABLISHED
 *                           -> data -> FIN_WAIT -> CLOSE), RST handling
 *   - kernel/net/dns.c    : name validation, response parsing (incl.
 *                           compression pointers), A-record extraction
 *
 * The real sources are pulled in by #include (same-translation-unit) so
 * their static tables (g_connections, g_dns_query_*, ...) are reachable.
 * Hardware / NIC dependencies are supplied by stubs_ext.c (fake cable).
 *
 * Build: gcc -I ../include -I . -o test_net.exe test_net.c stubs_ext.c
 */

int printf(const char *fmt, ...);

#include "common.h"
#include "cpu.h"
#include "dns.h"
#include "ip.h"
#include "ipv4.h"
#include "kernel.h"
#include "net.h"
#include "string.h"
#include "tcp.h"

/* ---- code under test (same TU => statics reachable) ---- */
#include "../kernel/net/ip.c"
#include "../kernel/net/tcp.c"
#include "../kernel/net/dns.c"

/* ============================================================
 *  Tiny test harness (same CHECK/section style as test_vfs.c)
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

static void section(const char *name) { printf("\n== %s ==\n", name); }

/* ============================================================
 *  IPv4 layer
 * ============================================================ */
static void test_ip_order(void)
{
    section("ip_put16 / ip_get16 (network byte order)");
    uint8_t b[2];
    ip_put16(b, 0x1234);
    CHECK(b[0] == 0x12);
    CHECK(b[1] == 0x34);
    CHECK(ip_get16(b) == 0x1234);

    ip_put16(b, 0x0000);
    CHECK(ip_get16(b) == 0x0000);
    ip_put16(b, 0xFFFF);
    CHECK(ip_get16(b) == 0xFFFF);
}

static void test_ip_checksum(void)
{
    section("ip_checksum (RFC 1071)");

    /* Known vector: 45 00 00 3C 1F 7B 00 00 40 11 .. .. (checksum zeroed)
     * Expected checksum = 0xB861 (canonical example). */
    uint8_t hdr[20] = {
        0x45, 0x00, 0x00, 0x3C, 0x1F, 0x7B, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00, 0xC0, 0xA8, 0x00, 0x01,
        0xC0, 0xA8, 0x00, 0x02
    };
    uint16_t c = ip_checksum(hdr, 20);
    CHECK(c == 0xD9E2U);

    /* Verify: write the checksum in, re-checksum whole header -> 0. */
    hdr[10] = (uint8_t) (c >> 8);
    hdr[11] = (uint8_t) c;
    CHECK(ip_checksum(hdr, 20) == 0);

    /* Odd-length buffer: trailing byte padded high. */
    uint8_t odd[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    uint16_t c2 = ip_checksum(odd, 5);
    uint8_t two[6];
    two[0] = 0x01; two[1] = 0x02; two[2] = 0x03; two[3] = 0x04;
    two[4] = 0x05; two[5] = 0x00;
    /* odd handling: last byte shifted high => equivalent to 0x0500 */
    CHECK(c2 == ip_checksum(two, 6));
}

static void test_ip_parse(void)
{
    section("ip_parse_ipv4");
    uint8_t o[4];
    CHECK(ip_parse_ipv4("192.168.1.10", o) == true);
    CHECK(o[0] == 192 && o[1] == 168 && o[2] == 1 && o[3] == 10);
    CHECK(ip_parse_ipv4("0.0.0.0", o) == true);
    CHECK(ip_parse_ipv4("255.255.255.255", o) == true);

    CHECK(ip_parse_ipv4("256.1.1.1", o) == false);   /* octet > 255 */
    CHECK(ip_parse_ipv4("1.2.3", o) == false);        /* too few parts */
    CHECK(ip_parse_ipv4("1.2.3.4.5", o) == false);     /* too many parts */
    CHECK(ip_parse_ipv4("1.2.3.", o) == false);       /* trailing dot */
    CHECK(ip_parse_ipv4(".1.2.3", o) == false);       /* leading dot */
    CHECK(ip_parse_ipv4("abc.def.ghi.jkl", o) == false);
    CHECK(ip_parse_ipv4("", o) == false);
    CHECK(ip_parse_ipv4((const char *) 0, o) == false);
}

static void test_ip_subnet(void)
{
    section("ip_equal / ip_same_subnet");
    uint8_t a[4] = { 192, 168, 1, 10 }, b[4] = { 192, 168, 1, 20 },
            c[4] = { 10, 0, 0, 5 }, m[4] = { 255, 255, 255, 0 };
    CHECK(ip_equal(a, a) == true);
    CHECK(ip_equal(a, b) == false);
    CHECK(ip_same_subnet(a, b, m) == true);
    CHECK(ip_same_subnet(a, c, m) == false);

    uint8_t m2[4] = { 255, 255, 0, 0 };
    CHECK(ip_same_subnet(a, c, m2) == false);
    uint8_t d[4] = { 192, 168, 50, 9 };
    CHECK(ip_same_subnet(a, d, m2) == true);
}

static void test_ip_text(void)
{
    section("ip_to_text round-trip");
    char buf[16];
    uint8_t ip[4] = { 10, 0, 2, 255 };
    ip_to_text(ip, buf);
    CHECK(strcmp(buf, "10.0.2.255") == 0);

    uint8_t back[4];
    CHECK(ip_parse_ipv4(buf, back) == true);
    CHECK(ip_equal(back, ip) == true);
}

static void test_ip_write_header(void)
{
    section("ip_write_header");
    uint8_t hdr[20];
    uint8_t src[4] = { 10, 0, 0, 1 }, dst[4] = { 10, 0, 0, 2 };
    ip_write_header(hdr, 6, src, dst, 8, 0x1234);
    CHECK(hdr[0] == 0x45);                 /* version + IHL */
    CHECK(hdr[8] == 64);                   /* TTL */
    CHECK(hdr[9] == 6);                    /* proto TCP */
    CHECK(hdr[12] == 10 && hdr[13] == 0 && hdr[14] == 0 && hdr[15] == 1);
    CHECK(hdr[16] == 10 && hdr[19] == 2);
    /* total length = 20 + 8 */
    CHECK(ip_get16(hdr + 2) == 28);
    /* checksum validates to zero over the 20-byte header */
    CHECK(ip_checksum(hdr, 20) == 0);
}

/* ============================================================
 *  TCP state machine
 *
 *  The host-local IP configured by stubs_ext.c is 192.168.1.50.
 * ============================================================ */
static const uint8_t MY_IP[4]     = { 192, 168, 1, 50 };
static const uint8_t SERVER_IP[4] = { 203, 0, 113, 5 };

/* Build a raw Ethernet+IPv4+TCP frame in out (>= 54 bytes) and fill both
 * the IP and TCP checksums so tcp_handle_ipv4() accepts it. */
static void build_tcp_frame(uint8_t *out,
                            uint16_t src_port, uint16_t dst_port,
                            uint32_t seq, uint32_t ack, uint8_t flags,
                            const uint8_t *data, uint16_t data_len)
{
    uint8_t *ip = out + 14;
    uint8_t *tcp = ip + 20;
    uint16_t tcp_len = 20 + data_len;

    memset(out, 0, 54 + data_len);

    /* IP header */
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 6;
    ip_put16(ip + 2, (uint16_t) (20 + tcp_len));
    memcpy(ip + 12, SERVER_IP, 4);
    memcpy(ip + 16, MY_IP, 4);
    ip_put16(ip + 10, 0);
    ip_put16(ip + 10, ip_checksum(ip, 20));

    /* TCP header */
    ip_put16(tcp + 0, src_port);
    ip_put16(tcp + 2, dst_port);
    tcp[4] = (uint8_t) (seq >> 24); tcp[5] = (uint8_t) (seq >> 16);
    tcp[6] = (uint8_t) (seq >> 8);  tcp[7] = (uint8_t) seq;
    tcp[8] = (uint8_t) (ack >> 24); tcp[9] = (uint8_t) (ack >> 16);
    tcp[10] = (uint8_t) (ack >> 8); tcp[11] = (uint8_t) ack;
    tcp[12] = 0x50;
    tcp[13] = flags;
    ip_put16(tcp + 14, 65535);
    if (data_len > 0) {
        memcpy(tcp + 20, data, data_len);
    }
    ip_put16(tcp + 16, 0);
    ip_put16(tcp + 16, tcp_checksum(SERVER_IP, MY_IP, tcp, tcp_len));
}

static void test_tcp_connect_args(void)
{
    section("tcp_connect argument validation");
    tcp_init();
    CHECK(tcp_connect(SERVER_IP, 80, 0) > 0);     /* ephemeral port assigned */
    CHECK(tcp_connect((const uint8_t *) 0, 80, 0) == -1);   /* NULL remote */
    CHECK(tcp_connect(SERVER_IP, 0, 0) == -1);              /* port 0 */
    /* explicit local port already in use -> -1 */
    int32_t h = tcp_connect(SERVER_IP, 8080, 40000);
    CHECK(h > 0);
    CHECK(tcp_connect(SERVER_IP, 9090, 40000) == -1);        /* same local port */
}

static void test_tcp_three_way_handshake(void)
{
    section("TCP: SYN -> SYN/ACK -> ESTABLISHED");
    tcp_init();
    int32_t h = tcp_connect(SERVER_IP, 80, 45000);
    CHECK(h > 0);
    CHECK(tcp_is_connected(h) == false);          /* still SYN_SENT */

    tcp_connection_t *conn = &g_connections[h - 1];
    CHECK(conn->state == TCP_STATE_SYN_SENT);

    /* Server replies SYN|ACK: ack must equal our seq (ISN+1), seq = server ISN */
    uint8_t frame[54];
    build_tcp_frame(frame, 80, 45000, 0x1000u, conn->seq_num,
                    TCP_FLAG_SYN | TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, sizeof(frame));

    CHECK(conn->state == TCP_STATE_ESTABLISHED);
    CHECK(tcp_is_connected(h) == true);
    /* our ack advanced past the server's SYN */
    CHECK(conn->ack_num == 0x1001u);
}

static void test_tcp_data_echo(void)
{
    section("TCP: inbound data buffered, tcp_recv reads it");
    tcp_init();
    int32_t h = tcp_connect(SERVER_IP, 80, 45001);
    tcp_connection_t *conn = &g_connections[h - 1];
    uint8_t frame[54 + 12];
    build_tcp_frame(frame, 80, 45001, 0x2000u, conn->seq_num,
                    TCP_FLAG_SYN | TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->state == TCP_STATE_ESTABLISHED);

    /* send 12 bytes of PSH|ACK data */
    const uint8_t payload[12] = { 'h','e','l','l','o',' ','w','o','r','l','d','\n' };
    build_tcp_frame(frame, 80, 45001, conn->ack_num, conn->seq_num,
                    TCP_FLAG_PSH | TCP_FLAG_ACK, payload, 12);
    tcp_handle_ipv4(frame, sizeof(frame));
    CHECK(tcp_has_data(h) == true);

    uint8_t rbuf[16];
    int32_t n = tcp_recv(h, rbuf, sizeof(rbuf));
    CHECK(n == 12);
    CHECK(memcmp(rbuf, payload, 12) == 0);
    CHECK(tcp_has_data(h) == false);
    CHECK(conn->ack_num == 0x200Du);   /* server seq advanced past data */
}

static void test_tcp_close_sequence(void)
{
    section("TCP: ESTABLISHED -> close -> FIN_WAIT -> closed");
    tcp_init();
    int32_t h = tcp_connect(SERVER_IP, 80, 45002);
    tcp_connection_t *conn = &g_connections[h - 1];
    uint8_t frame[54];
    build_tcp_frame(frame, 80, 45002, 0x3000u, conn->seq_num,
                    TCP_FLAG_SYN | TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->state == TCP_STATE_ESTABLISHED);

    /* active close: ESTABLISHED -> FIN_WAIT_1 (FIN consumes one seq) */
    CHECK(tcp_close(h) == true);
    CHECK(conn->state == TCP_STATE_FIN_WAIT_1);

    /* server ACKs our FIN: ack must equal our post-FIN seq_num */
    build_tcp_frame(frame, 80, 45002, conn->ack_num, conn->seq_num,
                    TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->state == TCP_STATE_FIN_WAIT_2);

    /* server FINs us -> connection fully closed (memset => used=false) */
    build_tcp_frame(frame, 80, 45002, conn->ack_num, conn->seq_num,
                    TCP_FLAG_FIN | TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->used == false);
    CHECK(tcp_is_connected(h) == false);
}

static void test_tcp_reset(void)
{
    section("TCP: inbound RST resets the connection");
    tcp_init();
    int32_t h = tcp_connect(SERVER_IP, 80, 45003);
    tcp_connection_t *conn = &g_connections[h - 1];
    uint8_t frame[54];
    build_tcp_frame(frame, 80, 45003, 0x4000u, conn->seq_num,
                    TCP_FLAG_SYN | TCP_FLAG_ACK, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->state == TCP_STATE_ESTABLISHED);

    build_tcp_frame(frame, 80, 45003, conn->ack_num, conn->seq_num,
                    TCP_FLAG_RST, (const uint8_t *) 0, 0);
    tcp_handle_ipv4(frame, 54);
    CHECK(conn->used == false);
    CHECK(tcp_recv(h, (uint8_t *) 0, 16) == -1);   /* dead handle */
}

static void test_tcp_send_recv_guards(void)
{
    section("tcp_send / tcp_recv guard clauses");
    tcp_init();
    uint8_t buf[8];
    CHECK(tcp_send(99, buf, 4) == -1);          /* bad handle */
    CHECK(tcp_recv(99, buf, sizeof(buf)) == -1); /* bad handle */

    int32_t h = tcp_connect(SERVER_IP, 80, 45004);
    /* not ESTABLISHED yet -> send rejected */
    CHECK(tcp_send(h, (const uint8_t *) "x", 1) == -1);
    CHECK(tcp_recv(h, buf, 0) == -1);           /* zero size */
    CHECK(tcp_has_data(h) == false);
}

/* ============================================================
 *  DNS
 * ============================================================ */
static void test_dns_fast_paths(void)
{
    section("DNS: literal IP and localhost fast paths");
    uint8_t out[4];
    CHECK(dns_resolve_ipv4("93.184.216.34", out) == true);
    CHECK(out[0] == 93 && out[3] == 34);

    CHECK(dns_resolve_ipv4("localhost", out) == true);
    CHECK(out[0] == 127 && out[1] == 0 && out[2] == 0 && out[3] == 1);

    CHECK(dns_resolve_ipv4("loopback", out) == true);
    CHECK(out[0] == 127 && out[3] == 1);
}

static void test_dns_bad_names(void)
{
    section("DNS: invalid names rejected before any wire traffic");
    uint8_t out[4];
    CHECK(dns_resolve_ipv4((const char *) 0, out) == false);
    CHECK(dns_resolve_ipv4("", out) == false);
    CHECK(dns_resolve_ipv4("bad name.com", out) == false);   /* space */
    CHECK(dns_resolve_ipv4("bad_name.com", out) == false);   /* underscore */
    CHECK(dns_resolve_ipv4("-lead.com", out) == false);      /* label rule */
    CHECK(dns_resolve_ipv4("a..b.com", out) == false);       /* empty label */
    CHECK(dns_resolve_ipv4(".com", out) == false);          /* empty first label */
}

/* Build a minimal DNS answer packet for g_dns_query_name with one A RR. */
static uint16_t dns_u16(uint8_t *p) { return (uint16_t) (p[0] << 8) | p[1]; }

static void test_dns_response_parse(void)
{
    section("DNS: response parsing + compression pointer + A record");
    dns_init();

    /* prime the pending-query state the way dns_resolve_ipv4 leaves it */
    g_dns_waiting = true;
    g_dns_answer_valid = false;
    g_dns_query_id = 0x1234;
    strcpy(g_dns_query_name, "example.com");
    g_dns_server_ip[0] = 8; g_dns_server_ip[1] = 8;
    g_dns_server_ip[2] = 8; g_dns_server_ip[3] = 8;

    /* packet: header(12) + question (example.com) + answer */
    uint8_t pkt[256];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x12; pkt[1] = 0x34;          /* id matches */
    pkt[2] = 0x81; pkt[3] = 0x80;          /* response, no error */
    pkt[4] = 0; pkt[5] = 1;                /* qdcount=1 */
    pkt[6] = 0; pkt[7] = 1;                /* ancount=1 */
    uint16_t pos = 12;
    /* question name: 7 "example" 3 "com" 0 */
    pkt[pos++] = 7; memcpy(pkt + pos, "example", 7); pos += 7;
    pkt[pos++] = 3; memcpy(pkt + pos, "com", 3); pos += 3;
    pkt[pos++] = 0;
    pkt[pos++] = 0; pkt[pos++] = 1;        /* QTYPE A */
    pkt[pos++] = 0; pkt[pos++] = 1;        /* QCLASS IN */
    /* answer name: compression pointer to offset 12 */
    pkt[pos++] = 0xC0; pkt[pos++] = 0x0C;
    pkt[pos++] = 0; pkt[pos++] = 1;        /* TYPE A */
    pkt[pos++] = 0; pkt[pos++] = 1;        /* CLASS IN */
    pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 0; pkt[pos++] = 60; /* TTL */
    pkt[pos++] = 0; pkt[pos++] = 4;        /* RDLENGTH 4 */
    pkt[pos++] = 93; pkt[pos++] = 184; pkt[pos++] = 216; pkt[pos++] = 34;

    uint8_t srv[4] = { 8, 8, 8, 8 };
    dns_handle_udp(srv, 53, pkt, pos);

    CHECK(g_dns_answer_valid == true);
    CHECK(g_dns_answer_ip[0] == 93 && g_dns_answer_ip[3] == 34);
}

static void test_dns_reject_wrong_id(void)
{
    section("DNS: answer with wrong txid is ignored");
    dns_init();
    g_dns_waiting = true;
    g_dns_answer_valid = false;
    g_dns_query_id = 0xAAAA;
    strcpy(g_dns_query_name, "example.com");
    g_dns_server_ip[0] = 8; g_dns_server_ip[1] = 8;
    g_dns_server_ip[2] = 8; g_dns_server_ip[3] = 8;

    uint8_t pkt[64];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x99; pkt[1] = 0x99;          /* wrong id */
    pkt[2] = 0x81; pkt[3] = 0x80;
    uint8_t srv[4] = { 8, 8, 8, 8 };
    dns_handle_udp(srv, 53, pkt, 12);
    /* a mismatched transaction id is silently discarded: no answer, and
     * the pending query is left waiting (a later correct reply may arrive). */
    CHECK(g_dns_answer_valid == false);
    CHECK(g_dns_waiting == true);
    (void) dns_u16;
}

/* ============================================================ */
int main(void)
{
    printf("Monios network host regression test\n");

    test_ip_order();
    test_ip_checksum();
    test_ip_parse();
    test_ip_subnet();
    test_ip_text();
    test_ip_write_header();

    test_tcp_connect_args();
    test_tcp_three_way_handshake();
    test_tcp_data_echo();
    test_tcp_close_sequence();
    test_tcp_reset();
    test_tcp_send_recv_guards();

    test_dns_fast_paths();
    test_dns_bad_names();
    test_dns_response_parse();
    test_dns_reject_wrong_id();

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
