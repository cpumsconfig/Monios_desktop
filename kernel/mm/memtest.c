/*
 * kernel/mm/memtest.c - 物理内存诊断测试内核模块。
 *
 * 提供三种测试模式：
 *   MEMTEST_QUICK  - 快速测试：写入 0x55/0xAA 图案，验证所有可用页
 *   MEMTEST_FULL   - 完整测试：多种图案 + 走步测试(walking 1s/0s) + 地址唯一性
 *   MEMTEST_BASIC  - 基础测试：仅写 0x00 和 0xFF
 *
 * 执行模型：
 *   测试是"增量可中断"的。memtest_start() 只做初始化并统计可测字节数，
 *   之后由 memtest_step() 每次推进有限页数。GUI 每帧调一次
 *   SYS_MEMTEST_CTL(MT_OP_STATUS)，因此长测试不会在系统调用里阻塞内核，
 *   也不会让调度器饿死。批量路径可以直接用 memtest_run() 同步跑完。
 *
 * 安全机制：
 *   - 逐页调用 frame_is_used()：已分配给内核/驱动/进程的帧一律跳过，
 *     绝不写入（这是本模块唯一的越界防护，必须保留）
 *   - 窗口默认封顶 MEMTEST_DEFAULT_BYTES，避免一次会话跑不完
 *   - 支持随时取消
 *
 * 结果通过 memtest_result_t 结构返回（定义见 include/memtest.h，
 * 与 user/apps/memtest.c 共享，双方不得各自镜像结构体）。
 */

#include "common.h"
#include "frame.h"
#include "kernel.h"
#include "memory.h"
#include "memtest.h"
#include "string.h"

static memtest_result_t g_memtest;

/* ---- 内部辅助 ---- */

/* 对单个 64 位对齐地址写入并验证图案。
 * 返回 0=通过，非0=错误码（位掩码表示哪种图案失败）。
 */
static uint32_t memtest_check_addr(volatile uint64_t *addr, uint64_t pattern)
{
    uint64_t rd;
    *addr = pattern;
    __asm__ volatile ("mfence" ::: "memory");
    rd = *addr;
    if (rd != pattern) {
        return 1u;
    }
    return 0u;
}

/* 走步测试：逐个 bit 翻转，检查地址线和数据线故障。
 * 对每个 8 字节单元，写入 walking 1 (0x01,0x02,0x04,...) 再验证。
 */
static uint32_t memtest_walking(volatile uint64_t *start, uint64_t count)
{
    uint64_t i;
    uint64_t bit;
    uint32_t fail = 0u;

    for (bit = 0; bit < 64u; bit++) {
        uint64_t pattern = 1ULL << bit;
        for (i = 0; i < count / 8u; i++) {
            if (g_memtest.cancelled) return 1u;
            fail |= memtest_check_addr(&start[i], pattern);
        }
    }
    return fail;
}

/* 地址唯一性测试：对每个地址写入其自身地址值，再读回验证。
 * 这能检测地址线短路/交叉。
 */
static uint32_t memtest_address_check(volatile uint64_t *start, uint64_t count)
{
    uint64_t i;
    uint32_t fail = 0u;
    uint64_t n = count / 8u;

    /* 写入地址值 */
    for (i = 0; i < n; i++) {
        if (g_memtest.cancelled) return 1u;
        start[i] = (uint64_t) (uintptr_t) &start[i];
    }
    __asm__ volatile ("mfence" ::: "memory");

    /* 读回验证 */
    for (i = 0; i < n; i++) {
        uint64_t expected = (uint64_t) (uintptr_t) &start[i];
        uint64_t actual = start[i];
        if (actual != expected) {
            fail = 1u;
            break;
        }
    }
    return fail;
}

/* 记录错误 */
static void memtest_record_error(uint64_t addr, uint64_t expected, uint64_t actual)
{
    if (g_memtest.error_count < MEMTEST_MAX_ERRORS) {
        memtest_error_t *e = &g_memtest.error_list[g_memtest.error_count];
        e->address = addr;
        e->expected = expected;
        e->actual = actual;
    }
    g_memtest.errors++;
    g_memtest.error_count++;
}

/* 测试一段物理内存范围（必须是空闲帧，调用者已确认）。
 * phys_start: 物理起始地址（4KB 对齐）
 * size: 测试字节数
 * mode: 测试模式
 */
