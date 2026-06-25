#ifndef _NVME_H_
#define _NVME_H_

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
    uint32_t cap_lo;
    uint32_t cap_hi;
    uint32_t version;
    uint32_t csts;
    uint16_t max_queue_entries;
    uint8_t doorbell_stride;
    char status[64];
} nvme_info_t;

bool nvme_driver_init(void);
void nvme_shutdown(void);
const nvme_info_t *nvme_info(void);
const char *nvme_status(void);

#endif
