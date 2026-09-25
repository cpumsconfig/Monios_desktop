#include "gpt.h"
#include "stddef.h"
#include "string.h"

/* ============================================================
 *  GPT 分区表驱动（仅负责解析，不直接访问磁盘端口）
 *  所有扇区读取都通过调用方传入的回调完成。
 * ============================================================ */

/* 分区类型 GUID 常量（磁盘原始字节序，与 type_guid 逐字节比较） */
const uint8_t gpt_guid_microsoft_basic_data[16] = {
    0xA2, 0xD0, 0xE0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};
const uint8_t gpt_guid_efi_system[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
const uint8_t gpt_guid_linux_filesystem[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};
const uint8_t gpt_guid_microsoft_reserved[16] = {
    0x16, 0xE3, 0xC9, 0xE3, 0x5C, 0x0B, 0xB8, 0x4D,
    0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE
};

#define GPT_SIGNATURE       "EFI PART"
#define GPT_SIGNATURE_LEN   8U
#define GPT_SECTOR_SIZE     512U
#define GPT_MBR_SIG_OFFSET  510U
#define GPT_MBR_ENTRY_BASE  446U
#define GPT_MBR_ENTRY_SIZE  16U
#define GPT_MBR_ENTRY_TYPE  4U
#define GPT_MBR_PROTECTIVE  0xEE

bool gpt_detect(uint8_t sector0[512])
{
    if (sector0 == NULL) {
        return false;
    }
    /* 必须先有合法的 MBR 引导签名 */
    if (sector0[GPT_MBR_SIG_OFFSET] != 0x55 ||
        sector0[GPT_MBR_SIG_OFFSET + 1] != 0xAA) {
        return false;
    }
    /* protective MBR 在至少一个分区条目里标记类型 0xEE */
    for (int i = 0; i < 4; i++) {
        if (sector0[GPT_MBR_ENTRY_BASE + i * GPT_MBR_ENTRY_SIZE +
                    GPT_MBR_ENTRY_TYPE] == GPT_MBR_PROTECTIVE) {
            return true;
        }
    }
    return false;
}

bool gpt_read_header(gpt_read_sector_fn read_sector, gpt_header_t *header)
{
    uint8_t sector[GPT_SECTOR_SIZE];

    if (read_sector == NULL || header == NULL) {
        return false;
    }
    /* GPT 头固定位于 LBA 1 */
    if (!read_sector(1, sector)) {
        return false;
    }
    /* 校验签名 "EFI PART" */
    if (memcmp(sector, GPT_SIGNATURE, GPT_SIGNATURE_LEN) != 0) {
        return false;
    }
    memcpy(header, sector, sizeof(gpt_header_t));
    return true;
}

int gpt_read_partitions(gpt_read_sector_fn read_sector,
                        const gpt_header_t *header,
                        gpt_part_entry_t *entries,
                        int max_entries)
{
    uint8_t sector[GPT_SECTOR_SIZE];
    uint32_t entry_size;
    uint32_t per_sector;
    uint32_t total;
    uint64_t array_lba;
    uint64_t cached_lba = (uint64_t)(-1);   /* 已读入 sector 的 LBA，避免重复读扇区 */
    int filled = 0;

    if (read_sector == NULL || header == NULL ||
        entries == NULL || max_entries <= 0) {
        return 0;
    }

    /* 防御性规范化条目大小：标准为 128 字节 */
    entry_size = header->partition_entry_size;
    if (entry_size < 128U || entry_size > GPT_SECTOR_SIZE) {
        entry_size = 128U;
    }
    per_sector = GPT_SECTOR_SIZE / entry_size;
    if (per_sector == 0) {
        per_sector = 1;
    }

    array_lba = header->partition_entry_lba;
    total = header->partition_entry_count;

    for (uint32_t i = 0; i < total && filled < max_entries; i++) {
        uint32_t sector_idx = i / per_sector;
        uint32_t offset = (i % per_sector) * entry_size;
        uint64_t lba = array_lba + sector_idx;
        const gpt_part_entry_t *e;

        if (lba != cached_lba) {
            if (!read_sector(lba, sector)) {
                break;
            }
            cached_lba = lba;
        }

        e = (const gpt_part_entry_t *) (sector + offset);

        /* 空条目：first_lba 为 0（标准上空条目类型 GUID 全 0，这里按任务约定以 first_lba 判定） */
        if (e->first_lba == 0) {
            continue;
        }

        memcpy(&entries[filled], e, sizeof(gpt_part_entry_t));
        filled++;
    }
    return filled;
}

fs_type_t gpt_partition_fs_type(const gpt_part_entry_t *entry)
{
    if (entry == NULL) {
        return FS_TYPE_NONE;
    }
    if (memcmp(entry->type_guid, gpt_guid_microsoft_basic_data, 16) == 0) {
        /* 可能是 NTFS/FAT32/exFAT，这里归为 NTFS，挂载时再按序探测 */
        return FS_TYPE_NTFS;
    }
    if (memcmp(entry->type_guid, gpt_guid_efi_system, 16) == 0) {
        return FS_TYPE_FAT32;
    }
    if (memcmp(entry->type_guid, gpt_guid_linux_filesystem, 16) == 0) {
        return FS_TYPE_EXTFS;
    }
    /* Microsoft Reserved 及其它未知类型一律返回 NONE，由调用方跳过 */
    return FS_TYPE_NONE;
}
