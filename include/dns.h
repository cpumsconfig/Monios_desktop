#ifndef _DNS_H_
#define _DNS_H_

#include "stdbool.h"
#include "stdint.h"

void dns_init(void);
bool dns_resolve_ipv4(const char *name, uint8_t out[4]);
void dns_handle_udp(const uint8_t src_ip[4], uint16_t src_port, const uint8_t *payload, uint16_t length);
const char *dns_status(void);

#endif
