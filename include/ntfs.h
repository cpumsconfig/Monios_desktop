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

#endif
