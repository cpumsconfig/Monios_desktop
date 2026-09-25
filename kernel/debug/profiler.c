/*
 * profiler.c - Monios CPU 采样性能分析器（Task 25）。
 *
 * 在定时器中断里按分频频率采样当前指令指针与内核栈回溯，用静态哈希表
 * 折叠统计每个调用栈的出现次数，为火焰图提供数据。全部静态存储、无锁、
 * 可安全在中断上下文调用。
 */

#include "profiler.h"
#include "common.h"
#include "exec.h"
#include "pcb.h"
#include "string.h"

/* ── 静态状态 ─────────────────────────────────────────────────── */
typedef struct {
    uint64_t frames[PROF_MAX_STACK]; /* 内层在前 */
    uint32_t frame_count;
    uint32_t pid;
    uint64_t count;
    uint32_t hash;                   /* 0 = 空槽 */
} prof_slot_t;

static prof_slot_t g_slots[PROF_MAX_ENTRIES];
static volatile bool g_running;
static uint32_t g_div = PROF_DEFAULT_DIV;
static uint32_t g_tick_mod;          /* 分频计数器 */
static uint64_t g_total_samples;
static uint64_t g_dropped;

void profiler_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    g_running = false;
    g_div = PROF_DEFAULT_DIV;
    g_tick_mod = 0;
    g_total_samples = 0;
    g_dropped = 0;
}

/* ── FNV-1a 哈希 ─────────────────────────────────────────────── */
static uint32_t fnv1a(const uint64_t *frames, uint32_t n, uint32_t pid)
{
    uint32_t h = 2166136261u;
    h ^= pid;
    h *= 16777619u;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t v = frames[i];
        for (int b = 0; b < 8; b++) {
            h ^= (uint8_t)(v & 0xFFu);
            h *= 16777619u;
            v >>= 8;
        }
    }
    return h ? h : 1u; /* 0 保留给空槽 */
}

/* ── 栈回溯：沿 rbp 帧链取返回地址 ──────────────────────────────
 * x86_64 帧布局：[rbp]=父帧 rbp, [rbp+8]=返回地址。
 * 与 crash_dump.c 的 build_backtrace() 同一方式。 */
static uint32_t unwind_kernel(uint64_t fp, uint64_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (fp >= 0x100000ULL && fp < 0x00007FFFFFFFFFFFULL && n < max) {
        uint64_t *frame = (uint64_t *)(uintptr_t)fp;
        uint64_t next_fp = frame[0];
        uint64_t ret = frame[1];
        if (ret < 0x2000000ULL || ret > 0x00007FFFFFFFFFFFULL) {
            break;
        }
        out[n++] = ret;
        if (next_fp <= fp) {
            break;
        }
        fp = next_fp;
    }
    return n;
}

/* ── 中断里的采样钩子 ────────────────────────────────────────── */
void profiler_timer_tick(uint64_t *saved)
{
    if (!g_running) {
        return;
    }
    /* 分频：每 g_div 个 tick 采一次。 */
    g_tick_mod++;
    if (g_tick_mod < g_div) {
        return;
    }
    g_tick_mod = 0;

    uint64_t rip = saved[16];
    uint64_t cs  = saved[17];
    bool from_user = (cs & 3u) == 3u;

    uint64_t stack[PROF_MAX_STACK];
    uint32_t n = 0;
    stack[n++] = rip; /* frames[0] = 被打断时的指令指针 */

    if (!from_user) {
        /* 内核态被打断：saved[6] 是被打断时的内核 rbp，沿帧链回溯调用者。 */
        uint64_t kbp = saved[6];
        uint32_t got = unwind_kernel(kbp, &stack[n], PROF_MAX_STACK - n);
        n += got;
    }
    /* 用户态被打断时：saved[6] 是用户 rbp，不可靠且属于用户地址空间，
     * 这里只记录用户 RIP 这一帧，避免越界访问用户栈。 */

    uint32_t pid = (uint32_t)pcb_current_pid();
    uint32_t h = fnv1a(stack, n, pid);

    /* 开放寻址线性探测。 */
    uint32_t idx = h & (PROF_MAX_ENTRIES - 1u);
    for (uint32_t probe = 0; probe < PROF_MAX_ENTRIES; probe++) {
        prof_slot_t *s = &g_slots[idx];
        if (s->hash == 0) {
            /* 新槽位：插入。 */
            s->hash = h;
            s->pid = pid;
            s->frame_count = n;
            s->count = 1;
            for (uint32_t i = 0; i < n; i++) s->frames[i] = stack[i];
            g_total_samples++;
            return;
        }
        if (s->hash == h && s->pid == pid && s->frame_count == n) {
            bool same = true;
            for (uint32_t i = 0; i < n; i++) {
                if (s->frames[i] != stack[i]) { same = false; break; }
            }
            if (same) {
                s->count++;
                g_total_samples++;
                return;
            }
        }
        idx = (idx + 1u) & (PROF_MAX_ENTRIES - 1u);
    }
    /* 表满：丢弃该采样。 */
    g_dropped++;
}

/* ── 控制接口 ─────────────────────────────────────────────────── */
int profiler_start(uint32_t divider)
{
    if (divider == 0) {
        divider = 1;
    }
    memset(g_slots, 0, sizeof(g_slots));
    g_total_samples = 0;
    g_dropped = 0;
    g_tick_mod = 0;
    g_div = divider;
    g_running = true;
    return 0;
}

void profiler_stop(void)
{
    g_running = false;
}

bool profiler_is_running(void)
{
    return g_running;
}

uint64_t profiler_total_samples(void)
{
    return g_total_samples;
}

uint32_t profiler_entry_count(void)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < PROF_MAX_ENTRIES; i++) {
        if (g_slots[i].hash != 0) c++;
    }
    return c;
}

/* ── 拷贝到用户空间 ─────────────────────────────────────────────
 * 布局：prof_header_t 后跟 entry_count 个 prof_entry_t。
 * 调用方（syscall.c）已经做过用户地址范围检查。 */
int64_t profiler_copy_to_user(void *buf, uint64_t buf_bytes)
{
    uint32_t count = profiler_entry_count();
    uint64_t need = sizeof(prof_header_t) + (uint64_t)count * sizeof(prof_entry_t);
    if (buf == 0 || buf_bytes < need) {
        return -1;
    }

    prof_header_t hdr;
    hdr.magic = PROF_MAGIC;
    hdr.entry_count = count;
    hdr.total_samples = g_total_samples;
    hdr.max_stack = PROF_MAX_STACK;
    hdr.div = g_div;
    hdr.dropped = g_dropped;

    memcpy(buf, &hdr, sizeof(hdr));
    prof_entry_t *out = (prof_entry_t *)((uint8_t *)buf + sizeof(hdr));

    uint32_t w = 0;
    for (uint32_t i = 0; i < PROF_MAX_ENTRIES && w < count; i++) {
        if (g_slots[i].hash == 0) continue;
        out[w].count = g_slots[i].count;
        out[w].pid = g_slots[i].pid;
        out[w].frame_count = g_slots[i].frame_count;
        for (uint32_t f = 0; f < PROF_MAX_STACK; f++) {
            out[w].frames[f] = g_slots[i].frames[f];
        }
        w++;
    }
    return (int64_t)need;
}
