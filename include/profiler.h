#ifndef _PROFILER_H_
#define _PROFILER_H_

#include "stdint.h"
#include "stdbool.h"

/*
 * Monios CPU 性能采样器（Task 25 - 火焰图）。
 *
 * 工作原理：
 *   - 在定时器中断（IRQ0）中调用 profiler_timer_tick()，按可配置频率采样。
 *   - 每次采样捕获：当前进程 PID、被打断时的指令指针（RIP）、
 *     以及沿 rbp 帧链回溯得到的内核栈返回地址（最多 16 层）。
 *   - 用 FNV-1a 哈希把整串返回地址折叠成一个签名，在静态哈希表中累加计数。
 *   - profiler_start() 清空统计表并开始采样；profiler_stop() 停止采样；
 *     profiler_copy_to_user() 把折叠后的栈拷贝到用户空间，供 perfmon.exe
 *     渲染火焰图，或导出为 Brendan Gregg 的 stackcollapse 文本格式。
 *
 * 该模块全部使用静态存储（不调用 kmalloc），可安全运行在中断上下文中。
 */

#define PROF_MAX_STACK      16u    /* 单次采样最多回溯的栈层数 */
#define PROF_MAX_ENTRIES    1024u  /* 静态哈希表槽位数（不同栈签名数上限） */
#define PROF_MAGIC         0x50524F46u /* 'PROF' */

/* 采样频率（Hz）。实际采样 = 每 N 个 tick 采一次，由 tick_rate / divider 得到。 */
#define PROF_DEFAULT_DIV    1u     /* 默认每个定时器 tick 采一次（100Hz 左右，取决于 PIT 频率） */

/* 拷给用户空间的二进制布局（与 user/apps/perfmon.c 中的镜像结构一致）。 */
typedef struct {
    uint32_t magic;          /* PROF_MAGIC */
    uint32_t entry_count;    /* 有效栈条目数 */
    uint64_t total_samples;  /* 采样总数 */
    uint32_t max_stack;      /* PROF_MAX_STACK */
    uint32_t div;            /* 当前采样分频 */
    uint64_t dropped;        /* 哈希表满时丢弃的采样数 */
} prof_header_t;

typedef struct {
    uint64_t count;                       /* 该栈被采到的次数 */
    uint32_t pid;                        /* 采样时正在运行的进程 PID */
    uint32_t frame_count;                 /* frames[] 有效层数 */
    uint64_t frames[PROF_MAX_STACK];      /* 内层在前：frames[0]=被打断的 RIP */
} prof_entry_t;

/* 生命周期接口（内核内部使用）。 */
void profiler_init(void);

/* 在定时器中断中调用。saved_regs 指向 irq0 入口压入的寄存器块：
 *   [0]=rax [1]=rbx ... [6]=rbp(被打断时的) ... [16]=RIP [17]=CS [18]=RFLAGS
 *   [19]=用户 RSP [20]=SS
 * 仅当 profiler 处于运行状态时才做任何工作。 */
void profiler_timer_tick(uint64_t *saved_regs);

/* 控制接口：清空统计并开始 / 停止。 */
int  profiler_start(uint32_t divider);
void profiler_stop(void);
bool profiler_is_running(void);

/* 把统计结果拷贝到用户缓冲区。
 * buf       : 用户空间目标缓冲区
 * buf_bytes : 缓冲区大小（字节）
 * 返回：拷贝的总字节数；不足返回 -1。 */
int64_t profiler_copy_to_user(void *buf, uint64_t buf_bytes);

/* 统计查询（内核内部 / shell 使用）。 */
uint64_t profiler_total_samples(void);
uint32_t profiler_entry_count(void);

#endif /* _PROFILER_H_ */
