#include "common.h"
#include "blockdev.h"
#include "ahci.h"
#include "virtio.h"
#include "cdrom.h"
#include "nvme.h"
#include "sdhci.h"
#include "string.h"

/* ============================================================
 *  块设备注册表
 * ============================================================ */

static blockdev_t g_blockdevs[BLOCKDEV_MAX_DEVICES];
static int g_blockdev_count = 0;

/* ============================================================
 *  ATA PIO 端口定义（从 lib/file.c 搬过来的 fallback 逻辑）
 * ============================================================ */

#define BLOCKDEV_ATA_DATA_PORT         0x1F0
#define BLOCKDEV_ATA_SECTOR_COUNT_PORT 0x1F2
#define BLOCKDEV_ATA_LBA_LOW_PORT      0x1F3
#define BLOCKDEV_ATA_LBA_MID_PORT      0x1F4
#define BLOCKDEV_ATA_LBA_HIGH_PORT     0x1F5
#define BLOCKDEV_ATA_DRIVE_PORT        0x1F6
#define BLOCKDEV_ATA_COMMAND_PORT      0x1F7
#define BLOCKDEV_ATA_STATUS_PORT       0x1F7
#define BLOCKDEV_ATA_CMD_READ_SECTORS  0x20
#define BLOCKDEV_ATA_CMD_WRITE_SECTORS 0x30
#define BLOCKDEV_ATA_STATUS_BSY        0x80
#define BLOCKDEV_ATA_STATUS_DRQ        0x08
#define BLOCKDEV_ATA_WAIT_LIMIT        1000000U

static bool blockdev_ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < BLOCKDEV_ATA_WAIT_LIMIT; i++) {
        if ((inb(BLOCKDEV_ATA_STATUS_PORT) & BLOCKDEV_ATA_STATUS_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool blockdev_ata_wait_data_ready(void)
{
    if (!blockdev_ata_wait_not_busy()) {
        return false;
    }
    for (uint32_t i = 0; i < BLOCKDEV_ATA_WAIT_LIMIT; i++) {
        if ((inb(BLOCKDEV_ATA_STATUS_PORT) & BLOCKDEV_ATA_STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

static bool blockdev_ata_read_sector(uint64_t lba, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (lba > 0x0FFFFFFFULL) {
        return false;
    }
    if (!blockdev_ata_wait_not_busy()) {
        return false;
    }
    outb(BLOCKDEV_ATA_DRIVE_PORT, (uint8_t) (0xE0U | ((lba >> 24) & 0x0FU)));
    outb(BLOCKDEV_ATA_SECTOR_COUNT_PORT, 1);
    outb(BLOCKDEV_ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFFU));
    outb(BLOCKDEV_ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFFU));
    outb(BLOCKDEV_ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFFU));
    outb(BLOCKDEV_ATA_COMMAND_PORT, BLOCKDEV_ATA_CMD_READ_SECTORS);

    if (!blockdev_ata_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(BLOCKDEV_ATA_DATA_PORT);
    }
    return true;
}

/* 多扇区 PIO 读：一条 READ SECTORS 命令最多带 255 个扇区。
 *
 * 这里曾经是"每个扇区单独发一条命令"的循环 —— 在真机上是每扇区一次
 * 命令开销，在 QEMU 上更糟：每条命令的异步完成延迟（本机实测约 20-45ms）
 * 都要原样付一次。读一个 19.7MB 的字体 = 38500 条命令 ≈ 半小时，表现为
 * "启动卡在 font loading"。文件系统驱动的自带 PIO 路径一直是多扇区命令，
 * 所以走块设备层反而慢了两个数量级 —— 本函数就是为消除这个差距而存在。
 * 返回 false 前已读到的数据保留，调用方按失败处理。 */
#define BLOCKDEV_ATA_MAX_MULTI_SECTORS 255u

static bool blockdev_ata_read_sectors(uint64_t lba, uint32_t count, void *buffer)
{
    uint8_t *ptr = (uint8_t *) buffer;

    if (lba + count - 1u > 0x0FFFFFFFULL) {
        return false;
    }
    while (count > 0u) {
        uint32_t chunk = count > BLOCKDEV_ATA_MAX_MULTI_SECTORS
                         ? BLOCKDEV_ATA_MAX_MULTI_SECTORS : count;

        if (!blockdev_ata_wait_not_busy()) {
            return false;
        }
        outb(BLOCKDEV_ATA_DRIVE_PORT, (uint8_t) (0xE0U | ((lba >> 24) & 0x0FU)));
        outb(BLOCKDEV_ATA_SECTOR_COUNT_PORT, (uint8_t) chunk);
        outb(BLOCKDEV_ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFFU));
        outb(BLOCKDEV_ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFFU));
        outb(BLOCKDEV_ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFFU));
        outb(BLOCKDEV_ATA_COMMAND_PORT, BLOCKDEV_ATA_CMD_READ_SECTORS);

        for (uint32_t s = 0; s < chunk; s++) {
            uint16_t *dst = (uint16_t *) (void *) ptr;
            if (!blockdev_ata_wait_data_ready()) {
                return false;
            }
            for (uint32_t i = 0; i < 256; i++) {
                dst[i] = inw(BLOCKDEV_ATA_DATA_PORT);
            }
            ptr += 512U;
        }
        lba += chunk;
        count -= chunk;
    }
    return true;
}

static bool blockdev_ata_write_sector(uint64_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

    if (lba > 0x0FFFFFFFULL) {
        return false;
    }
    if (!blockdev_ata_wait_not_busy()) {
        return false;
    }
    outb(BLOCKDEV_ATA_DRIVE_PORT, (uint8_t) (0xE0U | ((lba >> 24) & 0x0FU)));
    outb(BLOCKDEV_ATA_SECTOR_COUNT_PORT, 1);
    outb(BLOCKDEV_ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFFU));
    outb(BLOCKDEV_ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFFU));
    outb(BLOCKDEV_ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFFU));
    outb(BLOCKDEV_ATA_COMMAND_PORT, BLOCKDEV_ATA_CMD_WRITE_SECTORS);

    if (!blockdev_ata_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < 256; i++) {
        outw(BLOCKDEV_ATA_DATA_PORT, src[i]);
    }
    (void) blockdev_ata_wait_not_busy();
    return true;
}

