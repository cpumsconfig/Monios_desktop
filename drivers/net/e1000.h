#ifndef _E1000_H_
#define _E1000_H_

#include "net.h"
#include "pci.h"

bool e1000_supported(const pci_device_info_t *info);
bool e1000_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6]);
bool e1000_ready(void);
bool e1000_link_up(void);
void e1000_rx_start(void);
void e1000_rx_stop(void);
void e1000_debug_state(const char *reason);
bool e1000_send_frame(const uint8_t *packet, uint16_t length);
void e1000_poll(void (*handler)(const uint8_t *packet, uint16_t length));
void e1000_shutdown(void);

#endif
