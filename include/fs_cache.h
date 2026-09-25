#ifndef _FS_CACHE_H_
#define _FS_CACHE_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * 文件系统缓存（读缓存 + 写回缓存）
 *
 * 设计目标：
 *  - 读缓存：按 (path, block) 粒度缓存文件数据块，LRU 淘汰，命中直接返回内存。
 *  - 写缓存（write-back）：写入先落到缓存并标记 dirty，不立即写盘；
 *    通过 fs_cache_flush() 统一异步/定时刷盘。
 *  - 缓存槽位数可配置（默认 FS_CACHE_DEFAULT_SLOTS，每槽 4KB）。
 *  - 统计：命中 / 未命中 / 填充 / 写回 / 淘汰 / 刷盘次数。
 *
 * 与 lib/file.c 的集成点（file.c 由安全组负责，本文件只导出接口）：
 *  - 读路径：在真正读盘之前调用 fs_cache_read_at(path, off, buf, len, loader)，
 *    loader 回调封装原有“从磁盘读 len 字节”的逻辑。
 *  - 写路径：在真正写盘之前调用 fs_cache_write_at(path, off, buf, len, flusher)，
 *    flusher 回调封装原有“把 len 字节写回磁盘”的逻辑；返回成功即认为写完成。
 *  - 文件关闭 / 卸载时调用 fs_cache_invalidate_path(path)。
 *  - 建议在系统 tick 里周期性调用 fs_cache_flush() 做定时刷盘。
 */

typedef int32_t (*fs_cache_loader_t)(const char *path, uint32_t offset, void *buffer, uint32_t size);
typedef int32_t (*fs_cache_flusher_t)(const char *path, uint32_t offset, const void *buffer, uint32_t size);

/* fs_cache_ctl() 子命令（对应 syscall 58 / SYS_FS_CACHE_CTL） */
#define FS_CACHE_CTL_STATUS     0u  /* arg1: user fs_cache_info_t*，返回统计 */
#define FS_CACHE_CTL_FLUSH       1u  /* 立即刷盘所有 dirty 槽 */
#define FS_CACHE_CTL_ENABLE      2u  /* arg1: 0=禁用 1=启用 */
#define FS_CACHE_CTL_SET_SLOTS   3u  /* arg1: 目标槽位数（钳位到 [16, FS_CACHE_MAX_SLOTS]） */
#define FS_CACHE_CTL_DROP        4u  /* 丢弃全部缓存（先刷盘再清空） */

#define FS_CACHE_BLOCK_SIZE      4096u
#define FS_CACHE_DEFAULT_SLOTS   512u   /* 默认 512 槽 * 4KB = 2MB 缓存 */
#define FS_CACHE_MAX_SLOTS       4096u  /* 最大 16MB */

typedef struct {
    bool enabled;
    uint32_t slots;
    uint32_t block_size;
    uint32_t hits;
    uint32_t misses;
    uint32_t fills;
    uint32_t dirty_writes;   /* 累计写入缓存的次数 */
    uint32_t evictions;      /* 累计 LRU 淘汰的槽数 */
    uint32_t invalidations;
    uint32_t flushes;         /* 累计刷盘次数（刷了多少个 dirty 槽） */
    uint32_t dirty_count;    /* 当前 dirty 槽数 */
    uint32_t configured_slots;
    char status[64];
} fs_cache_info_t;

void fs_cache_init(void);

/* 注册默认 flusher（由 VFS/file.c 在初始化时调用，供 flush/ctl 使用） */
void fs_cache_set_flusher(fs_cache_flusher_t flusher);

/* 读缓存：命中返回缓存数据；未命中用 loader 回填。返回读到的字节数，<0 出错。 */
int32_t fs_cache_read_at(const char *path, uint32_t offset, void *buffer, uint32_t size, fs_cache_loader_t loader);

/* Full blocks use write-back; partial blocks use the offset-aware backend
 * directly to preserve untouched bytes. Each dirty slot retains its flusher.
 * NULL flusher uses the registered default. Returns bytes written or <0. */
int32_t fs_cache_write_at(const char *path, uint32_t offset, const void *buffer, uint32_t size, fs_cache_flusher_t flusher);

/* 刷盘：把所有 dirty 槽写回。返回刷盘的槽数。flusher 为 NULL 时用注册的默认 flusher。 */
uint32_t fs_cache_flush(void);

/* Flush before invalidating. Dirty slots survive failed writes; callers must
 * check dirty_count before assuming all data is durable. */
void fs_cache_invalidate_path(const char *path);
void fs_cache_invalidate_all(void);
const fs_cache_info_t *fs_cache_info(void);
const char *fs_cache_status(void);

/* syscall 58 handler（syscall.c dispatch 尚未接入，此处仅实现并导出） */
uint64_t fs_cache_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);

#endif
