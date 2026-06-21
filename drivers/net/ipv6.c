#include "common.h"
#include "ipv6.h"

static ipv6_info_t g_ipv6_info;

void ipv6_init(void)
{
    uint8_t link_local[16];

    memset(&g_ipv6_info, 0, sizeof(g_ipv6_info));
    g_ipv6_info.initialized = true;
    g_ipv6_info.parser_ready = true;
    g_ipv6_info.address_tools_ready = true;
    ipv6_make_link_local(NULL, link_local);
    ipv6_to_text(link_local, g_ipv6_info.link_local, sizeof(g_ipv6_info.link_local));
    strcpy(g_ipv6_info.status, "ipv6: parser/address tools ready");
}

static int8_t ipv6_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return (int8_t) (ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (int8_t) (ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (int8_t) (ch - 'A' + 10);
    }
    return -1;
}

bool ipv6_parse(const char *text, uint8_t out[16])
{
    uint16_t words[8];
    int32_t compress_at = -1;
    uint32_t word_count = 0;
    const char *p = text;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    memset(words, 0, sizeof(words));

    if (p[0] == ':' && p[1] == ':') {
        compress_at = 0;
        p += 2;
        if (*p == '\0') {
            word_count = 0;
        }
    } else if (p[0] == ':') {
        return false;
    }

    while (*p != '\0') {
        uint32_t value = 0;
        uint32_t digits = 0;

        if (word_count >= 8) {
            return false;
        }
        while (*p != '\0' && *p != ':') {
            int8_t hex = ipv6_hex_value(*p);

            if (hex < 0 || digits >= 4) {
                return false;
            }
            value = (value << 4) | (uint32_t) hex;
            digits++;
            p++;
        }
        if (digits == 0) {
            return false;
        }
        words[word_count++] = (uint16_t) value;

        if (*p == '\0') {
            break;
        }
        p++;
        if (*p == ':') {
            if (compress_at >= 0) {
                return false;
            }
            compress_at = (int32_t) word_count;
            p++;
            if (*p == '\0') {
                break;
            }
        } else if (*p == '\0') {
            return false;
        }
    }

    if (compress_at >= 0) {
        uint32_t zeros;

        if (word_count > 8) {
            return false;
        }
        zeros = 8 - word_count;
        for (int32_t i = (int32_t) word_count - 1; i >= compress_at; i--) {
            words[i + zeros] = words[i];
        }
        for (uint32_t i = (uint32_t) compress_at; i < (uint32_t) compress_at + zeros; i++) {
            words[i] = 0;
        }
    } else if (word_count != 8) {
        return false;
    }

    for (uint32_t i = 0; i < 8; i++) {
        out[i * 2] = (uint8_t) (words[i] >> 8);
        out[i * 2 + 1] = (uint8_t) words[i];
    }
    g_ipv6_info.parsed_count++;
    return true;
}

static void ipv6_append_char(char *out, uint32_t out_size, uint32_t *pos, char ch)
{
    if (*pos + 1 < out_size) {
        out[*pos] = ch;
        (*pos)++;
        out[*pos] = '\0';
    }
}

static void ipv6_append_hex(char *out, uint32_t out_size, uint32_t *pos, uint16_t value)
{
    static const char hex[] = "0123456789abcdef";
    bool started = false;

    for (int32_t shift = 12; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t) ((value >> shift) & 0xF);

        if (nibble != 0 || started || shift == 0) {
            ipv6_append_char(out, out_size, pos, hex[nibble]);
            started = true;
        }
    }
}

void ipv6_to_text(const uint8_t ip[16], char *out, uint32_t out_size)
{
    uint16_t words[8];
    int32_t best_start = -1;
    uint32_t best_len = 0;
    uint32_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (ip == NULL) {
        return;
    }

    for (uint32_t i = 0; i < 8; i++) {
        words[i] = ((uint16_t) ip[i * 2] << 8) | ip[i * 2 + 1];
    }

    for (uint32_t i = 0; i < 8; i++) {
        uint32_t len = 0;

        while (i + len < 8 && words[i + len] == 0) {
            len++;
        }
        if (len >= 2 && len > best_len) {
            best_start = (int32_t) i;
            best_len = len;
        }
        if (len > 0) {
            i += len - 1;
        }
    }

    for (uint32_t i = 0; i < 8; i++) {
        if (best_start >= 0 && i == (uint32_t) best_start) {
            ipv6_append_char(out, out_size, &pos, ':');
            ipv6_append_char(out, out_size, &pos, ':');
            i += best_len - 1;
            continue;
        }
        if (i > 0 && !(best_start >= 0 && i == (uint32_t) best_start + best_len)) {
            ipv6_append_char(out, out_size, &pos, ':');
        }
        ipv6_append_hex(out, out_size, &pos, words[i]);
    }
}

bool ipv6_is_unspecified(const uint8_t ip[16])
{
    if (ip == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < 16; i++) {
        if (ip[i] != 0) {
            return false;
        }
    }
    return true;
}

bool ipv6_is_loopback(const uint8_t ip[16])
{
    if (ip == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < 15; i++) {
        if (ip[i] != 0) {
            return false;
        }
    }
    return ip[15] == 1;
}

bool ipv6_is_link_local(const uint8_t ip[16])
{
    return ip != NULL && ip[0] == 0xFE && (ip[1] & 0xC0) == 0x80;
}

bool ipv6_is_multicast(const uint8_t ip[16])
{
    return ip != NULL && ip[0] == 0xFF;
}

bool ipv6_is_unique_local(const uint8_t ip[16])
{
    return ip != NULL && (ip[0] & 0xFE) == 0xFC;
}

bool ipv6_is_global_unicast(const uint8_t ip[16])
{
    return ip != NULL && (ip[0] & 0xE0) == 0x20;
}

bool ipv6_prefix_match(const uint8_t a[16], const uint8_t b[16], uint8_t prefix_bits)
{
    uint8_t full_bytes;
    uint8_t partial_bits;
    uint8_t mask;

    if (a == NULL || b == NULL || prefix_bits > 128) {
        return false;
    }
    full_bytes = (uint8_t) (prefix_bits / 8);
    partial_bits = (uint8_t) (prefix_bits % 8);
    if (full_bytes > 0 && memcmp(a, b, full_bytes) != 0) {
        return false;
    }
    if (partial_bits == 0) {
        return true;
    }
    mask = (uint8_t) (0xFFu << (8 - partial_bits));
    return (a[full_bytes] & mask) == (b[full_bytes] & mask);
}

void ipv6_make_link_local(const uint8_t mac[6], uint8_t out[16])
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, 16);
    out[0] = 0xFE;
    out[1] = 0x80;
    if (mac == NULL) {
        return;
    }
    out[8] = mac[0] ^ 0x02;
    out[9] = mac[1];
    out[10] = mac[2];
    out[11] = 0xFF;
    out[12] = 0xFE;
    out[13] = mac[3];
    out[14] = mac[4];
    out[15] = mac[5];
    ipv6_to_text(out, g_ipv6_info.link_local, sizeof(g_ipv6_info.link_local));
}

const ipv6_info_t *ipv6_info(void)
{
    return &g_ipv6_info;
}

const char *ipv6_status(void)
{
    return g_ipv6_info.status;
}
