#ifndef _IP_H_
#define _IP_H_

#include "stdbool.h"
#include "stdint.h"

void ip_put16(uint8_t *data, uint16_t value);
uint16_t ip_get16(const uint8_t *data);
uint16_t ip_checksum(const uint8_t *data, uint32_t size);
bool ip_parse_ipv4(const char *text, uint8_t out[4]);
bool ip_equal(const uint8_t a[4], const uint8_t b[4]);
bool ip_same_subnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]);
void ip_to_text(const uint8_t ip[4], char *out);
void ip_write_header(uint8_t *ip, uint8_t proto, const uint8_t src[4], const uint8_t dst[4], uint16_t payload_length, uint16_t ident);

#endif
