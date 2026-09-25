#ifndef _IDE_H_
#define _IDE_H_

#include "stdbool.h"
#include "stdint.h"

#define IDE_MAX_DEVICES 4

typedef struct {
    bool present;
    bool lba48;
    uint16_t io_base;
    uint16_t control_base;
    uint16_t bus_master_base;
    uint8_t drive;
    uint8_t channel;
    uint8_t status_reg;
    uint64_t sectors;
    uint32_t read_ops;
    uint32_t write_ops;
    uint32_t dma_ops;
    char model[41];
    char status[64];
} ide_device_t;

typedef struct {
    /* legacy top-level fields (mirror the active device) */
    bool present;
    uint16_t io_base;
    uint16_t control_base;
    uint8_t drive;
    uint8_t status_reg;
    uint32_t sectors;
    char model[41];
    char status[64];
    /* multi-device state */
    uint8_t device_count;
    uint8_t active;
    uint64_t sectors64;
    ide_device_t devices[IDE_MAX_DEVICES];
} ide_info_t;

bool ide_driver_init(void);
void ide_shutdown(void);
const ide_info_t *ide_info(void);
const char *ide_status(void);

uint32_t ide_probe(void);
bool ide_set_active(uint8_t dev);
bool ide_read_sectors(uint64_t lba, uint32_t count, void *buffer);
bool ide_write_sectors(uint64_t lba, uint32_t count, const void *buffer);
bool ide_read_sectors_dev(uint8_t dev, uint64_t lba, uint32_t count, void *buffer);
bool ide_write_sectors_dev(uint8_t dev, uint64_t lba, uint32_t count, const void *buffer);

#endif
