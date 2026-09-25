#ifndef _FAT32_H_
#define _FAT32_H_

#include "stdbool.h"
#include "stdint.h"

bool fat32_init(void);
uint16_t fat32_root_entry_count(void);
bool fat32_exists(const char *path);
bool fat32_is_dir(const char *path);
/* File stat structure */
typedef struct {
    uint32_t file_size;
    uint8_t  attr;
    bool     is_dir;
    uint16_t create_date;
    uint16_t create_time;
    uint16_t write_date;
    uint16_t write_time;
    uint16_t access_date;
} fat32_stat_t;

bool fat32_stat(const char *path, fat32_stat_t *stat_out);
int32_t fat32_file_size(const char *path);
int32_t fat32_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t fat32_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t fat32_write_file(const char *path, const void *buffer, uint32_t size);
bool fat32_delete(const char *path);
bool fat32_mkdir(const char *path);
bool fat32_rmdir(const char *path);
bool fat32_rename(const char *oldpath, const char *newpath);
/* 修改目录条目的属性位：仅替换 attr_mask 指定的位为 attr_value 中的对应位。 */
bool fat32_set_attr(const char *path, uint8_t attr_mask, uint8_t attr_value);
bool fat32_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool fat32_list_root(char *buffer, uint32_t buffer_size);

uint64_t fat32_total_bytes(uint64_t *free_out, uint32_t *cluster_size_out);

/* 将指定卷（绝对 LBA）格式化为 FAT32。sector_size 当前仅支持 512。 */
bool fat32_format(uint32_t volume_lba, uint64_t sector_count, uint32_t sector_size);

/* FAT32 文件系统检查报告 */
typedef struct {
    uint32_t errors;        /* 发现的错误数 */
    uint32_t fixed;          /* 已自动修复的错误数 */
    uint32_t files;          /* 普通文件数量 */
    uint64_t total_kb;       /* 卷总容量（KB） */
    uint32_t volume_serial;  /* 卷序列号 */
} fat32_chkdsk_report_t;

/* 检查当前挂载的 FAT32 卷，填充报告（可顺带修复部分错误）。 */
bool fat32_chkdsk(fat32_chkdsk_report_t *report);

/* ============================================================
 *  写前日志 (WAL) 与"干净卸载"标志
 *  - fat32_wal_mark_clean(): 正常关机/卸载时调用，把日志头标记为 CLEAN。
 *  - fat32_wal_dirty_at_mount(): 本次挂载是否检测到上次非正常关机
 *    （日志头 DIRTY 或存在未恢复记录）。开机流程据此决定是否自动 chkdsk。
 * ============================================================ */
void fat32_wal_mark_clean(void);
bool fat32_wal_dirty_at_mount(void);

/* ---- 符号链接（用内容魔数 "MONIOSLNK:" 标记的普通文件实现） ---- */

/* 创建符号链接：在 linkpath 处写入含魔数前缀的 target 路径。 */
bool fat32_symlink(const char *target, const char *linkpath);

/* 判断 linkpath 是否为符号链接文件。 */
bool fat32_is_symlink(const char *path);

/* 读取符号链接目标路径到 target，返回目标长度，失败返回 -1。 */
int32_t fat32_read_symlink(const char *path, char *target, uint32_t target_size);

/* ---- one-click backup: raw volume access + streaming image writer ---- */
bool fat32_volume_geometry(uint32_t *out_start_lba, uint64_t *out_total_sectors,
                           uint32_t *out_sector_size, uint32_t *out_cluster_sectors);
bool fat32_read_volume(uint32_t rel_lba, uint32_t count, void *buffer);
bool fat32_write_volume(uint32_t rel_lba, uint32_t count, const void *buffer);
int32_t fat32_append_open(const char *backend_path);
int32_t fat32_append_write(const void *buffer, uint32_t len);
int32_t fat32_append_close(void);

#endif
