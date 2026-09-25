/*
 * perfmon.c - Monios 图形化性能监视器（Task 25c）。
 *
 * 标签页：
 *   概览    实时 CPU 使用率 + 历史曲线
 *   火焰图  启动 5 秒采样，用 windows_fill_rect 画简化火焰图/调用栈排行
 *   进程    Top 10 进程 CPU 占用
 *   系统调用  系统概况（线程数/网络包等）
 *
 * 数据来源：
 *   SYS_PROFILER_START(76)/STOP(77)/GET_DATA(78)  采样器
 *   SYS_PROCESS_ENUM(51)                            进程列表
 *   app_get_system_status()                         全局状态
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* 与内核 include/profiler.h 保持一致的镜像结构。 */
#define PM_MAX_STACK    16u
#define PM_MAX_ENTRIES  1024u
#define PM_MAGIC        0x50524F46u

typedef struct {
    uint32_t magic;
    uint32_t entry_count;
    uint64_t total_samples;
    uint32_t max_stack;
    uint32_t div;
    uint64_t dropped;
} pm_prof_header_t;

typedef struct {
    uint64_t count;
    uint32_t pid;
    uint32_t frame_count;
    uint64_t frames[PM_MAX_STACK];
} pm_prof_entry_t;

typedef struct {
    int32_t pid;
    int32_t parent_pid;
    char name[32];
    char state_name[12];
    uint32_t state;
    uint64_t cpu_user_ticks;
    uint64_t cpu_kernel_ticks;
    uint64_t cpu_total_ticks;
    uint64_t uptime_ticks;
    uint32_t mem_pages;
    uint32_t mem_kb;
    uint32_t thread_count;
} pm_proc_t;

typedef struct {
    uint32_t count;
    pm_proc_t entries[16];
} pm_proc_enum_t;

/* 采样数据缓冲区：header + 全部 entry。 */
static char g_prof_buf[sizeof(pm_prof_header_t) +
                       PM_MAX_ENTRIES * sizeof(pm_prof_entry_t)];

/* 窗口几何 */
#define PM_WIN_X   60
#define PM_WIN_Y   40
#define PM_WIN_W   920
#define PM_WIN_H   660
#define PM_BODY_Y  140

/* 颜色 */
#define PM_CANVAS  0x00EAF0F6
#define PM_TEXT    0x001C2930
#define PM_MUTED   0x005C6A70
#define PM_ACCENT  0x0000717F
#define PM_BAR     0x003974D9
#define PM_BAR2    0x002F8E5D

static const char *const g_tabs[4] = { "概览", "火焰图", "进程", "系统调用" };

static uint32_t g_tab;
static pm_proc_enum_t g_enum;
static uint64_t g_prev_proc_ticks[16];
static uint8_t  g_proc_pct[16];
static uint8_t  g_cpu_hist[120];
static uint32_t g_cpu_hist_pos;
static uint32_t g_cpu_cur;
static uint64_t g_last_refresh;
static app_system_status_t g_status;

/* 火焰图状态 */
static int      g_have_prof;
static uint64_t g_prof_total;
static uint32_t g_prof_entries;
static uint32_t g_sampling;       /* 1 = 正在采样倒计时 */
static uint64_t g_sample_start;

/* ── 格式化小工具 ────────────────────────────────────────────── */
static void fmt_u32(char *out, uint32_t v)
{
    char tmp[12]; int i = 0, j = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

static void fmt_hex(char *out, uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 12; i++) {
        out[2 + i] = hex[(v >> ((11 - i) * 4)) & 0xFu];
    }
    out[14] = '\0';
}

static void append_str(char *out, const char *s, uint32_t cap)
{
    uint32_t l = (uint32_t)strlen(out);
    while (*s && l + 1 < cap) out[l++] = *s++;
    out[l] = '\0';
}

static void draw_text(uint16_t x, uint16_t y, const char *t, uint32_t c)
{
    app_graphics_draw_text(x, y, t, c);
}