/* 多扇区 PIO 写：与读对称，一条命令带满 255 个扇区，
 * 每个扇区在 DRQ 置位后写入 256 个字，全部写完等 BSY 落下再返回。 */
static bool blockdev_ata_write_sectors(uint64_t lba, uint32_t count, const void *buffer)
{
    const uint8_t *ptr = (const uint8_t *) buffer;

    if (lba + count - 1u > 0x0FFFFFFFULL) {
        return false;
    }
    while (count > 0u) {
        uint32_t chunk = count > BLOCKDEV_ATA_MAX_MULTI_SECTORS
                         ? BLOCKDEV_ATA_MAX_MULTI_SECTORS : count;

        if (!blockdev_ata_wait_not_busy()) {
            return false;
        }
        outb(BLOCKDEV_ATA_DRIVE_PORT, (uint8_t) (0xE0U | ((lba >> 24) & 0x0FU)));
        outb(BLOCKDEV_ATA_SECTOR_COUNT_PORT, (uint8_t) chunk);
        outb(BLOCKDEV_ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFFU));
        outb(BLOCKDEV_ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFFU));
        outb(BLOCKDEV_ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFFU));
        outb(BLOCKDEV_ATA_COMMAND_PORT, BLOCKDEV_ATA_CMD_WRITE_SECTORS);

        for (uint32_t s = 0; s < chunk; s++) {
            const uint16_t *src = (const uint16_t *) (const void *) ptr;
            if (!blockdev_ata_wait_data_ready()) {
                return false;
            }
            for (uint32_t i = 0; i < 256; i++) {
                outw(BLOCKDEV_ATA_DATA_PORT, src[i]);
            }
            ptr += 512U;
        }
        if (!blockdev_ata_wait_not_busy()) {
            return false;
        }
        lba += chunk;
        count -= chunk;
    }
    return true;
}

