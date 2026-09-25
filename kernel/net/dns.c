#include "common.h"
#include "cpu.h"
#include "dns.h"
#include "ip.h"
#include "ipv4.h"
#include "kernel.h"
#include "net.h"

#define DNS_PORT             53
#define DNS_CLIENT_PORT      53000
#define DNS_TYPE_A           1
#define DNS_TYPE_CNAME       5
#define DNS_CLASS_IN         1
#define DNS_MAX_PACKET       512
#define DNS_MAX_NAME         64
#define DNS_CACHE_SIZE       16
#define DNS_WAIT_LOOPS       600000U

typedef struct {
    char     name[DNS_MAX_NAME];   /* lower-cased lookup key */
    uint8_t  ip[4];
    uint64_t expiry;               /* timer_ticks() at which entry dies */
    bool     valid;
} dns_cache_entry_t;

static char   g_dns_status[64];
static bool   g_dns_waiting;
static bool   g_dns_answer_valid;
static uint16_t g_dns_query_id;
static char   g_dns_query_name[DNS_MAX_NAME];
static uint8_t g_dns_answer_ip[4];
static uint32_t g_dns_answer_ttl;
static uint8_t g_dns_server_ip[4];      /* server we are currently talking to */
static char   g_dns_cname[DNS_MAX_NAME]; /* set when answer chain ended in CNAME */
static dns_cache_entry_t g_cache[DNS_CACHE_SIZE];
static uint32_t g_cache_next;

static uint16_t dns_get16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static uint32_t dns_get32(const uint8_t *data)
{
    return ((uint32_t) data[0] << 24) | ((uint32_t) data[1] << 16) |
           ((uint32_t) data[2] << 8)  |  (uint32_t) data[3];
}

static void dns_put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

static char dns_lower(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char) (ch - 'A' + 'a');
    }
    return ch;
}

static void dns_str_lower(char *dst, const char *src)
{
    while (*src != '\0') {
        *dst++ = dns_lower(*src++);
    }
    *dst = '\0';
}

static bool dns_valid_name(const char *name)
{
    uint32_t label_len = 0;
    uint32_t total_len = 0;

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    while (*name != '\0') {
        char ch = *name++;

        total_len++;
        if (total_len >= DNS_MAX_NAME) {
            return false;
        }
        if (ch == '.') {
            if (label_len == 0 || label_len > 63) {
                return false;
            }
            label_len = 0;
            continue;
        }
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '-')) {
            return false;
        }
        label_len++;
    }
    return label_len > 0 && label_len <= 63;
}