/* ── 数据刷新 ───────────────────────────────────────────────── */
static void refresh_data(void)
{
    uint64_t now = app_ticks();
    uint64_t elapsed = now - g_last_refresh;
    uint32_t total = 0;
    uint32_t i;
    g_last_refresh = now;
    if (elapsed == 0) elapsed = 1;

    syscall1(SYS_PROCESS_ENUM, (uint64_t)&g_enum);
    for (i = 0; i < g_enum.count && i < 16; i++) {
        pm_proc_t *p = &g_enum.entries[i];
        uint64_t prev = g_prev_proc_ticks[i];
        uint64_t delta = (p->cpu_total_ticks >= prev) ? (p->cpu_total_ticks - prev) : 0;
        uint32_t pct = (uint32_t)((delta * 100u) / elapsed);
        if (pct > 100) pct = 100;
        g_proc_pct[i] = (uint8_t)pct;
        total += pct;
        g_prev_proc_ticks[i] = p->cpu_total_ticks;
    }
    g_cpu_cur = total > 100 ? 100 : total;
    g_cpu_hist[g_cpu_hist_pos] = (uint8_t)g_cpu_cur;
    g_cpu_hist_pos = (g_cpu_hist_pos + 1u) % 120u;

    app_get_system_status(&g_status);

    /* 采样倒计时结束后自动停止并取数据。 */
    if (g_sampling && (now - g_sample_start) >= 500u) {
        syscall0(SYS_PROFILER_STOP);
        int64_t n = syscall2(SYS_PROFILER_GET_DATA,
                            (uint64_t)g_prof_buf, sizeof(g_prof_buf));
        pm_prof_header_t *h = (pm_prof_header_t *)g_prof_buf;
        if (n > 0 && h->magic == PM_MAGIC) {
            g_have_prof = 1;
            g_prof_total = h->total_samples;
            g_prof_entries = h->entry_count;
        }
        g_sampling = 0;
    }
}

/* ── 概览页 ─────────────────────────────────────────────────── */
static void draw_overview(void)
{
    uint16_t cx = (uint16_t)(PM_WIN_X + 24);
    uint16_t cy = (uint16_t)(PM_BODY_Y + 30);
    uint16_t cw = 600, ch = 220;
    uint32_t k;

    osui_card((osui_rect_t){(uint16_t)(cx - 12), (uint16_t)(cy - 24),
                           (uint16_t)(cw + 24), (uint16_t)(ch + 70)});
    draw_text(cx, (uint16_t)(cy - 16), "CPU 使用率（实时）", PM_TEXT);
    app_graphics_fill_rect(cx, cy, cw, ch, 0x00F3F6F7);
    for (k = 1; k < 4; k++)
        app_graphics_fill_rect(cx, (uint16_t)(cy + ch * k / 4u), cw, 1, 0x00D5DEE0);
    for (k = 0; k < 120; k++) {
        uint32_t idx = (g_cpu_hist_pos + k) % 120u;
        uint8_t v = g_cpu_hist[idx];
        uint16_t bx = (uint16_t)(cx + k * cw / 120u);
        uint16_t bh = (uint16_t)(ch * v / 100u);
        if (bh == 0) bh = 1;
        app_graphics_fill_rect(bx, (uint16_t)(cy + ch - bh), 4, bh,
                               (k == 119) ? PM_ACCENT : PM_BAR);
    }
    {
        char line[96]; char t[8];
        line[0] = '\0';
        append_str(line, "当前 ", sizeof(line));
        fmt_u32(t, g_cpu_cur); append_str(line, t, sizeof(line));
        append_str(line, "%   进程数 ", sizeof(line));
        fmt_u32(t, g_enum.count); append_str(line, t, sizeof(line));
        draw_text(cx, (uint16_t)(cy + ch + 8), line, PM_MUTED);
    }

    /* 状态卡片 */
    osui_card((osui_rect_t){(uint16_t)(PM_WIN_X + 640), (uint16_t)(PM_BODY_Y + 6),
                           240, 120});
    draw_text((uint16_t)(PM_WIN_X + 656), (uint16_t)(PM_BODY_Y + 18), "系统", PM_TEXT);
    {
        char line[96];
        line[0] = '\0';
        append_str(line, "线程 ", sizeof(line));
        fmt_u32(line + strlen(line), g_status.task_count);
        draw_text((uint16_t)(PM_WIN_X + 656), (uint16_t)(PM_BODY_Y + 48), line, PM_TEXT);
        line[0] = '\0';
        append_str(line, "发包 ", sizeof(line));
        fmt_u32(line + strlen(line), g_status.net_tx_packets);
        draw_text((uint16_t)(PM_WIN_X + 656), (uint16_t)(PM_BODY_Y + 74), line, PM_TEXT);
        line[0] = '\0';
        append_str(line, "收包 ", sizeof(line));
        fmt_u32(line + strlen(line), g_status.net_rx_packets);
        draw_text((uint16_t)(PM_WIN_X + 656), (uint16_t)(PM_BODY_Y + 100), line, PM_TEXT);
    }
}

