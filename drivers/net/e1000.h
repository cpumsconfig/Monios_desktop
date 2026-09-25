#ifndef _E1000_H_
#define _E1000_H_

#include "net.h"
#include "pci.h"

bool e1000_supported(const pci_device_info_t *info);
bool e1000_probe(pci_device_info_t *out_info);
bool e1000_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6]);
bool e1000_ready(void);
bool e1000_link_up(void);
void e1000_rx_start(void);
void e1000_rx_stop(void);
void e1000_debug_state(const char *reason);
bool e1000_send_frame(const uint8_t *packet, uint16_t length);
void e1000_poll(void (*handler)(const uint8_t *packet, uint16_t length));
void e1000_shutdown(void);

/* Generic driver interface: read = receive, write = transmit */
int  e1000_read(uint8_t *buffer, uint32_t max_len);
bool e1000_write(const uint8_t *packet, uint32_t length);

/* Filtering / interrupt / info plumbing */
void e1000_set_promisc(bool promisc);
bool e1000_read_mac(uint8_t out[6]);
uint32_t e1000_interrupt_handler(void);
void e1000_irq_enable(void);
const char *e1000_status(void);
void e1000_info(char *buffer, uint32_t size);

#endif
