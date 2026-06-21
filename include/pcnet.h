#ifndef _PCNET_H_
#define _PCNET_H_

#include "stdbool.h"
#include "stdint.h"
#include "net.h"
#include "pci.h"

typedef struct {
    bool present;
    bool io_ready;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint32_t io_base;
    char mac_text[18];
    char status[64];
} pcnet_info_t;

bool pcnet_supported(const pci_device_info_t *info);
bool pcnet_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6]);
bool pcnet_ready(void);
void pcnet_shutdown(void);
const pcnet_info_t *pcnet_info(void);
const char *pcnet_status(void);

#endif