static void memtest_range(uint64_t phys_start, uint64_t size, uint32_t mode)
{
    volatile uint64_t *ptr = (volatile uint64_t *) (uintptr_t) phys_start;
    uint64_t count = size;
    uint64_t i;
    uint64_t n;

    if (size == 0) return;
    n = count / 8u;

    if (mode == MEMTEST_BASIC) {
        /* 基础：0x00 和 0xFF */
        for (i = 0; i < n; i++) {
            if (g_memtest.cancelled) return;
            if (memtest_check_addr(&ptr[i], 0x0000000000000000ULL)) {
                memtest_record_error((uint64_t) (uintptr_t) &ptr[i],
                                     0x0000000000000000ULL, ptr[i]);
            }
        }
        for (i = 0; i < n; i++) {
            if (g_memtest.cancelled) return;
            if (memtest_check_addr(&ptr[i], 0xFFFFFFFFFFFFFFFFULL)) {
                memtest_record_error((uint64_t) (uintptr_t) &ptr[i],
                                     0xFFFFFFFFFFFFFFFFULL, ptr[i]);
            }
        }
    } else if (mode == MEMTEST_QUICK) {
        /* 快速：0x5555555555555555 和 0xAAAAAAAAAAAAAAAA */
        for (i = 0; i < n; i++) {
            if (g_memtest.cancelled) return;
            if (memtest_check_addr(&ptr[i], 0x5555555555555555ULL)) {
                memtest_record_error((uint64_t) (uintptr_t) &ptr[i],
                                     0x5555555555555555ULL, ptr[i]);
            }
        }
        for (i = 0; i < n; i++) {
            if (g_memtest.cancelled) return;
            if (memtest_check_addr(&ptr[i], 0xAAAAAAAAAAAAAAAAULL)) {
                memtest_record_error((uint64_t) (uintptr_t) &ptr[i],
                                     0xAAAAAAAAAAAAAAAAULL, ptr[i]);
            }
        }
    } else {
        /* 完整：多种图案 + 走步 + 地址测试 */
        static const uint64_t patterns[] = {
            0x0000000000000000ULL,
            0xFFFFFFFFFFFFFFFFULL,
            0x5555555555555555ULL,
            0xAAAAAAAAAAAAAAAAULL,
            0x0123456789ABCDEFULL,
            0xDEADBEEFCAFEBABEULL,
            0x0000000000000001ULL,
            0x8000000000000000ULL
        };
        uint32_t p;
        uint64_t pat_count = sizeof(patterns) / sizeof(patterns[0]);

        for (p = 0; p < pat_count; p++) {
            for (i = 0; i < n; i++) {
                if (g_memtest.cancelled) return;
                if (memtest_check_addr(&ptr[i], patterns[p])) {
                    memtest_record_error((uint64_t) (uintptr_t) &ptr[i],
                                         patterns[p], ptr[i]);
                }
            }
        }

        /* 走步测试 */
        if (!g_memtest.cancelled) {
            (void) memtest_walking(ptr, count);
        }

        /* 地址唯一性测试 */
        if (!g_memtest.cancelled) {
            if (memtest_address_check(ptr, count)) {
                memtest_record_error(phys_start, 0u, 0u);
            }
        }
    }
}

/* ---- 时间 ---- */

uint64_t memtest_elapsed_ms(void)
{
    uint64_t hz = (uint64_t) timer_hz();
    uint64_t end;

    if (hz == 0u) return 0u;
    end = g_memtest.running ? timer_ticks() : g_memtest.end_tick;
    if (end <= g_memtest.start_tick) return 0u;
    return (end - g_memtest.start_tick) * 1000ULL / hz;
}

/* ---- 窗口选择 ---- */

/* 统计 [start,end) 内未被分配的字节数。 */
static uint64_t memtest_count_free(uint64_t start, uint64_t end)
{
    uint64_t addr;
    uint64_t bytes = 0u;

    for (addr = start; addr + FRAME_PAGE_SIZE <= end; addr += FRAME_PAGE_SIZE) {
        if (!frame_is_used(addr)) {
            bytes += FRAME_PAGE_SIZE;
        }
    }
    return bytes;
}

/* 推导测试窗口：起点取空闲区起点（或调用者给定值），
 * 终点取 min(空闲区末尾, 起点 + MEMTEST_DEFAULT_BYTES)。 */
static bool memtest_pick_range(uint64_t *start, uint64_t *end)
{
    const frame_info_t *fi = frame_info();
    uint64_t lo;
    uint64_t hi;

    if (fi == NULL || fi->total_frames == 0u) {
        return false;
    }
    lo = fi->base;
    hi = fi->base + (uint64_t) fi->total_frames * FRAME_PAGE_SIZE;

    if (*start < lo || *start >= hi) {
        *start = lo;
    }
    if (hi <= *start) {
        return false;
    }
    if (*end == 0u || *end > hi) {
        *end = hi;
    }
    if (*end - *start > MEMTEST_DEFAULT_BYTES) {
        *end = *start + MEMTEST_DEFAULT_BYTES;
    }
    /* 窗口必须至少有一页并且 4KB 对齐 */
    if (*end <= *start + FRAME_PAGE_SIZE) {
        return false;
    }
    *end &= ~(uint64_t) (FRAME_PAGE_SIZE - 1u);
    if (*end <= *start) {
        return false;
    }
    return true;
}

