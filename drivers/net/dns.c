#include "common.h"
#include "dns.h"
#include "ip.h"
#include "ipv4.h"
#include "kernel.h"
#include "net.h"

#define DNS_PORT             53
#define DNS_CLIENT_PORT      53000
#define DNS_TYPE_A           1
#define DNS_CLASS_IN         1
#define DNS_MAX_PACKET       512
#define DNS_MAX_NAME         64

static char g_dns_status[64];
static bool g_dns_waiting;
static bool g_dns_answer_valid;
static uint16_t g_dns_query_id;
static char g_dns_query_name[DNS_MAX_NAME];
static uint8_t g_dns_answer_ip[4];

static uint16_t dns_get16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

static void dns_put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
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

static bool dns_skip_name(const uint8_t *packet, uint16_t length, uint16_t *pos)
{
    uint16_t cursor = *pos;
    uint32_t hops = 0;

    while (cursor < length) {
        uint8_t len = packet[cursor++];

        if (len == 0) {
            *pos = cursor;
            return true;
        }
        if ((len & 0xC0) == 0xC0) {
            if (cursor >= length || ++hops > 16) {
                return false;
            }
            cursor++;
            *pos = cursor;
            return true;
        }
        if ((len & 0xC0) != 0 || cursor + len > length) {
            return false;
        }
        cursor += len;
    }
    return false;
}

void dns_init(void)
{
    g_dns_waiting = false;
    g_dns_answer_valid = false;
    g_dns_query_id = 0x4D00;
    g_dns_query_name[0] = '\0';
    strcpy(g_dns_status, "dns: ready");
}

bool dns_resolve_ipv4(const char *name, uint8_t out[4])
{
    uint8_t server_ip[4];
    uint8_t packet[DNS_MAX_PACKET];
    uint16_t pos = 12;

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
    if (!net_get_dns_ip(server_ip)) {
        strcpy(g_dns_status, "dns: no server");
        return false;
    }

    memset(packet, 0, sizeof(packet));
    g_dns_query_id++;
    dns_put16(packet + 0, g_dns_query_id);
    dns_put16(packet + 2, 0x0100);
    dns_put16(packet + 4, 1);
    if (!dns_encode_name(packet, &pos, sizeof(packet), name) || pos + 4 > sizeof(packet)) {
        strcpy(g_dns_status, "dns: encode failed");
        return false;
    }
    dns_put16(packet + pos, DNS_TYPE_A);
    pos += 2;
    dns_put16(packet + pos, DNS_CLASS_IN);
    pos += 2;

    memset(g_dns_query_name, 0, sizeof(g_dns_query_name));
    if (strlen(name) < sizeof(g_dns_query_name)) {
        strcpy(g_dns_query_name, name);
    }
    g_dns_waiting = true;
    g_dns_answer_valid = false;
    if (!net_udp_send_to(server_ip, DNS_CLIENT_PORT, DNS_PORT, packet, pos)) {
        g_dns_waiting = false;
        strcpy(g_dns_status, "dns: query send failed");
        log_write(g_dns_status);
        return false;
    }

    strcpy(g_dns_status, "dns: query sent");
    for (uint32_t i = 0; i < 300000 && g_dns_waiting; i++) {
        net_update();
        io_wait();
    }
    if (!g_dns_answer_valid) {
        g_dns_waiting = false;
        strcpy(g_dns_status, "dns: timeout");
        log_write(g_dns_status);
        return false;
    }
    memcpy(out, g_dns_answer_ip, 4);
    strcpy(g_dns_status, "dns: resolved ");
    ip_to_text(out, g_dns_status + strlen(g_dns_status));
    log_write(g_dns_status);
    return true;
}

void dns_handle_udp(const uint8_t src_ip[4], uint16_t src_port, const uint8_t *payload, uint16_t length)
{
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t pos;

    (void) src_ip;
    if (!g_dns_waiting || src_port != DNS_PORT || payload == NULL || length < 12) {
        return;
    }
    if (dns_get16(payload + 0) != g_dns_query_id) {
        return;
    }
    flags = dns_get16(payload + 2);
    if ((flags & 0x8000) == 0 || (flags & 0x000F) != 0) {
        g_dns_waiting = false;
        strcpy(g_dns_status, "dns: response error");
        return;
    }

    qdcount = dns_get16(payload + 4);
    ancount = dns_get16(payload + 6);
    pos = 12;
    for (uint16_t i = 0; i < qdcount; i++) {
        if (!dns_skip_name(payload, length, &pos) || pos + 4 > length) {
            g_dns_waiting = false;
            strcpy(g_dns_status, "dns: bad question");
            return;
        }
        pos += 4;
    }

    for (uint16_t i = 0; i < ancount; i++) {
        uint16_t type;
        uint16_t klass;
        uint16_t rdlen;

        if (!dns_skip_name(payload, length, &pos) || pos + 10 > length) {
            break;
        }
        type = dns_get16(payload + pos);
        klass = dns_get16(payload + pos + 2);
        rdlen = dns_get16(payload + pos + 8);
        pos += 10;
        if (pos + rdlen > length) {
            break;
        }
        if (type == DNS_TYPE_A && klass == DNS_CLASS_IN && rdlen == 4) {
            memcpy(g_dns_answer_ip, payload + pos, 4);
            g_dns_answer_valid = true;
            g_dns_waiting = false;
            strcpy(g_dns_status, "dns: answer ");
            ip_to_text(g_dns_answer_ip, g_dns_status + strlen(g_dns_status));
            return;
        }
        pos += rdlen;
    }

    g_dns_waiting = false;
    strcpy(g_dns_status, "dns: no A record");
}

const char *dns_status(void)
{
    return g_dns_status;
}
