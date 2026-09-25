#ifndef _MEMTEST_H_
#define _MEMTEST_H_

/*
 * include/memtest.h - 内存诊断测试的用户态/内核态共享 ABI。
 *
 * 实现：kernel/mm/memtest.c
 * 前端：user/apps/memtest.c（通过 SYS_MEMTEST_CTL 驱动）
 *
 * 重要：两侧必须包含本头文件，不得各自镜像一份结构体。历史上应用层自己
 * 定义了一份 error_list[16] 的镜像结构，而内核是 error_list[32]，状态拷贝
 * 时会写穿应用的栈缓冲区。这里把 memtest_result_t 作为唯一定义源。
 */

#include "stdbool.h"
#include "stdint.h"

/* ── 测试模式 ─────────────────────────────────────────────────────── */
#define MEMTEST_BASIC   0u
#define MEMTEST_QUICK   1u
#define MEMTEST_FULL    2u
#define MEMTEST_MODE_MAX MEMTEST_FULL

#define MEMTEST_MAX_ERRORS   32u

/* 默认测试窗口：从空闲区起点开始，最多测这么多字节。
 * 完整模式每个页要跑 8 种图案 + 64 轮走步 + 地址唯一性，代价是快速模式的
 * 数十倍，所以窗口必须封顶，否则一次 GUI 会话根本跑不完。 */
#define MEMTEST_DEFAULT_BYTES (64ULL * 1024ULL * 1024ULL)

/* ── SYS_MEMTEST_CTL 的 op 码 ────────────────────────────────────── */
#define MT_OP_START   1u   /* mode 有效，range 可省略 */
#define MT_OP_STOP    2u
#define MT_OP_STATUS  3u   /* 推进测试一步，返回当前快照 */
#define MT_OP_INFO    4u   /* 只看快照，不推进 */

typedef struct {
    uint64_t address;    /* 出错物理地址 */
    uint64_t expected;   /* 期望值 */
    uint64_t actual;     /* 实际读回值 */
} memtest_error_t;

typedef struct {
    uint32_t mode;              /* MEMTEST_BASIC / QUICK / FULL */
    uint32_t running;           /* 1 = 正在运行 */
    uint32_t cancelled;         /* 1 = 已被取消 */
    uint32_t finished;          /* 1 = 自然跑完 */

    uint64_t range_start;       /* 测试窗口起始物理地址 */
    uint64_t range_end;         /* 测试窗口结束物理地址 */
    uint64_t cursor;            /* 下一个待测物理地址 */

    uint64_t tested_bytes;      /* 已测试字节数 */
    uint64_t total_bytes;       /* 窗口内可测字节数（跳过已用帧后） */

    uint32_t errors;            /* 错误总数（可超过 error_count） */
    uint32_t error_count;       /* error_list 中已记录条数 */
    uint32_t error_list_size;   /* = MEMTEST_MAX_ERRORS，用于 ABI 自检 */
    uint32_t reserved0;

    uint64_t start_tick;        /* 开始 tick */
    uint64_t end_tick;          /* 结束 tick（未结束为 0） */
    uint64_t elapsed_ms;        /* 已用毫秒 */

    memtest_error_t error_list[MEMTEST_MAX_ERRORS];
} memtest_result_t;

/* ── SYS_MEMTEST_CTL 的请求块（用户态与内核共享） ─────────────────── */
typedef struct {
    uint32_t op;                /* MT_OP_* */
    uint32_t mode;              /* MT_OP_START：测试模式 */
    uint64_t range_start;       /* MT_OP_START：0 = 自动挑选空闲窗口 */
    uint64_t range_end;         /* MT_OP_START：0 = 自动推导 */
    int32_t  result;            /* 内核填写：0 成功，负数错误码 */
    uint32_t status_ready;      /* 快照是否已填充 */
    memtest_result_t status;    /* 结果快照 */
} memtest_ctl_request_t;

/* ── 内核侧接口 ──────────────────────────────────────────────────── */
/* 启动一次可中断的增量测试；range_start/range_end 为 0 时自动挑选空闲窗口。 */
int      memtest_start(uint32_t mode, uint64_t range_start, uint64_t range_end);
/* 推进测试，budget_pages 为本轮最多实写的空闲页数；返回 1=仍在运行。 */
uint32_t memtest_step(uint32_t budget_pages);
/* 请求取消当前测试。 */
void     memtest_cancel(void);
/* 取结果快照（内核地址）。 */
const memtest_result_t *memtest_get_result(void);
/* 进度百分比 0-100。 */
uint32_t memtest_progress(void);
/* 已用毫秒。 */
uint64_t memtest_elapsed_ms(void);
/* 同步跑完整个窗口（无 GUI 的批处理路径）。返回错误数，负数为失败。 */
int      memtest_run(uint64_t range_start, uint64_t range_end, uint32_t mode);
/* SYS_MEMTEST_CTL 内核入口。 */
int      memtest_ctl(memtest_ctl_request_t *req);

#endif /* _MEMTEST_H_ */
