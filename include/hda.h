#ifndef _HDA_H_
#define _HDA_H_

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
    uint16_t global_cap;
    char status[64];
} hda_info_t;

bool hda_driver_init(void);
void hda_shutdown(void);
const hda_info_t *hda_info(void);
const char *hda_status(void);

#endif
