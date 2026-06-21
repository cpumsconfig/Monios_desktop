#ifndef _XHCI_H_
#define _XHCI_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool mmio_ready;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint32_t mmio_base;
    uint8_t cap_length;
    uint16_t hci_version;
    uint8_t max_slots;
    uint8_t max_ports;
    uint32_t hcsparams1;
    uint32_t hccparams1;
    char status[64];
} xhci_info_t;

bool xhci_driver_init(void);
void xhci_shutdown(void);
const xhci_info_t *xhci_info(void);
const char *xhci_status(void);

#endif
