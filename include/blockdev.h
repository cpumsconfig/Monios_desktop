#ifndef _BLOCKDEV_H_
#define _BLOCKDEV_H_

#include "stdbool.h"
#include "stdint.h"

/* 块设备类型枚举 */
typedef enum {
    BLOCKDEV_ATA = 0,
    BLOCKDEV_AHCI,
    BLOCKDEV_NVME,
    BLOCKDEV_VIRTIO_BLK,
    BLOCKDEV_CDROM,
    BLOCKDEV_SDHCI
} blockdev_type_t;

/* 块设备读扇区函数指针：从 LBA 读取一个扇区到 buffer */
typedef bool (*blockdev_read_sector_fn)(uint64_t lba, void *buffer);

/* 块设备多扇区读函数指针：从 LBA 开始读取 count 个扇区到 buffer */
typedef bool (*blockdev_read_sectors_fn)(uint64_t lba, uint32_t count, void *buffer);

/* 块设备写扇区函数指针：把 buffer 内容写入 LBA 对应的一个扇区 */
typedef bool (*blockdev_write_sector_fn)(uint64_t lba, const void *buffer);

/* 块设备多扇区写函数指针：把 buffer 内容从 LBA 开始写入 count 个扇区 */
typedef bool (*blockdev_write_sectors_fn)(uint64_t lba, uint32_t count, const void *buffer);

/* 统一块设备抽象结构体 */
typedef struct {
    blockdev_type_t type;          /* 设备类型 */
    uint32_t index;                /* 同类型内的序号 */
    uint32_t sector_size;           /* 扇区大小（默认 512） */
    uint64_t capacity_sectors;     /* 总扇区数 */
    /* 单次 I/O 能承受的最大扇区数，0 表示不限。
     *
     * 各驱动的 DMA 暂存缓冲是固定大小的（AHCI 32 个扇区、virtio-blk 8 个），
     * 超过就会被驱动直接拒绝 —— 而文件系统一次会要几百个扇区。块设备层会按
     * 这个值自动切块下发，调用方不用关心。 */
    uint32_t max_transfer_sectors;
    char name[32];                  /* 设备名称 */
    blockdev_read_sector_fn read_sector;       /* 读单个扇区 */
    blockdev_read_sectors_fn read_sectors;      /* 读多个扇区 */
    blockdev_write_sector_fn write_sector;      /* 写单个扇区（NULL 表示不支持写） */
    blockdev_write_sectors_fn write_sectors;    /* 写多个扇区（NULL 表示不支持写） */
} blockdev_t;

/* 注册表最大设备数 */
#define BLOCKDEV_MAX_DEVICES 16

/* 初始化块设备层：探测并注册所有可用存储设备 */
bool blockdev_init(void);

/* 获取已注册的块设备数量 */
int blockdev_count(void);

/* 按索引获取块设备指针（只读），越界返回 NULL */
const blockdev_t *blockdev_get(int index);

/* 从指定设备读取一个扇区 */
bool blockdev_read_sector(int dev_index, uint64_t lba, void *buffer);

/* 从指定设备读取多个扇区 */
bool blockdev_read_sectors(int dev_index, uint64_t lba, uint32_t count, void *buffer);

/* 向指定设备写入一个扇区（设备不支持写时返回 false） */
bool blockdev_write_sector(int dev_index, uint64_t lba, const void *buffer);

/* 向指定设备写入多个扇区（设备不支持写时返回 false） */
bool blockdev_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void *buffer);

/* ============================================================
 *  I/O 调度钩子（由 kernel/sched/iosched.c 注册）
 *  - blockdev_set_iosched() 安装后，公开的 read/write_sectors 会先经过调度器。
 *  - 调度器最终通过 blockdev_raw_read/write_sectors 下发真实 I/O。
 *  - 未安装钩子时（默认），blockdev 行为与原来完全一致，不影响回归测试。
 * ============================================================ */

/* 调度器下发真实 I/O 的回调签名 */
typedef bool (*blockdev_iosched_fn)(int dev_index, uint64_t lba, uint32_t count,
                                    void *buffer, bool write);

/* 绕过调度器的底层读写（供调度器 drain 时调用） */
bool blockdev_raw_read_sectors(int dev_index, uint64_t lba, uint32_t count, void *buffer);
bool blockdev_raw_write_sectors(int dev_index, uint64_t lba, uint32_t count, const void *buffer);

/* 安装/卸载调度钩子（传 NULL 卸载） */
void blockdev_set_iosched(blockdev_iosched_fn fn);

#endif /* _BLOCKDEV_H_ */
