#ifndef _TCP_H_
#define _TCP_H_

#include "stdbool.h"
#include "stdint.h"

void tcp_init(void);
void tcp_handle_ipv4(const uint8_t *packet, uint16_t length);

#endif