/* ── 火焰图页 ───────────────────────────────────────────────── */
static void draw_flame(void)
{
    uint16_t x0 = (uint16_t)(PM_WIN_X + 16);
    uint16_t y  = (uint16_t)(PM_BODY_Y + 16);

    if (g_sampling) {
        draw_text(x0, y, "正在采样 5 秒…请稍候", PM_ACCENT);
        return;
    }
    if (!g_have_prof) {
        osui_callout((osui_rect_t){(uint16_t)(PM_WIN_X + 120), (uint16_t)(PM_BODY_Y + 60),
                                   660, 100},
                     "火焰图", "点击下方按钮开始 5 秒 CPU 采样。", OSUI_STATE_PRIMARY);
        osui_button((osui_rect_t){(uint16_t)(PM_WIN_X + 360), (uint16_t)(PM_BODY_Y + 180),
                                  200, 34},
                    "开始采样 5 秒", OSUI_BUTTON_PRIMARY);
        return;
    }

    /* 标题行 */
    {
        char line[96]; char t[12];
        line[0] = '\0';
        append_str(line, "采样 ", sizeof(line));
        fmt_u32(t, (uint32_t)g_prof_total); append_str(line, t, sizeof(line));
        append_str(line, " 次，栈条目 ", sizeof(line));
        fmt_u32(t, g_prof_entries); append_str(line, t, sizeof(line));
        draw_text(x0, y, line, PM_TEXT);
    }

    /* Top 10 栈：按 count 画横向条。 */
    {
        pm_prof_header_t *h = (pm_prof_header_t *)g_prof_buf;
        pm_prof_entry_t *e = (pm_prof_entry_t *)(g_prof_buf + sizeof(pm_prof_header_t));
        uint32_t n = h->entry_count;
        uint64_t maxc = 1;
        uint32_t i;
        for (i = 0; i < n; i++) if (e[i].count > maxc) maxc = e[i].count;

        y += 28;
        draw_text(x0, y, "采样最多的调用栈（条长=采样数，标签=叶子函数地址）", PM_MUTED);
        y += 10;
        for (i = 0; i < n && i < 10; i++) {
            /* 简单选择排序找最大的 n 个 */
            uint32_t best = i;
            uint32_t j;
            for (j = i + 1; j < n; j++) if (e[j].count > e[best].count) best = j;
            if (best != i) {
                pm_prof_entry_t te = e[i]; e[i] = e[best]; e[best] = te;
            }
            uint16_t bw = (uint16_t)(700u * e[i].count / maxc);
            app_graphics_fill_rect(x0, y, bw, 16, (i & 1) ? PM_BAR : PM_BAR2);
            {
                char line[128]; char hx[16];
                line[0] = '\0';
                fmt_hex(hx, e[i].frames[0]);
                append_str(line, hx, sizeof(line));
                append_str(line, "  pid=", sizeof(line));
                { char p[8]; fmt_u32(p, e[i].pid); append_str(line, p, sizeof(line)); }
                append_str(line, "  n=", sizeof(line));
                { char c[12]; fmt_u32(c, (uint32_t)e[i].count); append_str(line, c, sizeof(line)); }
                draw_text((uint16_t)(x0 + bw + 6), (uint16_t)(y + 2), line, PM_TEXT);
            }
            y += 22;
        }
    }
    osui_button((osui_rect_t){(uint16_t)(PM_WIN_X + 360), (uint16_t)(PM_WIN_Y + PM_WIN_H - 60),
                              200, 30},
                "重新采样", OSUI_BUTTON_GHOST);
}

