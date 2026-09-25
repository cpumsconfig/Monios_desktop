#ifndef _VIRTIO_H_
#define _VIRTIO_H_

#include "stdbool.h"
#include "stdint.h"
#include "net.h"

typedef enum {
    VIRTIO_DEVICE_NONE = 0,
    VIRTIO_DEVICE_NETWORK,
    VIRTIO_DEVICE_BLOCK,
    VIRTIO_DEVICE_CONSOLE,
    VIRTIO_DEVICE_BALLOON,
    VIRTIO_DEVICE_SCSI,
    VIRTIO_DEVICE_RNG,
    VIRTIO_DEVICE_GPU,
    VIRTIO_DEVICE_INPUT,
    VIRTIO_DEVICE_SOUND,
    VIRTIO_DEVICE_OTHER
} virtio_device_kind_t;

typedef struct {
    bool present;
    bool legacy;
    bool modern;
    bool io_ready;
    bool mmio_ready;
    bool bus_master_enabled;
    bool vendor_capability;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint8_t legacy_status;
    uint16_t io_base;
    uint64_t mmio_base;
    uint32_t device_count;
    virtio_device_kind_t kind;
    char name[32];
    char status[64];
} virtio_info_t;

typedef struct {
    bool present;
    bool ready;
    bool legacy;
    bool modern;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint16_t io_base;
    uint32_t mmio_base;
    uint64_t capacity_sectors;
    uint32_t sector_size;
    uint32_t read_ops;
    uint8_t last_status;
    uint16_t sector0_signature;
    char status[64];
} virtio_blk_info_t;

bool virtio_driver_init(void);
void virtio_shutdown(void);
const virtio_info_t *virtio_info(void);
const char *virtio_status(void);

bool virtio_blk_driver_init(void);
void virtio_blk_shutdown(void);
bool virtio_blk_ready(void);

/* 单次 I/O 的扇区上限，由驱动内部的 DMA 暂存缓冲大小决定。
 * 块设备层会读这个值来切块，见 include/blockdev.h 的 max_transfer_sectors。 */
#define VIRTIO_BLK_SECTOR_SIZE        512U
#define VIRTIO_BLK_MAX_SECTORS_PER_IO 8U

bool virtio_blk_read_sector(uint64_t sector, void *buffer);
bool virtio_blk_read_sectors(uint64_t sector, uint32_t count, void *buffer);
const virtio_blk_info_t *virtio_blk_info(void);
const char *virtio_blk_status(void);

bool virtio_net_init(net_info_t *net, uint8_t mac[6]);
bool virtio_net_ready(void);
bool virtio_net_link_up(void);
void virtio_net_rx_start(void);
void virtio_net_poll(void (*handler)(const uint8_t *packet, uint16_t length));
bool virtio_net_send_frame(const uint8_t *packet, uint16_t length);
void virtio_net_shutdown(void);
const char *virtio_net_status(void);

#endif
