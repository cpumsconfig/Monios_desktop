#ifndef _AHCI_H_
#define _AHCI_H_

#include "stdbool.h"
#include "stdint.h"

#define AHCI_MAX_PORTS 6

/* 单次 I/O 的扇区上限，由驱动内部的 DMA 暂存缓冲大小决定。
 * 块设备层会读这个值来切块，见 include/blockdev.h 的 max_transfer_sectors。 */
#define AHCI_MAX_SECTORS_PER_IO 32U
#define AHCI_SECTOR_SIZE        512U

typedef struct {
    bool present;
    bool ready;
    uint8_t port;
    uint32_t ssts;
    uint32_t tfd;
    uint64_t capacity_sectors;
    uint16_t sector_size;
    uint32_t read_ops;
    uint32_t write_ops;
    char model[41];
} ahci_port_t;

typedef struct {
    bool present;
    bool ready;
    bool mmio_ready;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t prog_if;
    uint8_t irq;
    uint8_t active;
    uint8_t command_slots;
    uint16_t sector_size;
    uint64_t abar;
    uint64_t capacity_sectors;
    uint32_t cap;
    uint32_t version;
    uint32_t ports_implemented;
    uint32_t implemented_port_count;
    uint32_t active_port_count;
    uint32_t port_signature;
    uint32_t port_ssts;
    uint32_t port_tfd;
    uint32_t read_ops;
    uint32_t write_ops;
    uint32_t last_error;
    uint32_t sector0_signature;
    uint32_t disk_count;
    ahci_port_t ports[AHCI_MAX_PORTS];
    char model[41];
    char status[64];
} ahci_info_t;

bool ahci_driver_init(void);
void ahci_shutdown(void);
bool ahci_ready(void);
bool ahci_read_sector(uint64_t sector, void *buffer);
bool ahci_read_sectors(uint64_t sector, uint32_t count, void *buffer);
bool ahci_write_sector(uint64_t sector, const void *buffer);
bool ahci_write_sectors(uint64_t sector, uint32_t count, const void *buffer);
uint32_t ahci_probe(void);
void ahci_check_hotplug(void);
const ahci_info_t *ahci_info(void);
const char *ahci_status(void);

#endif