/* 不支持写的设备占位函数（CD-ROM / NVMe 等） */
static bool blockdev_no_write_sector(uint64_t lba, const void *buffer)
{
    (void) lba;
    (void) buffer;
    return false;
}

static bool blockdev_no_write_sectors(uint64_t lba, uint32_t count, const void *buffer)
{
    (void) lba;
    (void) count;
    (void) buffer;
    return false;
}

/* ============================================================
 *  CD-ROM 512 字节扇区分片适配
 *  CD-ROM 硬件扇区为 2048 字节，注册时按 512 字节扇区暴露，
 *  内部做 2048->512 的分片缓存。
 * ============================================================ */

static uint8_t g_cdrom_cache[2048];
static uint64_t g_cdrom_cache_lba4 = (uint64_t) -1; /* 当前缓存对应的 2048 字节 LBA */

static bool blockdev_cdrom_read_sector(uint64_t lba, void *buffer)
{
    uint64_t lba4 = lba / 4U;
    uint32_t offset = (uint32_t) (lba % 4U) * 512U;

    if (lba4 != g_cdrom_cache_lba4) {
        if (!cdrom_read_sector((uint32_t) lba4, g_cdrom_cache)) {
            return false;
        }
        g_cdrom_cache_lba4 = lba4;
    }
    memcpy(buffer, g_cdrom_cache + offset, 512U);
    return true;
}

static bool blockdev_cdrom_read_sectors(uint64_t lba, uint32_t count, void *buffer)
{
    uint8_t *ptr = (uint8_t *) buffer;

    for (uint32_t i = 0; i < count; i++) {
        if (!blockdev_cdrom_read_sector(lba + i, ptr + (uint64_t) i * 512U)) {
            return false;
        }
    }
    return true;
}

/* ============================================================
 *  NVMe 占位：目前只有初始化，没有读扇区 API
 * ============================================================ */

static bool blockdev_nvme_read_sector(uint64_t lba, void *buffer)
{
    (void) lba;
    (void) buffer;
    return false;
}

static bool blockdev_nvme_read_sectors(uint64_t lba, uint32_t count, void *buffer)
{
    (void) lba;
    (void) count;
    (void) buffer;
    return false;
}

/* ============================================================
 *  内部注册辅助
 * ============================================================ */

static bool blockdev_register(blockdev_type_t type, uint32_t index,
                              uint32_t sector_size, uint64_t capacity_sectors,
                              uint32_t max_transfer_sectors,
                              const char *name,
                              blockdev_read_sector_fn read_sector,
                              blockdev_read_sectors_fn read_sectors,
                              blockdev_write_sector_fn write_sector,
                              blockdev_write_sectors_fn write_sectors)
{
    blockdev_t *dev;
    uint64_t name_len;

    if (g_blockdev_count >= BLOCKDEV_MAX_DEVICES) {
        return false;
    }
    dev = &g_blockdevs[g_blockdev_count];
    dev->type = type;
    dev->index = index;
    dev->sector_size = sector_size;
    dev->capacity_sectors = capacity_sectors;
    dev->max_transfer_sectors = max_transfer_sectors;
    dev->read_sector = read_sector;
    dev->read_sectors = read_sectors;
    dev->write_sector = write_sector;
    dev->write_sectors = write_sectors;

    name_len = 0;
    while (name != NULL && name[name_len] != '\0' && name_len < sizeof(dev->name) - 1U) {
        dev->name[name_len] = name[name_len];
        name_len++;
    }
    dev->name[name_len] = '\0';

    g_blockdev_count++;
    return true;
}

/* ============================================================
 *  公共 API 实现
 * ============================================================ */

