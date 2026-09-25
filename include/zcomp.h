#ifndef _ZCOMP_H_
#define _ZCOMP_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * 内存压缩框架（zcomp）
 *  - 把“不常用”的物理页压缩后存进 zsmalloc 池（zs.c），腾出原始页。
 *  - 访问时（缺页）再解压缩恢复。
 *  - 压缩算法为内置轻量 LZ77（无外部库依赖）。
 *
 * 这是一个自包含框架：
 *  - zcomp_compress_page() / zcomp_decompress_page() 供 VM 回收路径与缺页路径调用。
 *  - zcomp_touch() 标记页最近被访问（引用位）。
 *  - zcomp_reclaim_pass() 扫描候选页，压缩其中未被引用的页。
 *
 * 与 kernel/mm/zs.c 的关系：压缩后的字节串用 zs_malloc() 存进 zcomp 专用 pool。
 * 缺页异常处理（kernel/mm 回收/缺页代码，由内存组负责）应在发生压缩页故障时
 * 调用 zcomp_decompress_page(vaddr, out_buffer) 恢复内容。
 *
 * syscall 59 / SYS_MEM_COMPRESS_CTL handler。
 */

#define ZCOMP_CTL_STATUS     0u   /* arg1: user zcomp_info_t* */
#define ZCOMP_CTL_RECLAIM    1u   /* 触发一轮回收 */
#define ZCOMP_CTL_ENABLE     2u   /* arg1: 0/1 */
#define ZCOMP_CTL_DROP       3u   /* 解压全部并清空（释放 zs 池引用） */

#define ZCOMP_PAGE_SIZE      4096u
#define ZCOMP_MAX_PAGES      256u  /* 跟踪表容量 */

typedef enum {
    ZCOMP_PAGE_FREE = 0,
    ZCOMP_PAGE_ACTIVE,      /* 在跟踪表中但尚未压缩 */
    ZCOMP_PAGE_COMPRESSED   /* 已压缩，内容在 zs 池 */
} zcomp_page_state_t;

typedef struct {
    bool enabled;
    bool pool_ready;
    uint32_t tracked_pages;
    uint32_t compressed_pages;
    uint32_t compress_ops;
    uint32_t decompress_ops;
    uint32_t reclaim_passes;
    uint32_t failures;
    uint64_t total_original_bytes;
    uint64_t total_compressed_bytes;   /* 压缩后实际占用（含 zs 开销另计） */
    float last_ratio;                  /* 最近一次平均压缩率 = comp/orig */
    char status[64];
} zcomp_info_t;

void zcomp_init(void);

/* 注册一个 4KB 页到回收候选表（vaddr 为该页内核虚拟地址）。返回 slot 索引，<0 失败。 */
int32_t zcomp_track(uint64_t vaddr, const void *page_data);

/* 标记页最近被访问（置引用位，回收时会跳过）。 */
void zcomp_touch(uint64_t vaddr);

/* 压缩指定页（立即压缩，不等待回收）。成功返回 true。 */
bool zcomp_compress_page(uint64_t vaddr, const void *page_data);

/* 若该页已压缩，解压到 out_buffer（必须 >= 4096 字节）。返回 true 表示命中压缩页并已恢复。 */
bool zcomp_decompress_page(uint64_t vaddr, void *out_buffer);

/* 一轮回收：清除引用位，把未被引用的 active 页压缩。返回压缩的页数。 */
uint32_t zcomp_reclaim_pass(void);

const zcomp_info_t *zcomp_info(void);
const char *zcomp_status(void);

/* syscall 59 handler（syscall.c dispatch 尚未接入，此处仅实现并导出） */
uint64_t zcomp_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);

/* 内置压缩原语（可独立单元测试）：返回压缩后长度，0 表示压缩失败（不可压缩/溢出） */
uint32_t zcomp_lz_encode(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap);
uint32_t zcomp_lz_decode(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap);

#endif
