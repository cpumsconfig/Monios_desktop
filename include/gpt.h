#ifndef _GPT_H_
#define _GPT_H_

#include "file.h"
#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  GPT (GUID Partition Table) 磁盘分区表支持
 *  结构体均按 UEFI 规范在磁盘上的原始布局定义（packed）。
 *  注意：GUID 在磁盘上采用微软混合字节序——前 3 个字段小端，
 *  最后 8 字节原样大端存储，因此下列常量表直接与 type_guid
 *  做 memcmp 即可。
 * ============================================================ */

/* GPT 头（LBA 1），磁盘上共 92 字节 */
typedef struct {
    uint8_t  signature[8];          /* 固定为 "EFI PART" */
    uint32_t revision;              /* 通常为 0x00010000 */
    uint32_t header_size;            /* GPT 头大小，通常 92 */
    uint32_t header_crc32;           /* 头 CRC32（本实现不校验） */
    uint32_t reserved;               /* 必须为 0 */
    uint64_t current_lba;           /* 本头所在 LBA */
    uint64_t backup_lba;             /* 备份头所在 LBA */
    uint64_t first_usable_lba;       /* 首个可用分区 LBA */
    uint64_t last_usable_lba;        /* 末个可用分区 LBA */
    uint8_t  disk_guid[16];          /* 磁盘 GUID */
    uint64_t partition_entry_lba;    /* 分区条目数组起始 LBA，通常为 2 */
    uint32_t partition_entry_count;  /* 分区条目总数（含空条目） */
    uint32_t partition_entry_size;   /* 单个条目大小，通常 128 */
    uint32_t partition_entry_crc32;  /* 条目数组 CRC32（本实现不校验） */
} __attribute__((packed)) gpt_header_t;

/* GPT 分区条目，磁盘上通常 128 字节 */
typedef struct {
    uint8_t  type_guid[16];          /* 分区类型 GUID（全 0 表示空条目） */
    uint8_t  unique_guid[16];        /* 分区唯一 GUID */
    uint64_t first_lba;              /* 分区起始 LBA */
    uint64_t last_lba;               /* 分区结束 LBA（含） */
    uint64_t attributes;             /* 属性位 */
    uint16_t name[36];                /* 分区名 UTF-16LE，本实现不解析 */
} __attribute__((packed)) gpt_part_entry_t;

/* 常用 GPT 分区类型 GUID（磁盘原始字节序） */
/* Microsoft Basic Data：EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 (NTFS/FAT32/exFAT) */
extern const uint8_t gpt_guid_microsoft_basic_data[16];
/* EFI System Partition：C12A7328-F81F-11D2-BA4B-00A0C93EC93B (FAT32) */
extern const uint8_t gpt_guid_efi_system[16];
/* Linux filesystem：0FC63DAF-8483-4772-8E79-3D69D8477DE4 (ext) */
extern const uint8_t gpt_guid_linux_filesystem[16];
/* Microsoft Reserved：E3C9E316-0B5C-4DB8-817D-F92DF00215AE（保留分区，跳过不挂载） */
extern const uint8_t gpt_guid_microsoft_reserved[16];

/* 读扇区回调：从指定 LBA 读 512 字节到 buf，成功返回 true */
typedef bool (*gpt_read_sector_fn)(uint64_t lba, void *buf);

/* 检测 sector0（磁盘第 0 扇区）是否为 protective MBR（存在类型 0xEE 条目） */
bool gpt_detect(uint8_t sector0[512]);

/* 通过回调读取 LBA 1 的 GPT 头并校验签名，成功返回 true */
bool gpt_read_header(gpt_read_sector_fn read_sector, gpt_header_t *header);

/* 读取分区条目数组，过滤掉 first_lba==0 的空条目，复制到 entries；
 * 返回实际填充的条目数（不超过 max_entries） */
int gpt_read_partitions(gpt_read_sector_fn read_sector,
                        const gpt_header_t *header,
                        gpt_part_entry_t *entries,
                        int max_entries);

/* 根据 type_guid 判断文件系统类型；未知/保留分区返回 FS_TYPE_NONE */
fs_type_t gpt_partition_fs_type(const gpt_part_entry_t *entry);

#endif
