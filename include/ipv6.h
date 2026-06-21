#ifndef _IPV6_H_
#define _IPV6_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool parser_ready;
    bool address_tools_ready;
    uint32_t parsed_count;
    char link_local[48];
    char status[64];
} ipv6_info_t;

void ipv6_init(void);
bool ipv6_parse(const char *text, uint8_t out[16]);
void ipv6_to_text(const uint8_t ip[16], char *out, uint32_t out_size);
bool ipv6_is_unspecified(const uint8_t ip[16]);
bool ipv6_is_loopback(const uint8_t ip[16]);
bool ipv6_is_link_local(const uint8_t ip[16]);
bool ipv6_is_multicast(const uint8_t ip[16]);
bool ipv6_is_unique_local(const uint8_t ip[16]);
bool ipv6_is_global_unicast(const uint8_t ip[16]);
bool ipv6_prefix_match(const uint8_t a[16], const uint8_t b[16], uint8_t prefix_bits);
void ipv6_make_link_local(const uint8_t mac[6], uint8_t out[16]);
const ipv6_info_t *ipv6_info(void);
const char *ipv6_status(void);

#endif
