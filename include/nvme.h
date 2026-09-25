#ifndef _NVME_H_
#define _NVME_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool ready;
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
    uint32_t ns_count;
    uint64_t capacity_sectors;
    uint32_t lba_size;
    uint32_t read_ops;
    uint32_t write_ops;
    uint8_t model[41];
    char status[64];
} nvme_info_t;

bool nvme_driver_init(void);
void nvme_shutdown(void);
bool nvme_ready(void);
uint32_t nvme_probe(void);
bool nvme_read(uint32_t namespace_id, uint64_t lba, uint32_t count, void *buffer);
bool nvme_write(uint32_t namespace_id, uint64_t lba, uint32_t count, const void *buffer);
const nvme_info_t *nvme_info(void);
const char *nvme_status(void);

#endif
