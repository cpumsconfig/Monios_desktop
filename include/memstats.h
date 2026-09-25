#ifndef _MEMSTATS_H_
#define _MEMSTATS_H_

#include "stdint.h"

/*
 * Monios 细粒度内存统计（Task 26 - 内存可视化）。
 *
 * memstats_fill() 在内核侧聚合各子系统（frame/heap/buddy/pool/进程）的
 * 实时数据，填入一个平铺结构，再由 SYS_MEMORY_STATS(79) 一次性拷给用户态
 * memmon.exe。所有字段都是只读快照，不持有锁、不分配内存。
 */

#define MEMSTATS_MAGIC        0x4D454D53u /* 'MEMS' */
#define MEMSTATS_MAX_PROCS    16u
#define MEMSTATS_BUDDY_ORDERS 13u   /* order 0..12 */

typedef struct {
    int32_t  pid;
    char     name[32];
    uint32_t mem_kb;        /* 常驻内存（KB） */
    uint32_t mem_pages;     /* 常驻页表页数 */
    uint64_t cpu_ticks;     /* 累计 CPU tick */
} memstats_proc_t;

typedef struct {
    uint32_t magic;             /* MEMSTATS_MAGIC */
    uint32_t version;

    /* ── 全局用量（字节） ── */
    uint64_t total_bytes;       /* 物理内存总量 */
    uint64_t used_bytes;        /* 已用 */
    uint64_t free_bytes;        /* 空闲 */
    uint64_t kernel_text_bytes; /* 内核代码段（估算） */
    uint64_t heap_bytes;        /* 内核堆总大小 */
    uint64_t heap_used_bytes;  /* 内核堆已用 */
    uint64_t heap_free_bytes;   /* 内核堆空闲 */
    uint64_t page_table_bytes;  /* 页表占用（按进程页数估算） */
    uint64_t reserved_bytes;    /* 保留/MMIO 区域 */

    /* ── 分配器计数 ── */
    uint32_t kmalloc_alloc_count;
    uint32_t kmalloc_free_count;
    uint64_t high_water_used;       /* 堆历史高水位 */

    /* frame 分配器 */
    uint32_t frame_total;
    uint32_t frame_used;
    uint32_t frame_reserved;

    /* buddy：每个 order 的空闲页数（本实现为近似/占位，0 表示未单独统计） */
    uint32_t buddy_free_pages[MEMSTATS_BUDDY_ORDERS];
    uint32_t buddy_alloc_count;
    uint32_t buddy_free_count;
    uint32_t buddy_split_count;

    /* slab/pool */
    uint32_t pool_count;
    uint32_t pool_slots_total;
    uint32_t pool_slots_used;
    uint32_t pool_alloc_ops;
    uint32_t pool_free_ops;

    /* ── 进程占用 Top N ── */
    uint32_t proc_count;
    memstats_proc_t procs[MEMSTATS_MAX_PROCS];
} memstats_snapshot_t;

void memstats_init(void);
void memstats_fill(memstats_snapshot_t *out);

#endif /* _MEMSTATS_H_ */
