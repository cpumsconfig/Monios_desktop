#ifndef _PCNET_H_
#define _PCNET_H_

#include "stdbool.h"
#include "stdint.h"
#include "net.h"
#include "pci.h"

#define PCNET_RING_SIZE 16
#define PCNET_BUFFER_SIZE 2048

typedef struct {
    bool present;
    bool io_ready;
    bool link_up;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint32_t io_base;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t rx_dropped;
    char mac_text[18];
    char status[64];
} pcnet_info_t;

bool pcnet_supported(const pci_device_info_t *info);
bool pcnet_probe(pci_device_info_t *out_info);
bool pcnet_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6]);
bool pcnet_ready(void);
bool pcnet_link_up(void);
void pcnet_shutdown(void);

/* Generic driver interface: read = receive, write = transmit */
int  pcnet_read(uint8_t *buffer, uint32_t max_len);
bool pcnet_write(const uint8_t *packet, uint32_t length);
bool pcnet_send(const uint8_t *packet, uint32_t length);
int  pcnet_recv(uint8_t *buffer, uint32_t max_len);

/* Polling / interrupt plumbing */
void pcnet_rx_start(void);
void pcnet_poll(void);
uint32_t pcnet_interrupt_handler(void);

const pcnet_info_t *pcnet_info(void);
const char *pcnet_status(void);

#endif
