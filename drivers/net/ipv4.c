#include "common.h"
#include "ip.h"
#include "ipv4.h"

bool ipv4_parse(const char *text, uint8_t out[4])
{
    return ip_parse_ipv4(text, out);
}

void ipv4_to_text(const uint8_t ip[4], char *out)
{
    ip_to_text(ip, out);
}

bool ipv4_is_loopback(const uint8_t ip[4])
{
    return ip != NULL && ip[0] == 127;
}

bool ipv4_is_private(const uint8_t ip[4])
{
    if (ip == NULL) {
        return false;
    }
    return ip[0] == 10 ||
           (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
           (ip[0] == 192 && ip[1] == 168);
}

bool ipv4_is_link_local(const uint8_t ip[4])
{
    return ip != NULL && ip[0] == 169 && ip[1] == 254;
}

bool ipv4_is_multicast(const uint8_t ip[4])
{
    return ip != NULL && ip[0] >= 224 && ip[0] <= 239;
}
