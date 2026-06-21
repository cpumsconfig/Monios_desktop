#ifndef _AHCI_H_
#define _AHCI_H_

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
    uint8_t prog_if;
    uint8_t irq;
    uint32_t abar;
    uint32_t cap;
    uint32_t ports_implemented;
    char status[64];
} ahci_info_t;

bool ahci_driver_init(void);
void ahci_shutdown(void);
const ahci_info_t *ahci_info(void);
const char *ahci_status(void);

#endif