static bool dns_encode_name(uint8_t *packet, uint16_t *pos, uint16_t max, const char *name)
{
    const char *label = name;
    const char *p = name;

    while (1) {
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

/* Decode a (possibly compressed) domain name at *pos into out (dotted,
 * lower-cased). Advances *pos past the name unless a compression jump
 * was taken (then it points just after the first compression pointer). */
static bool dns_decode_name(const uint8_t *packet, uint16_t length,
                            uint16_t *pos, char *out, uint32_t out_max)
{
    uint16_t cursor = *pos;
    uint32_t olen = 0;
    uint32_t hops = 0;
    bool jumped = false;

    out[0] = '\0';
    while (cursor < length) {
        uint8_t len = packet[cursor++];

        if (len == 0) {
            if (!jumped) {
                *pos = cursor;
            }
            out[olen] = '\0';
            return true;
        }
        if ((len & 0xC0) == 0xC0) {
            uint16_t target;

            if (cursor >= length || ++hops > 16) {
                return false;
            }
            target = (uint16_t) ((((uint16_t) len & 0x3Fu) << 8) | packet[cursor++]);
            if (target >= length) {
                return false;
            }
            if (!jumped) {
                /* A compression pointer terminates the encoded name at the
                 * original location.  Preserve the byte after the pointer so
                 * callers can continue with QTYPE/RR metadata after decoding
                 * the pointed-to labels. */
                *pos = cursor;
                jumped = true;
            }
            cursor = target;
            continue;
        }
        if (len > 63 || (uint32_t) cursor + len > length) {
            return false;
        }
        if (olen + (olen > 0 ? 1u : 0u) + len >= out_max) return false;
        if (olen > 0) {
            out[olen++] = '.';
        }
        for (uint8_t i = 0; i < len; i++) {
            /* Embedded separators/NUL must not alias a dotted C string. */
            if (packet[cursor + i] == 0 || packet[cursor + i] == '.') return false;
            out[olen++] = dns_lower((char) packet[cursor + i]);
        }
        cursor = (uint16_t) (cursor + len);
    }
    return false;
}

/* ---------------- cache ---------------- */

static bool dns_cache_lookup(const char *name, uint8_t out[4])
{
    char key[DNS_MAX_NAME];

    dns_str_lower(key, name);
    for (uint32_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_cache[i].valid && timer_ticks() < g_cache[i].expiry &&
            strcmp(g_cache[i].name, key) == 0) {
            memcpy(out, g_cache[i].ip, 4);
            return true;
        }
    }
    return false;
}

static void dns_cache_insert(const char *name, const uint8_t ip[4], uint32_t ttl)
{
    uint32_t hz = timer_hz();
    uint32_t slot = g_cache_next;

    g_cache_next = (g_cache_next + 1) % DNS_CACHE_SIZE;
    dns_str_lower(g_cache[slot].name, name);
    memcpy(g_cache[slot].ip, ip, 4);
    if (ttl == 0) return;
    if (ttl > 86400u) {
        ttl = 300;
    }
    g_cache[slot].expiry = timer_ticks() + (uint64_t) ttl * (hz == 0 ? 100u : hz);
    g_cache[slot].valid = true;
}

/* ---------------- query engine ---------------- */

static bool dns_send_query(const char *name, const uint8_t server[4])
{
    uint8_t packet[DNS_MAX_PACKET];
    uint16_t pos = 12;

    memset(packet, 0, sizeof(packet));
    g_dns_query_id++;
    dns_put16(packet + 0, g_dns_query_id);
    dns_put16(packet + 2, 0x0100);   /* standard query, recursion desired */
    dns_put16(packet + 4, 1);         /* QDCOUNT */
    if (!dns_encode_name(packet, &pos, sizeof(packet), name) ||
        (uint32_t) pos + 4u > sizeof(packet)) {
        return false;
    }
    dns_put16(packet + pos, DNS_TYPE_A);
    pos += 2;
    dns_put16(packet + pos, DNS_CLASS_IN);
    pos += 2;

    memcpy(g_dns_server_ip, server, 4);
    strcpy(g_dns_query_name, name);
    g_dns_waiting = true;
    g_dns_answer_valid = false;
    g_dns_cname[0] = '\0';
    return net_udp_send_to(server, DNS_CLIENT_PORT, DNS_PORT, packet, pos);
}

static bool dns_wait_answer(void)
{
    for (uint32_t i = 0; i < DNS_WAIT_LOOPS && g_dns_waiting; i++) {
        net_update();
        io_wait();
    }
    g_dns_waiting = false;
    return g_dns_answer_valid;
}

void dns_init(void)
{
    g_dns_waiting = false;
    g_dns_answer_valid = false;
    g_dns_query_id = (uint16_t) (cpu_read_tsc() ^ (cpu_read_tsc() >> 17) ^ 0x4D00u);
    if (g_dns_query_id == 0) {
        g_dns_query_id = 0x4D00;
    }
    g_dns_query_name[0] = '\0';
    g_dns_cname[0] = '\0';
    memset(g_dns_server_ip, 0, sizeof(g_dns_server_ip));
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_next = 0;
    strcpy(g_dns_status, "dns: ready");
}

bool dns_resolve_ipv4(const char *name, uint8_t out[4])
{
    uint32_t srv_count;

    if (out == NULL || name == NULL) {
        return false;
    }
    if (ipv4_parse(name, out)) {
        strcpy(g_dns_status, "dns: literal ipv4");
        return true;
    }
    if (strcmp(name, "localhost") == 0 || strcmp(name, "loopback") == 0) {
        out[0] = 127;
        out[1] = 0;
        out[2] = 0;
        out[3] = 1;
        strcpy(g_dns_status, "dns: localhost");
        return true;
    }
    if (!dns_valid_name(name)) {
        strcpy(g_dns_status, "dns: bad name");
        return false;
    }

    /* 1) cache */
    if (dns_cache_lookup(name, out)) {
        strcpy(g_dns_status, "dns: cache hit ");
        ip_to_text(out, g_dns_status + strlen(g_dns_status));
        log_write(g_dns_status);
        return true;
    }

    srv_count = net_dns_server_count();
    if (srv_count == 0) {
        strcpy(g_dns_status, "dns: no server");
        return false;
    }

    /* 2) try each configured server (fallback on timeout) */
    for (uint32_t srv = 0; srv < srv_count; srv++) {
        uint8_t server[4];
        const char *target = name;
        bool got;

        if (!net_get_dns_server(srv, server)) {
            continue;
        }
        /* Follow up to two CNAME hops on this server. */
        for (uint32_t hop = 0; hop < 2; hop++) {
            if (!dns_send_query(target, server)) {
                break;
            }
            got = dns_wait_answer();
            if (!got) {
                break;
            }
            if (g_dns_cname[0] != '\0') {
                target = g_dns_cname;      /* continue chain next hop */
                continue;
            }
            memcpy(out, g_dns_answer_ip, 4);
            dns_cache_insert(name, out, g_dns_answer_ttl);
            strcpy(g_dns_status, "dns: resolved ");
            ip_to_text(out, g_dns_status + strlen(g_dns_status));
            log_write(g_dns_status);
            return true;
        }
    }

    strcpy(g_dns_status, "dns: all servers failed");
    log_write(g_dns_status);
    return false;
}

void dns_handle_udp(const uint8_t src_ip[4], uint16_t src_port,
                    const uint8_t *payload, uint16_t length)
{
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t pos;
    char desired[DNS_MAX_NAME];

    if (!g_dns_waiting || src_port != DNS_PORT || src_ip == NULL ||
        payload == NULL || length < 12) {
        return;
    }
    if (!ip_equal(src_ip, g_dns_server_ip)) {
        return;
    }
    if (dns_get16(payload + 0) != g_dns_query_id) {
        return;
    }
    flags = dns_get16(payload + 2);
    if ((flags & 0x8000) == 0 || (flags & 0x7A0F) != 0) {
        g_dns_waiting = false;
        strcpy(g_dns_status, "dns: response error");
        return;
    }

    qdcount = dns_get16(payload + 4);
    ancount = dns_get16(payload + 6);
    if (qdcount != 1) {
        g_dns_waiting = false;
        return;
    }

    dns_str_lower(desired, g_dns_query_name);

    /* Validate and skip the question section.  A matching transaction ID is
     * not sufficient: rejecting a different name/type/class also prevents a
     * forged or stale response from satisfying the pending query. */
    pos = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        char qname[DNS_MAX_NAME];
        if (!dns_decode_name(payload, length, &pos, qname, sizeof(qname)) ||
            pos + 4 > length) {
            g_dns_waiting = false;
            return;
        }
        if (strcmp(qname, desired) != 0 ||
            dns_get16(payload + pos) != DNS_TYPE_A ||
            dns_get16(payload + pos + 2) != DNS_CLASS_IN) {
            g_dns_waiting = false;
            strcpy(g_dns_status, "dns: question mismatch");
            return;
        }
        pos += 4;
    }

    for (uint16_t i = 0; i < ancount; i++) {
        char owner[DNS_MAX_NAME];
        uint16_t type;
        uint16_t klass;
        uint32_t ttl;
        uint16_t rdlen;

        if (!dns_decode_name(payload, length, &pos, owner, sizeof(owner)) ||
            pos + 10 > length) {
            break;
        }
        type = dns_get16(payload + pos);
        klass = dns_get16(payload + pos + 2);
        ttl = dns_get32(payload + pos + 4);
        rdlen = dns_get16(payload + pos + 8);
        pos += 10;
        if (pos + rdlen > length) {
            break;
        }
        if (klass != DNS_CLASS_IN) {
            pos += rdlen;
            continue;
        }
        if (type == DNS_TYPE_A && rdlen == 4 &&
            strcmp(owner, desired) == 0) {
            memcpy(g_dns_answer_ip, payload + pos, 4);
            g_dns_answer_ttl = ttl;
            g_dns_cname[0] = '\0';
            g_dns_answer_valid = true;
            g_dns_waiting = false;
            return;
        }
        if (type == DNS_TYPE_CNAME && strcmp(owner, desired) == 0) {
            uint16_t rp = pos;
            char cname[DNS_MAX_NAME];
            if (dns_decode_name(payload, length, &rp, cname, sizeof(cname)) &&
                rp == pos + rdlen && cname[0] != '\0') {
                strcpy(desired, cname);          /* follow chain */
                strncpy(g_dns_cname, cname, DNS_MAX_NAME - 1);
                g_dns_cname[DNS_MAX_NAME - 1] = '\0';
            }
        }
        pos += rdlen;
    }

    /* Walked all answers. */
    g_dns_waiting = false;
    if (g_dns_cname[0] != '\0') {
        g_dns_answer_valid = true;   /* valid chain; caller follows cname */
    } else {
        strcpy(g_dns_status, "dns: no A record");
    }
}

const char *dns_status(void)
{
    return g_dns_status;
}
