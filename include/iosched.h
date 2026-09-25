#ifndef _IOSCHED_H_
#define _IOSCHED_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * 块设备 I/O 调度器
 *  - 请求队列：缓存待下发的块设备 I/O 请求
 *  - 排序：按 LBA 地址（电梯算法）/ FIFO(NOOP) / Deadline(读优先)
 *  - 合并：相邻 LBA 且缓冲连续的同方向请求合并为大请求
 *  - 通过 blockdev_set_iosched() 挂接进块设备层
 *
 * 集成点：
 *  - 内核初始化时调用 iosched_init()（注册 blockdev 钩子）。
 *  - blockdev.c 的公开 read/write_sectors 在钩子安装后自动经过本调度器。
 *  - syscall 60 / SYS_IOSCHED_CTL handler。
 */

typedef enum {
    IOSCHED_NOOP = 0,       /* FIFO，不排序 */
    IOSCHED_ELEVATOR = 1,   /* 按 LBA 升序（单向电梯） */
    IOSCHED_DEADLINE = 2    /* 读优先，读之间按 LBA，写次之 */
} iosched_algo_t;

#define IOSCHED_CTL_STATUS     0u
#define IOSCHED_CTL_SET_ALGO   1u   /* arg1: iosched_algo_t */
#define IOSCHED_CTL_RUN        2u   /* 立即 drain 队列 */
#define IOSCHED_CTL_GET_ALGO   3u

typedef struct {
    bool enabled;
    uint32_t algorithm;
    uint32_t queue_depth;       /* 当前队列长度 */
    uint32_t max_queue_depth;
    uint32_t total_queued;
    uint32_t total_dispatched;
    uint32_t total_merged;
    uint32_t total_reads;
    uint32_t total_writes;
    uint32_t failed;
    uint64_t last_lba;
    char status[64];
} iosched_info_t;

void iosched_init(void);
void iosched_set_algorithm(iosched_algo_t algo);
uint32_t iosched_run(void);  /* drain 队列，返回下发的请求数 */
const iosched_info_t *iosched_info(void);
const char *iosched_status(void);

/* syscall 60 handler（syscall.c dispatch 尚未接入，此处仅实现并导出） */
uint64_t iosched_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2);

#endif
