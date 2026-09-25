#include "common.h"
#include "iosched.h"
#include "blockdev.h"
#include "string.h"

/*
 * 块设备 I/O 调度器
 *  - 请求队列 + LBA 排序 + 相邻请求合并
 *  - 三种可切换算法：NOOP(FIFO) / ELEVATOR(按 LBA) / DEADLINE(读优先)
 *
 * 注意：本文件默认不被 Makefile 链接（由集成方加入 KERNEL_*_OBJS）。
 * 编译期只依赖 blockdev.h 的声明；链接期需要 blockdev.o 提供的
 * blockdev_set_iosched / blockdev_raw_read_sectors / blockdev_raw_write_sectors。
 */

#define IOSCHED_MAX_REQUESTS   64U
#define IOSCHED_SECTOR_SIZE    512U

typedef struct {
    bool used;
    int dev;
    uint64_t lba;
    uint32_t count;
    void *buffer;
    bool write;
} iosched_req_t;

static iosched_req_t g_queue[IOSCHED_MAX_REQUESTS];
static iosched_info_t g_info;
static char g_status[64];
static uint64_t g_last_lba;
static bool g_hook_installed;

static iosched_req_t *iosched_alloc_slot(void)
{
    for (uint32_t i = 0; i < IOSCHED_MAX_REQUESTS; i++) {
        if (!g_queue[i].used) {
            return &g_queue[i];
        }
    }
    return NULL;
}

/* 尝试与队列中相邻、同方向、缓冲连续的请求合并。返回 true 表示已合并。 */
static bool iosched_try_merge(iosched_req_t *req)
{
    for (uint32_t i = 0; i < IOSCHED_MAX_REQUESTS; i++) {
        iosched_req_t *e = &g_queue[i];
        uint8_t *expected;

        if (!e->used || e->dev != req->dev || e->write != req->write) {
            continue;
        }
        /* 后向合并：e 紧接在 req 前面 */
        if (e->lba + e->count == req->lba) {
            expected = (uint8_t *) e->buffer + (uint64_t) e->count * IOSCHED_SECTOR_SIZE;
            if (expected == (uint8_t *) req->buffer) {
                e->count += req->count;
                g_info.total_merged++;
                return true;
            }
        }
        /* 前向合并：req 紧接在 e 前面 */
        if (req->lba + req->count == e->lba) {
            expected = (uint8_t *) req->buffer + (uint64_t) req->count * IOSCHED_SECTOR_SIZE;
            if (expected == (uint8_t *) e->buffer) {
                e->lba = req->lba;
                e->count += req->count;
                e->buffer = req->buffer;
                g_info.total_merged++;
                return true;
            }
        }
    }
    return false;
}

/* 按算法挑选下一个要下发的请求，返回索引；无请求返回 -1。 */
static int32_t iosched_pick_next(void)
{
    int32_t best = -1;

    for (uint32_t i = 0; i < IOSCHED_MAX_REQUESTS; i++) {
        if (!g_queue[i].used) {
            continue;
        }
        if (best < 0) {
            best = (int32_t) i;
            continue;
        }
        switch (g_info.algorithm) {
        case IOSCHED_NOOP:
            /* FIFO：保持队列顺序，先找到的即队首 */
            break;
        case IOSCHED_ELEVATOR:
            /* 单向电梯：优先 >= last_lba 的最小 LBA，否则取全局最小 LBA */
            {
                uint64_t bl = g_queue[best].lba;
                uint64_t il = g_queue[i].lba;
                bool best_ge = bl >= g_last_lba;
                bool i_ge = il >= g_last_lba;

                if (i_ge && (!best_ge || il < bl)) {
                    best = (int32_t) i;
                } else if (!i_ge && !best_ge && il < bl) {
                    best = (int32_t) i;
                }
            }
            break;
        case IOSCHED_DEADLINE:
            /* 读优先；同类内按 LBA 升序 */
            if (g_queue[i].write != g_queue[best].write) {
                if (!g_queue[i].write) {
                    best = (int32_t) i;  /* 读优先 */
                }
            } else if (g_queue[i].lba < g_queue[best].lba) {
                best = (int32_t) i;
            }
            break;
        default:
            break;
        }
    }
    return best;
}

