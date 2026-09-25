/*
 * memstats.c - Monios 细粒度内存统计聚合（Task 26）。
 *
 * 把 frame/heap/buddy/pool/进程等子系统的只读状态汇总成一个平铺快照，
 * 供 SYS_MEMORY_STATS(79) 拷给用户态 memmon.exe。纯只读、无锁、无动态分配。
 */

#include "memstats.h"
#include "buddy.h"
#include "frame.h"
#include "heap.h"
#include "memory.h"
#include "pcb.h"
#include "pool.h"
#include "string.h"

void memstats_init(void)
{
    /* 无静态状态需要初始化；保留接口以与其它 *_init 一致。 */
}

void memstats_fill(memstats_snapshot_t *out)
{
    if (out == 0) return;
    memset(out, 0, sizeof(*out));
    out->magic = MEMSTATS_MAGIC;
    out->version = 1;

    /* 全局用量 */
    out->total_bytes = memory_total_used() + memory_total_free();
    out->used_bytes  = memory_total_used();
    out->free_bytes  = memory_total_free();
    out->high_water_used = memory_high_water_used();
    out->kmalloc_alloc_count = memory_alloc_count();
    out->kmalloc_free_count  = memory_free_count();

    /* 内核堆 */
    const heap_info_t *hi = heap_info();
    if (hi != 0) {
        out->heap_bytes      = hi->size;
        out->heap_used_bytes = hi->used;
        out->heap_free_bytes = hi->free_bytes;
    }

    /* 内核代码段估算：链接起点 0x2001000，按已用堆之前的静态区粗估 8MB 中的 1MB。 */
    out->kernel_text_bytes = 0x00100000ULL; /* 1 MiB 估算 */

    /* frame / buddy */
    const frame_info_t *fi = frame_info();
    if (fi != 0) {
        out->frame_total    = fi->total_frames;
        out->frame_used     = fi->used_frames;
        out->frame_reserved = fi->reserved_frames;
        out->reserved_bytes = (uint64_t)fi->reserved_frames * 4096ULL;
    }
    const buddy_info_t *bi = buddy_info();
    if (bi != 0) {
        out->buddy_alloc_count = bi->alloc_count;
        out->buddy_free_count  = bi->free_count;
        out->buddy_split_count = bi->split_count;
    }

    /* pool/slab */
    const pool_stats_t *ps = pool_stats();
    if (ps != 0) {
        out->pool_count      = ps->pools;
        out->pool_slots_total = ps->slots_total;
        out->pool_slots_used  = ps->slots_used;
        out->pool_alloc_ops    = ps->alloc_ops;
        out->pool_free_ops     = ps->free_ops;
    }

    /* 进程：复用 process_enum_all，取常驻页表页数累加为页表占用。 */
    process_enum_result_t penum;
    uint32_t n = process_enum_all(&penum);
    if (n > MEMSTATS_MAX_PROCS) n = MEMSTATS_MAX_PROCS;
    out->proc_count = n;
    uint64_t page_table_pages = 0;
    for (uint32_t i = 0; i < n; i++) {
        const process_info_t *pi = &penum.entries[i];
        out->procs[i].pid = pi->pid;
        uint32_t j = 0;
        while (pi->name[j] && j < sizeof(out->procs[i].name) - 1) {
            out->procs[i].name[j] = pi->name[j];
            j++;
        }
        out->procs[i].name[j] = '\0';
        out->procs[i].mem_kb   = pi->mem_kb;
        out->procs[i].mem_pages = pi->mem_pages;
        out->procs[i].cpu_ticks = pi->cpu_total_ticks;
        page_table_pages += pi->mem_pages;
    }
    out->page_table_bytes = page_table_pages * 4096ULL;
}