/* ---- 对外接口 ---- */

int memtest_start(uint32_t mode, uint64_t range_start, uint64_t range_end)
{
    uint64_t start = range_start;
    uint64_t end = range_end;
    uint64_t total;

    if (mode > MEMTEST_MODE_MAX) {
        mode = MEMTEST_QUICK;
    }
    if (!memtest_pick_range(&start, &end)) {
        log_write("memtest: no testable window");
        return -1;
    }

    total = memtest_count_free(start, end);
    if (total == 0u) {
        log_write("memtest: window has no free frames");
        return -2;
    }

    memset(&g_memtest, 0, sizeof(g_memtest));
    g_memtest.mode = mode;
    g_memtest.range_start = start;
    g_memtest.range_end = end;
    g_memtest.cursor = start;
    g_memtest.total_bytes = total;
    g_memtest.error_list_size = MEMTEST_MAX_ERRORS;
    g_memtest.start_tick = timer_ticks();
    g_memtest.running = 1u;

    log_write("memtest: started");
    return 0;
}

uint32_t memtest_step(uint32_t budget_pages)
{
    uint32_t written = 0u;

    if (!g_memtest.running) {
        return 0u;
    }

    while (written < budget_pages && g_memtest.cursor < g_memtest.range_end) {
        uint64_t addr = g_memtest.cursor;
        uint64_t chunk = FRAME_PAGE_SIZE;

        g_memtest.cursor = addr + chunk;
        if (addr + chunk > g_memtest.range_end) {
            chunk = g_memtest.range_end - addr;
            g_memtest.cursor = g_memtest.range_end;
        }

        if (g_memtest.cancelled) {
            break;
        }
        /* 安全：已分配的帧绝不写入。这是本模块唯一的越界防护。 */
        if (frame_is_used(addr)) {
            continue;
        }

        memtest_range(addr, chunk, g_memtest.mode);
        g_memtest.tested_bytes += chunk;
        written++;
    }

    if (g_memtest.cancelled || g_memtest.cursor >= g_memtest.range_end) {
        g_memtest.running = 0u;
        g_memtest.end_tick = timer_ticks();
        if (!g_memtest.cancelled) {
            g_memtest.finished = 1u;
        }
        log_write(g_memtest.errors == 0u ? "memtest: done, no errors"
                                        : "memtest: done, errors found");
    }
    return g_memtest.running;
}

void memtest_cancel(void)
{
    g_memtest.cancelled = 1u;
}

const memtest_result_t *memtest_get_result(void)
{
    return &g_memtest;
}

uint32_t memtest_progress(void)
{
    if (g_memtest.total_bytes == 0) return 0;
    if (g_memtest.tested_bytes >= g_memtest.total_bytes) return 100;
    return (uint32_t) (g_memtest.tested_bytes * 100ULL / g_memtest.total_bytes);
}

/* 同步跑完整个窗口。用于无 GUI 的批处理 / 开机自检路径。
 * 注意：完整模式的代价随窗口线性增长，调用者应自备超时意识。 */
int memtest_run(uint64_t range_start, uint64_t range_end, uint32_t mode)
{
    int rc = memtest_start(mode, range_start, range_end);

    if (rc != 0) {
        return rc;
    }
    while (memtest_step(256u) != 0u) {
        /* 一直推进到结束或取消 */
    }
    return (int) g_memtest.errors;
}

int memtest_ctl(memtest_ctl_request_t *req)
{
    if (req == NULL) {
        return -1;
    }
    req->status_ready = 0u;

    switch (req->op) {
    case MT_OP_START:
        req->result = memtest_start(req->mode, req->range_start, req->range_end);
        /* 把实际选定的窗口回填给调用者 */
        req->range_start = g_memtest.range_start;
        req->range_end = g_memtest.range_end;
        break;
    case MT_OP_STOP:
        memtest_cancel();
        req->result = 0;
        break;
    case MT_OP_STATUS: {
        /* 每次状态查询推进一小步：完整模式每页代价极高，预算要相应收窄。 */
        uint32_t budget = (g_memtest.mode == MEMTEST_FULL) ? 16u : 256u;
        (void) memtest_step(budget);
        req->result = 0;
        break;
    }
    case MT_OP_INFO:
        req->result = 0;
        break;
    default:
        return -2;
    }

    g_memtest.elapsed_ms = memtest_elapsed_ms();
    req->status = g_memtest;
    req->status_ready = 1u;
    return 0;
}