static bool iosched_issue(iosched_req_t *req)
{
    bool ok;

    if (req->write) {
        ok = blockdev_raw_write_sectors(req->dev, req->lba, req->count, req->buffer);
        g_info.total_writes++;
    } else {
        ok = blockdev_raw_read_sectors(req->dev, req->lba, req->count, req->buffer);
        g_info.total_reads++;
    }
    g_last_lba = req->lba;
    return ok;
}

uint32_t iosched_run(void)
{
    uint32_t dispatched = 0;

    for (;;) {
        int32_t idx = iosched_pick_next();
        iosched_req_t *req;
        bool ok;

        if (idx < 0) {
            break;
        }
        req = &g_queue[idx];
        ok = iosched_issue(req);
        req->used = false;
        dispatched++;
        g_info.total_dispatched++;
        g_info.queue_depth--;
        if (!ok) {
            g_info.failed++;
        }
    }
    return dispatched;
}

/* blockdev 钩子：公开 read/write_sectors 进入这里 */
static bool iosched_enqueue_hook(int dev, uint64_t lba, uint32_t count,
                                 void *buffer, bool write)
{
    iosched_req_t *req;
    bool merged;
    bool ok;

    if (count == 0 || buffer == NULL) {
        return false;
    }

    req = iosched_alloc_slot();
    if (req == NULL) {
        /* 队列满：直接下发，不阻塞 */
        if (write) {
            return blockdev_raw_write_sectors(dev, lba, count, buffer);
        }
        return blockdev_raw_read_sectors(dev, lba, count, buffer);
    }

    req->used = true;
    req->dev = dev;
    req->lba = lba;
    req->count = count;
    req->buffer = buffer;
    req->write = write;

    merged = iosched_try_merge(req);
    if (!merged) {
        g_info.queue_depth++;
        g_info.total_queued++;
        if (g_info.queue_depth > g_info.max_queue_depth) {
            g_info.max_queue_depth = g_info.queue_depth;
        }
    } else {
        /* 已合并进旧槽，当前槽作废 */
        req->used = false;
    }

    /* 同步模型：drain 到空，保证调用方 buffer 被填充 */
    iosched_run();
    ok = true;
    strcpy(g_status, write ? "iosched: write dispatched" : "iosched: read dispatched");
    return ok;
}

void iosched_init(void)
{
    memset(g_queue, 0, sizeof(g_queue));
    memset(&g_info, 0, sizeof(g_info));
    g_info.algorithm = IOSCHED_ELEVATOR;
    g_info.enabled = true;
    g_last_lba = 0;
    strcpy(g_status, "iosched: ready");

    if (!g_hook_installed) {
        blockdev_set_iosched(iosched_enqueue_hook);
        g_hook_installed = true;
    }
}

void iosched_set_algorithm(iosched_algo_t algo)
{
    if (algo > IOSCHED_DEADLINE) {
        algo = IOSCHED_ELEVATOR;
    }
    g_info.algorithm = (uint32_t) algo;
}

const iosched_info_t *iosched_info(void)
{
    return &g_info;
}

const char *iosched_status(void)
{
    return g_status;
}

uint64_t iosched_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2)
{
    (void) arg2;

    switch (cmd) {
    case IOSCHED_CTL_STATUS: {
        iosched_info_t *out = (iosched_info_t *) arg1;
        if (out != NULL) {
            *out = g_info;
        }
        return 0;
    }
    case IOSCHED_CTL_SET_ALGO:
        iosched_set_algorithm((iosched_algo_t) arg1);
        return 0;
    case IOSCHED_CTL_RUN:
        return (uint64_t) iosched_run();
    case IOSCHED_CTL_GET_ALGO:
        return (uint64_t) g_info.algorithm;
    default:
        return (uint64_t) -1;
    }
}
