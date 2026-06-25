#ifndef _IPV4_H_
#define _IPV4_H_

#include "stdbool.h"
#include "stdint.h"

bool ipv4_parse(const char *text, uint8_t out[4]);
void ipv4_to_text(const uint8_t ip[4], char *out);
bool ipv4_is_loopback(const uint8_t ip[4]);
bool ipv4_is_private(const uint8_t ip[4]);
bool ipv4_is_link_local(const uint8_t ip[4]);
bool ipv4_is_multicast(const uint8_t ip[4]);

#endif
