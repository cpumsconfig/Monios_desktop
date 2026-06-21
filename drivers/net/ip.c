#include "common.h"
#include "ip.h"

void ip_put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) (value >> 8);
    data[1] = (uint8_t) value;
}

uint16_t ip_get16(const uint8_t *data)
{
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

uint16_t ip_checksum(const uint8_t *data, uint32_t size)
{
    uint32_t sum = 0;

    while (size > 1) {
        sum += ip_get16(data);
        data += 2;
        size -= 2;
    }
    if (size != 0) {
        sum += (uint16_t) data[0] << 8;
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t) ~sum;
}

bool ip_parse_ipv4(const char *text, uint8_t out[4])
{
    uint32_t part = 0;
    uint32_t index = 0;
    bool have_digit = false;

    if (text == NULL || text[0] == '\0') {
        return false;
    }

    while (*text != '\0') {
        char ch = *text++;

        if (ch >= '0' && ch <= '9') {
            part = part * 10u + (uint32_t) (ch - '0');
            if (part > 255) {
                return false;
            }
            have_digit = true;
            continue;
        }
        if (ch == '.' && have_digit && index < 3) {
            out[index++] = (uint8_t) part;
            part = 0;
            have_digit = false;
            continue;
        }
        return false;
    }
    if (!have_digit || index != 3) {
        return false;
    }
    out[index] = (uint8_t) part;
    return true;
}

bool ip_equal(const uint8_t a[4], const uint8_t b[4])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

bool ip_same_subnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4])
{
    for (uint32_t i = 0; i < 4; i++) {
        if ((a[i] & mask[i]) != (b[i] & mask[i])) {
            return false;
        }
    }
    return true;
}

static void ip_append_dec(char *out, uint8_t value)
{
    char temp[3];
    uint32_t pos = 0;

    if (value >= 100) {
        *out++ = (char) ('0' + value / 100);
        value %= 100;
        *out++ = (char) ('0' + value / 10);
        value %= 10;
        *out++ = (char) ('0' + value);
        *out = '\0';
        return;
    }
    if (value >= 10) {
        *out++ = (char) ('0' + value / 10);
        value %= 10;
    }
    temp[pos++] = (char) ('0' + value);
    temp[pos] = '\0';
    strcpy(out, temp);
}

void ip_to_text(const uint8_t ip[4], char *out)
{
    uint32_t pos = 0;

    for (uint32_t i = 0; i < 4; i++) {
        char part[4];
        ip_append_dec(part, ip[i]);
        strcpy(out + pos, part);
        pos += (uint32_t) strlen(part);
        if (i < 3) {
            out[pos++] = '.';
            out[pos] = '\0';
        }
    }
}

void ip_write_header(uint8_t *ip, uint8_t proto, const uint8_t src[4], const uint8_t dst[4], uint16_t payload_length, uint16_t ident)
{
    uint16_t total = (uint16_t) (20 + payload_length);

    ip[0] = 0x45;
    ip[1] = 0;
    ip_put16(ip + 2, total);
    ip_put16(ip + 4, ident);
    ip_put16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, src, 4);
    memcpy(ip + 16, dst, 4);
    ip_put16(ip + 10, 0);
    ip_put16(ip + 10, ip_checksum(ip, 20));
}
