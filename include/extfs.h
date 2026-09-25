#ifndef _EXTFS_H_
#define _EXTFS_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool read_only;
    uint32_t volume_lba;
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint16_t inode_size;
    uint16_t state;
    uint32_t feature_compat;
    uint32_t feature_incompat;
    uint32_t feature_ro_compat;
    char status[64];
} extfs_info_t;

bool extfs_init(void);
/* False preserves dirty cache state and must prevent clean unmount. */
bool extfs_sync(void);
uint16_t extfs_root_entry_count(void);
bool extfs_exists(const char *path);
bool extfs_is_dir(const char *path);
bool extfs_is_symlink(const char *path);
int32_t extfs_file_size(const char *path);
int32_t extfs_read_symlink(const char *path, char *target, uint32_t target_size);
int32_t extfs_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t extfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t extfs_write_file(const char *path, const void *buffer, uint32_t size);
bool extfs_delete(const char *path);
bool extfs_mkdir(const char *path);
bool extfs_rmdir(const char *path);
bool extfs_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool extfs_list_root(char *buffer, uint32_t buffer_size);
const extfs_info_t *extfs_info(void);
const char *extfs_status(void);

/* 文件状态：时间戳为 Unix 秒级时间戳，由 VFS 层转换。 */
typedef struct {
    uint64_t file_size;
    uint16_t mode;        /* inode mode（权限位 + 文件类型） */
    uint16_t uid;
    uint16_t gid;
    bool     is_dir;
    uint32_t create_time; /* i_ctime */
    uint32_t modify_time; /* i_mtime */
    uint32_t access_time;  /* i_atime */
} extfs_stat_t;

bool extfs_stat(const char *path, extfs_stat_t *stat_out);
bool extfs_rename(const char *oldpath, const char *newpath);

#endif