/* ── 进程页 ─────────────────────────────────────────────────── */
static void draw_procs(void)
{
    uint16_t y = (uint16_t)(PM_BODY_Y + 10);
    uint32_t i;
    draw_text((uint16_t)(PM_WIN_X + 16), y, "进程 CPU 占用 Top 10", PM_TEXT);
    y += 24;
    for (i = 0; i < g_enum.count && i < 10; i++) {
        pm_proc_t *p = &g_enum.entries[i];
        char line[128]; char t[12];
        line[0] = '\0';
        append_str(line, p->name, sizeof(line));
        append_str(line, "  pid=", sizeof(line));
        fmt_u32(t, (uint32_t)p->pid); append_str(line, t, sizeof(line));
        append_str(line, "  CPU ", sizeof(line));
        fmt_u32(t, g_proc_pct[i]); append_str(line, t, sizeof(line));
        append_str(line, "%", sizeof(line));
        draw_text((uint16_t)(PM_WIN_X + 16), y, line, PM_TEXT);
        app_graphics_fill_rect((uint16_t)(PM_WIN_X + 260), (uint16_t)(y + 4),
                               (uint16_t)(300u * g_proc_pct[i] / 100u), 10, PM_BAR);
        y += 24;
    }
}

/* ── 系统调用页（简化：显示系统概况） ─────────────────────── */
static void draw_syscalls(void)
{
    uint16_t y = (uint16_t)(PM_BODY_Y + 20);
    draw_text((uint16_t)(PM_WIN_X + 16), y, "系统调用与内核概况", PM_TEXT);
    y += 30;
    draw_text((uint16_t)(PM_WIN_X + 16), y,
              "逐系统调用计数需内核增量埋点（本版本未提供）。", PM_MUTED);
    y += 24;
    {
        char line[128]; char t[12];
        line[0]='\0';
        append_str(line, "运行线程数 ", sizeof(line));
        fmt_u32(t, g_status.task_count); append_str(line, t, sizeof(line));
        draw_text((uint16_t)(PM_WIN_X + 16), y, line, PM_TEXT);
    }
}

/* ── 点击命中 ──────────────────────────────────────────────── */
static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

static void handle_click(int32_t mx, int32_t my)
{
    osui_rect_t tab = { (uint16_t)(PM_WIN_X + 8), 106,
                        (uint16_t)(PM_WIN_W - 16), 34 };
    uint32_t tw = tab.width / 4u;
    uint32_t i;
    for (i = 0; i < 4; i++) {
        osui_rect_t tr = { (uint16_t)(tab.x + i * tw), tab.y, (uint16_t)tw, tab.height };
        if (in_rect(mx, my, tr)) { g_tab = i; return; }
    }
    if (g_tab == 1 && !g_sampling) {
        if (!g_have_prof) {
            if (in_rect(mx, my, (osui_rect_t){(uint16_t)(PM_WIN_X + 360),
                                              (uint16_t)(PM_BODY_Y + 180), 200, 34})) {
                syscall1(SYS_PROFILER_START, 1u);
                g_sampling = 1;
                g_sample_start = app_ticks();
            }
        } else if (in_rect(mx, my, (osui_rect_t){(uint16_t)(PM_WIN_X + 360),
                                                 (uint16_t)(PM_WIN_Y + PM_WIN_H - 60), 200, 30})) {
            syscall1(SYS_PROFILER_START, 1u);
            g_sampling = 1;
            g_sample_start = app_ticks();
        }
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;

    app_enter_graphics_mode();
    memset(&g_enum, 0, sizeof(g_enum));
    g_last_refresh = app_ticks();
    refresh_data();

    for (;;) {
        uint64_t now = app_ticks();
        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;  /* 右键退出 */
        prev_btn = mouse.buttons;

        if (now - g_last_refresh >= 100u) refresh_data();

        osui_canvas(PM_CANVAS);
        osui_panel((osui_rect_t){PM_WIN_X, PM_WIN_Y, PM_WIN_W, PM_WIN_H});
        osui_titlebar((osui_rect_t){PM_WIN_X, PM_WIN_Y, PM_WIN_W, 36},
                      "性能监视器", true);
        osui_tabbar((osui_rect_t){(uint16_t)(PM_WIN_X + 8), 106,
                                  (uint16_t)(PM_WIN_W - 16), 34},
                    g_tabs, 4, g_tab);
        if (g_tab == 0) draw_overview();
        else if (g_tab == 1) draw_flame();
        else if (g_tab == 2) draw_procs();
        else draw_syscalls();

        osui_statusbar((osui_rect_t){PM_WIN_X, (uint16_t)(PM_WIN_Y + PM_WIN_H - 28),
                                     PM_WIN_W, 28},
                       "perfmon", "右键退出", OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