bool blockdev_init(void)
{
    const ahci_info_t *ahci;
    const virtio_blk_info_t *vblk;
    const cdrom_info_t *cdrom;
    const nvme_info_t *nvme;

    g_blockdev_count = 0;
    g_cdrom_cache_lba4 = (uint64_t) -1;

    /* 1. AHCI 磁盘 */
    if (ahci_driver_init() && ahci_ready()) {
        ahci = ahci_info();
        if (ahci != NULL && ahci->present) {
            uint32_t sec_size = ahci->sector_size != 0 ? ahci->sector_size : 512U;
            blockdev_register(BLOCKDEV_AHCI, 0, sec_size,
                              ahci->capacity_sectors, AHCI_MAX_SECTORS_PER_IO, "ahci0",
                              ahci_read_sector, ahci_read_sectors,
                              ahci_write_sector, ahci_write_sectors);
        }
    }

    /* 2. VirtIO-blk 磁盘 */
    if (virtio_blk_driver_init() && virtio_blk_ready()) {
        vblk = virtio_blk_info();
        if (vblk != NULL && vblk->present) {
            uint32_t sec_size = vblk->sector_size != 0 ? vblk->sector_size : 512U;
            blockdev_register(BLOCKDEV_VIRTIO_BLK, 0, sec_size,
                              vblk->capacity_sectors, VIRTIO_BLK_MAX_SECTORS_PER_IO,
                              "virtio-blk0",
                              virtio_blk_read_sector, virtio_blk_read_sectors,
                              blockdev_no_write_sector, blockdev_no_write_sectors);
        }
    }

    /* 3. CD-ROM */
    cdrom_init();
    if (cdrom_is_present()) {
        cdrom = cdrom_info();
        uint64_t cap = (cdrom != NULL) ? (uint64_t) cdrom->total_sectors * 4U : 0U;
        /* 注册为 512 字节扇区视角：2048 字节硬件扇区 = 4 个 512 字节逻辑扇区 */
        blockdev_register(BLOCKDEV_CDROM, 0, 512U, cap, 0U, "cdrom0",
                          blockdev_cdrom_read_sector, blockdev_cdrom_read_sectors,
                          blockdev_no_write_sector, blockdev_no_write_sectors);
    }

    /* 4. NVMe（占位，读函数返回 false） */
    nvme_driver_init();
    nvme = nvme_info();
    if (nvme != NULL && nvme->present) {
        blockdev_register(BLOCKDEV_NVME, 0, 512U, 0U, 0U, "nvme0",
                          blockdev_nvme_read_sector, blockdev_nvme_read_sectors,
                          blockdev_no_write_sector, blockdev_no_write_sectors);
    }

    /* 5. ATA PIO fallback：始终注册一个，保证旧环境下仍能读写主盘 */
    /* 4b. SDHCI SD card (if a controller + card are present) */
    if (sdhci_init() && sdhci_ready()) {
        const sdhci_info_t *sd = sdhci_info();
        if (sd != NULL && sd->present) {
            blockdev_register(BLOCKDEV_SDHCI, 0, sd->block_size,
                              sd->capacity_sectors, 0U, "sd0",
                              sdhci_read_sector, sdhci_read_sectors,
                              sdhci_write_sector, sdhci_write_sectors);
        }
    }

    blockdev_register(BLOCKDEV_ATA, 0, 512U, 0U, BLOCKDEV_ATA_MAX_MULTI_SECTORS, "ata0",
                      blockdev_ata_read_sector, blockdev_ata_read_sectors,
                      blockdev_ata_write_sector, blockdev_ata_write_sectors);

    return g_blockdev_count > 0;
}

int blockdev_count(void)
{
    return g_blockdev_count;
}

const blockdev_t *blockdev_get(int index)
{
    if (index < 0 || index >= g_blockdev_count) {
        return NULL;
    }
    return &g_blockdevs[index];
}

bool blockdev_read_sector(int dev_index, uint64_t lba, void *buffer)
{
    const blockdev_t *dev;

    if (buffer == NULL) {
        return false;
    }
    dev = blockdev_get(dev_index);
    if (dev == NULL || dev->read_sector == NULL) {
        return false;
    }
    return dev->read_sector(lba, buffer);
}

