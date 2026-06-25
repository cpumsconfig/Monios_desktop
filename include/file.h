#ifndef _FILE_H_
#define _FILE_H_

#include "stdbool.h"
#include "stdint.h"

/* 文件系统类型 */
typedef enum {
    FS_TYPE_NONE = 0,
    FS_TYPE_FAT16,
    FS_TYPE_FAT32,
    FS_TYPE_ISO9660,
    FS_TYPE_NTFS,
    FS_TYPE_EXTFS
} fs_type_t;

/* 挂载点信息 */
#define MAX_MOUNT_POINTS 8
#define MAX_MOUNT_PATH 128

typedef struct {
    char path[MAX_MOUNT_PATH];  /* 挂载点路径 */
    fs_type_t fs_type;          /* 文件系统类型 */
    int32_t partition;          /* 分区号（-1 表示整个磁盘，0-3 表示 MBR 分区） */
    bool mounted;               /* 是否已挂载 */
} mount_point_t;

bool file_init(void);
const char *file_backend_name(void);

/* 动态挂载接口 */
bool file_mount(const char *mount_path, const char *fs_type, int32_t partition);
bool file_umount(const char *mount_path);
int32_t file_mount_count(void);
bool file_get_mount_info(int32_t index, mount_point_t *info);
bool file_auto_mount(void);  /* 自动探测并挂载到 / */

/* 文件操作 */
bool file_exists(const char *path);
bool file_is_dir(const char *path);
int32_t file_size(const char *path);
uint16_t file_root_entry_count(void);
int32_t file_read(const char *path, void *buffer, uint32_t buffer_size);
int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t file_write(const char *path, const void *buffer, uint32_t size);
bool file_delete(const char *path);
bool file_mkdir(const char *path);
bool file_rmdir(const char *path);
bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size);

#endif
