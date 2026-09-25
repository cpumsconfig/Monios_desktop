#ifndef _NTFS_H_
#define _NTFS_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool read_only;
    uint32_t volume_lba;
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint64_t total_sectors;
    uint64_t total_clusters;
    uint64_t free_clusters;
    uint64_t mft_lcn;
    uint64_t mftmirr_lcn;
    uint32_t serial_low;
    uint32_t serial_high;
    uint32_t cluster_size;
    uint32_t mft_record_size;
    uint32_t index_record_size;
    uint32_t mft0_lba;
    bool mft0_readable;
    char status[64];
} ntfs_info_t;

bool ntfs_init(void);
uint16_t ntfs_root_entry_count(void);
bool ntfs_exists(const char *path);
bool ntfs_is_dir(const char *path);
int32_t ntfs_file_size(const char *path);
int32_t ntfs_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t ntfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t ntfs_write_file(const char *path, const void *buffer, uint32_t size);
bool ntfs_delete(const char *path);
bool ntfs_mkdir(const char *path);
bool ntfs_rmdir(const char *path);
bool ntfs_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool ntfs_list_root(char *buffer, uint32_t buffer_size);
const ntfs_info_t *ntfs_info(void);
const char *ntfs_status(void);
uint64_t ntfs_free_bytes(void);

/* 文件状态：时间戳为 NTFS 原生 100ns 间隔（自 1601-01-01 起），由 VFS 层转换。
 * attr 为 FAT 风格属性位：只读=0x01 隐藏=0x02 系统=0x04 目录=0x10 归档=0x20。 */
typedef struct {
    uint64_t file_size;
    uint8_t  attr;
    bool     is_dir;
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
} ntfs_stat_t;

bool ntfs_stat(const char *path, ntfs_stat_t *stat_out);
bool ntfs_rename(const char *oldpath, const char *newpath);

#endif