/* I/O 调度钩子：安装后公开的 read/write_sectors 先经过调度器 */
static blockdev_iosched_fn g_iosched_hook = NULL;

void blockdev_set_iosched(blockdev_iosched_fn fn)
{
    g_iosched_hook = fn;
}

/* 按设备的单次 I/O 上限切块下发。
 *
 * 各驱动的 DMA 暂存缓冲是固定大小的（AHCI 32 个扇区、virtio-blk 8 个），
 * 超过上限会被驱动直接拒绝并返回 false。而文件系统读文件时会一次要几百个
 * 扇区，所以必须在块设备层切开 —— 否则大文件读取会在真机上静默失败。
 *
 * 任一子块失败立即返回 false；调用方需自行判断缓冲区内容是否可用。 */
static bool blockdev_dispatch_sectors(const blockdev_t *dev, uint64_t lba,
                                      uint32_t count, void *buffer, bool write)
{
    uint32_t max_chunk = dev->max_transfer_sectors != 0 ? dev->max_transfer_sectors : count;
    uint32_t done = 0;

    while (done < count) {
        uint8_t *ptr = (uint8_t *) buffer + (uint64_t) done * dev->sector_size;
        uint32_t chunk = count - done;
        bool ok = false;

        if (chunk > max_chunk) {
            chunk = max_chunk;
        }
        if (write) {
            if (dev->write_sectors != NULL) {
                ok = dev->write_sectors(lba + done, chunk, ptr);
            } else if (dev->write_sector != NULL) {
                ok = true;
                for (uint32_t i = 0; i < chunk; i++) {
                    if (!dev->write_sector(lba + done + i, ptr + (uint64_t) i * dev->sector_size)) {
                        ok = false;
                        break;
                    }
                }
            }
        } else {
            if (dev->read_sectors != NULL) {
                ok = dev->read_sectors(lba + done, chunk, ptr);
            } else if (dev->read_sector != NULL) {
                ok = true;
                for (uint32_t i = 0; i < chunk; i++) {
                    if (!dev->read_sector(lba + done + i, ptr + (uint64_t) i * dev->sector_size)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        if (!ok) {
            return false;
        }
        done += chunk;
    }
    return true;
}

bool blockdev_raw_read_sectors(int dev_index, uint64_t lba, uint32_t count, void *buffer)
{
    const blockdev_t *dev;

    if (buffer == NULL || count == 0) {
        return false;
    }
    dev = blockdev_get(dev_index);
    if (dev == NULL) {
        return false;
    }
    return blockdev_dispatch_sectors(dev, lba, count, buffer, false);
}

bool blockdev_raw_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void *buffer)
{
    const blockdev_t *dev;

    if (buffer == NULL || count == 0) {
        return false;
    }
    dev = blockdev_get(dev_index);
    if (dev == NULL) {
        return false;
    }
    return blockdev_dispatch_sectors(dev, lba, count, (void *) buffer, true);
}

bool blockdev_read_sectors(int dev_index, uint64_t lba, uint32_t count, void *buffer)
{
    if (g_iosched_hook != NULL) {
        return g_iosched_hook(dev_index, lba, count, buffer, false);
    }
    return blockdev_raw_read_sectors(dev_index, lba, count, buffer);
}

bool blockdev_write_sector(int dev_index, uint64_t lba, const void *buffer)
{
    const blockdev_t *dev;

    if (buffer == NULL) {
        return false;
    }
    dev = blockdev_get(dev_index);
    if (dev == NULL || dev->write_sector == NULL) {
        return false;
    }
    return dev->write_sector(lba, buffer);
}

bool blockdev_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void *buffer)
{
    if (g_iosched_hook != NULL) {
        return g_iosched_hook(dev_index, lba, (uint32_t) count, (void *) buffer, true);
    }
    return blockdev_raw_write_sectors(dev_index, lba, count, buffer);
}
