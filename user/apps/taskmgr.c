#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/*
 * taskmgr.exe - Monios task manager (Task 13).
 *
 * Usage:
 *   taskmgr                 -> graphical window (process list / performance /
 *                               details tabs, live refresh every second)
 *   taskmgr kill <pid>      -> console mode: terminate a process via syscall 52
 *
 * The GUI is rendered with the OSUI component library (titlebar / tabbar /
 * list_item / button / statusbar / progress / callout) on top of the raw
 * windows_fill_rect / draw_text primitives. Process enumeration uses syscall
 * 51 (SYS_PROCESS_ENUM); if the kernel dispatch returns nothing, the window
 * still renders completely and falls back to the global system status.
 */

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
} app_process_info_t;

typedef struct {
    uint32_t count;
    app_process_info_t entries[16];
} app_process_enum_t;

#define TM_MAX_PROCS       16u
#define TM_CPU_HIST        120u
#define TM_ROW_H           26u
#define TM_REFRESH_TICKS  100u

/* window geometry */
#define TM_WIN_X   60
#define TM_WIN_Y   40
#define TM_WIN_W   900
#define TM_WIN_H   680
#define TM_BODY_Y  140
#define TM_BODY_H  (TM_WIN_H - 108)

/* colors (0x00RRGGBB) */
#define TM_CANVAS   0x00EAF0F6
#define TM_TEXT     0x001C2930
#define TM_MUTED    0x005C6A70
#define TM_ACCENT   0x0000717F
#define TM_DANGER   0x00C83E50
#define TM_BAR      0x003974D9
#define TM_BAR_ALT  0x002F8E5D

static const char *const g_tabs[3] = { "进程", "性能", "详细信息" };

/* ---------------- state ---------------- */
static app_process_enum_t g_enum;          /* latest snapshot */
static uint64_t g_prev_ticks[TM_MAX_PROCS];/* cpu_total_ticks per matched pid */
static uint32_t g_cpu_pct[TM_MAX_PROCS];   /* computed % per entry */
static uint8_t  g_cpu_hist[TM_CPU_HIST];   /* global cpu history ring */
static uint32_t g_cpu_hist_pos;
static uint32_t g_cpu_avg;
static uint32_t g_cpu_max;
static uint32_t g_mem_used_kb;
static int      g_enum_ok;
static uint32_t g_tab;                      /* 0 list, 1 perf, 2 details */
static uint32_t g_sort;                     /* 0 name 1 pid 2 cpu 3 mem */
static int      g_sort_desc;
static int      g_selected = -1;
static int      g_confirm_open;
static int32_t  g_confirm_pid;
static char     g_confirm_name[32];
static uint64_t g_last_refresh;
static app_system_status_t g_status;

/* ---------------- tiny formatting helpers ---------------- */
static void fmt_u32(char *out, uint32_t v)
{
    char tmp[10];
    int i = 0;
    int j = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0) {
        tmp[i++] = (char) ('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    out[j] = '\0';
}

static void fmt_s32(char *out, int32_t v)
{
    if (v < 0) {
        out[0] = '-';
        fmt_u32(out + 1, (uint32_t) (-v));
    } else {
        fmt_u32(out, (uint32_t) v);
    }
}

static void append_str(char *out, const char *s, uint32_t cap)
{
    uint32_t l = (uint32_t) strlen(out);
    while (*s && l + 1 < cap) {
        out[l++] = *s++;
    }
    out[l] = '\0';
}

/* ---------------- console mode (kill) ---------------- */
static uint32_t parse_u32(const char *text)
{
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t) (*text - '0');
        text++;
    }
    return value;
}

static int console_kill(const char *pid_arg)
{
    uint32_t pid = parse_u32(pid_arg);
    uint64_t ret = syscall1(SYS_PROCESS_TERMINATE, (uint64_t) pid);

    fputs("kill pid ");
    {
        char buf[12];
        fmt_u32(buf, pid);
        fputs(buf);
    }
    fputs(": ");
    fputs((int64_t) ret == 0 ? "ok" : "failed");
    fputs("\r\n");
    return 0;
}

/* ---------------- data refresh ---------------- */
static int find_entry_by_pid(int32_t pid)
{
    uint32_t i;
    for (i = 0; i < g_enum.count; i++) {
        if (g_enum.entries[i].pid == pid) {
            return (int) i;
        }
    }
    return -1;
}

static void refresh_data(void)
{
    app_process_enum_t fresh;
    uint64_t ret;
    uint32_t i;
    uint64_t now = app_ticks();
    uint64_t elapsed = now - g_last_refresh;
    uint32_t total_pct = 0;

    if (elapsed == 0) {
        elapsed = 1;
    }
    g_last_refresh = now;

    ret = syscall1(SYS_PROCESS_ENUM, (uint64_t) &fresh);
    if ((int64_t) ret < 0 || fresh.count == 0 || fresh.count > TM_MAX_PROCS) {
        g_enum_ok = 0;
    } else {
        g_enum_ok = 1;
        memcpy(&g_enum, &fresh, sizeof(fresh));
    }

    g_mem_used_kb = 0;
    for (i = 0; i < g_enum.count; i++) {
        app_process_info_t *p = &g_enum.entries[i];
        uint64_t prev;
        uint64_t delta;
        uint32_t pct;
        int matched = find_entry_by_pid(p->pid);

        g_mem_used_kb += p->mem_kb;

        if (matched < 0 || g_prev_ticks[i] == 0) {
            /* first sight of this pid: no delta yet */
            g_cpu_pct[i] = 0;
            prev = p->cpu_total_ticks;
        } else {
            prev = g_prev_ticks[i];
        }
        if (p->cpu_total_ticks >= prev) {
            delta = p->cpu_total_ticks - prev;
        } else {
            delta = 0;
        }
        pct = (uint32_t) ((delta * 100u) / elapsed);
        if (pct > 100u) {
            pct = 100u;
        }
        g_cpu_pct[i] = pct;
        total_pct += pct;
        g_prev_ticks[i] = p->cpu_total_ticks;
    }

    {
        uint32_t global_pct = total_pct;
        if (global_pct > 100u) {
            global_pct = 100u;
        }
        g_cpu_hist[g_cpu_hist_pos] = (uint8_t) global_pct;
        g_cpu_hist_pos = (g_cpu_hist_pos + 1u) % TM_CPU_HIST;
        {
            uint32_t sum = 0;
            uint32_t maxv = 0;
            uint32_t k;
            for (k = 0; k < TM_CPU_HIST; k++) {
                sum += g_cpu_hist[k];
                if (g_cpu_hist[k] > maxv) {
                    maxv = g_cpu_hist[k];
                }
            }
            g_cpu_avg = sum / TM_CPU_HIST;
            g_cpu_max = maxv;
        }
    }

    app_get_system_status(&g_status);
}

/* ---------------- sort ---------------- */
/* insertion sort over a permutation array (n <= 16) */
static void sort_rows(uint8_t *order, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        uint32_t j = i;
        while (j > 0) {
            uint8_t tmp;
            uint32_t pi = order[j - 1];
            uint32_t qi = order[j];
            /* direct compare on g_enum */
            {
                int r;
                switch (g_sort) {
                case 1: r = (g_enum.entries[qi].pid < g_enum.entries[pi].pid) ? -1
                          : (g_enum.entries[qi].pid > g_enum.entries[pi].pid) ? 1 : 0; break;
                case 2: r = (g_cpu_pct[qi] < g_cpu_pct[pi]) ? -1
                          : (g_cpu_pct[qi] > g_cpu_pct[pi]) ? 1 : 0; break;
                case 3: r = (g_enum.entries[qi].mem_kb < g_enum.entries[pi].mem_kb) ? -1
                          : (g_enum.entries[qi].mem_kb > g_enum.entries[pi].mem_kb) ? 1 : 0; break;
                default: r = strcmp(g_enum.entries[qi].name, g_enum.entries[pi].name); break;
                }
                if (g_sort_desc) r = -r;
                if (r < 0) {
                    tmp = order[j - 1];
                    order[j - 1] = order[j];
                    order[j] = tmp;
                    j--;
                } else {
                    break;
                }
            }
        }
    }
}

/* ---------------- drawing helpers ---------------- */
static void draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    app_graphics_draw_text(x, y, text, color);
}

static void draw_u32(uint16_t x, uint16_t y, uint32_t v, uint32_t color)
{
    char buf[12];
    fmt_u32(buf, v);
    draw_text(x, y, buf, color);
}

/* ---------------- process tab ---------------- */
static void draw_process_tab(void)
{
    uint8_t order[TM_MAX_PROCS];
    uint32_t n;
    uint32_t i;
    const char *col_names[4] = { "名称", "PID", "CPU", "内存(KB)" };
    uint16_t col_x[4] = { (uint16_t) (TM_WIN_X + 14), (uint16_t) (TM_WIN_X + 330),
                           (uint16_t) (TM_WIN_X + 430), (uint16_t) (TM_WIN_X + 520) };

    /* column header strip */
    app_graphics_fill_rect(TM_WIN_X + 8, TM_BODY_Y, TM_WIN_W - 16, 22, 0x00FFFFFF);
    for (i = 0; i < 4; i++) {
        char label[24];
        const char *name = col_names[i];
        uint32_t l = 0;
        while (name[l]) { label[l] = name[l]; l++; }
        label[l] = '\0';
        if (g_sort == i) {
            append_str(label, g_sort_desc ? " v" : " ^", sizeof(label));
        }
        draw_text(col_x[i], (uint16_t) (TM_BODY_Y + 4), label,
                  (g_sort == i) ? TM_ACCENT : TM_MUTED);
    }
    app_graphics_fill_rect(TM_WIN_X + 8, (uint16_t) (TM_BODY_Y + 21), TM_WIN_W - 16, 1,
                           0x00D5DEE0);

    if (!g_enum_ok) {
        osui_callout((osui_rect_t) { (uint16_t) (TM_WIN_X + 120), (uint16_t) (TM_BODY_Y + 80),
                                     660, 90 },
                     "进程信息暂不可用",
                     "内核进程枚举尚未就绪；此处仍可查看全局状态。",
                     OSUI_STATE_WARNING);
        return;
    }

    n = g_enum.count;
    for (i = 0; i < n; i++) {
        order[i] = (uint8_t) i;
    }
    sort_rows(order, n);

    for (i = 0; i < n; i++) {
        app_process_info_t *p = &g_enum.entries[order[i]];
        char sub[96];
        uint32_t pct = g_cpu_pct[order[i]];
        osui_rect_t rr = { (uint16_t) (TM_WIN_X + 8),
                           (uint16_t) (TM_BODY_Y + 26 + i * TM_ROW_H),
                           (uint16_t) (TM_WIN_W - 16),
                           (uint16_t) (TM_ROW_H - 2) };
        uint32_t state = 0;

        if (rr.y + TM_ROW_H > TM_WIN_Y + TM_WIN_H - 40) {
            break; /* viewport overflow */
        }
        if ((int) order[i] == g_selected) {
            state = OSUI_STATE_SELECTED;
        }

        fmt_s32(sub, p->pid);
        append_str(sub, "  ", sizeof(sub));
        append_str(sub, p->state_name[0] ? p->state_name : "running", sizeof(sub));
        append_str(sub, "  CPU ", sizeof(sub));
        {
            char tmp[8];
            fmt_u32(tmp, pct);
            append_str(sub, tmp, sizeof(sub));
        }
        append_str(sub, "%  ", sizeof(sub));
        {
            char tmp[12];
            fmt_u32(tmp, p->mem_kb);
            append_str(sub, tmp, sizeof(sub));
        }

        osui_list_item(rr, p->name, sub, state);
    }

    /* terminate button */
    osui_button((osui_rect_t) { (uint16_t) (TM_WIN_X + TM_WIN_W - 170),
                                (uint16_t) (TM_WIN_Y + TM_WIN_H - 62), 140, 30 },
                "结束任务", OSUI_BUTTON_DANGER);
}

/* ---------------- performance tab ---------------- */
static void draw_perf_tab(void)
{
    uint16_t chart_x = (uint16_t) (TM_WIN_X + 24);
    uint16_t chart_y = (uint16_t) (TM_BODY_Y + 30);
    uint16_t chart_w = 560;
    uint16_t chart_h = 200;
    uint32_t k;

    osui_card((osui_rect_t) { chart_x - 12, chart_y - 24, chart_w + 24, chart_h + 70 });
    draw_text(chart_x, chart_y - 16, "CPU 使用率（每秒采样）", TM_TEXT);

    /* chart background + grid */
    app_graphics_fill_rect(chart_x, chart_y, chart_w, chart_h, 0x00F3F6F7);
    for (k = 1; k < 4; k++) {
        app_graphics_fill_rect(chart_x, (uint16_t) (chart_y + chart_h * k / 4u),
                               chart_w, 1, 0x00D5DEE0);
    }

    /* bars: newest samples drawn left-to-right in ring order */
    for (k = 0; k < TM_CPU_HIST; k++) {
        uint32_t idx = (g_cpu_hist_pos + k) % TM_CPU_HIST;
        uint8_t v = g_cpu_hist[idx];
        uint16_t bx = (uint16_t) (chart_x + k * chart_w / TM_CPU_HIST);
        uint16_t bh = (uint16_t) (chart_h * v / 100u);
        if (bh == 0) {
            bh = 1;
        }
        app_graphics_fill_rect(bx, (uint16_t) (chart_y + chart_h - bh), 3, bh,
                               (k == TM_CPU_HIST - 1) ? TM_ACCENT : TM_BAR);
    }

    {
        char line[96];
        uint8_t cur = g_cpu_hist[(g_cpu_hist_pos + TM_CPU_HIST - 1u) % TM_CPU_HIST];
        line[0] = '\0';
        append_str(line, "当前 ", sizeof(line));
        {
            char t[8]; fmt_u32(t, cur); append_str(line, t, sizeof(line));
        }
        append_str(line, "%   平均 ", sizeof(line));
        {
            char t[8]; fmt_u32(t, g_cpu_avg); append_str(line, t, sizeof(line));
        }
        append_str(line, "%   最大 ", sizeof(line));
        {
            char t[8]; fmt_u32(t, g_cpu_max); append_str(line, t, sizeof(line));
        }
        append_str(line, "%", sizeof(line));
        draw_text(chart_x, (uint16_t) (chart_y + chart_h + 8), line, TM_MUTED);
    }

    /* memory card */
    osui_card((osui_rect_t) { (uint16_t) (TM_WIN_X + 608), (uint16_t) (TM_BODY_Y + 6),
                              256, 130 });
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 18), "内存", TM_TEXT);
    draw_u32((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 44), g_mem_used_kb, TM_TEXT);
    draw_text((uint16_t) (TM_WIN_X + 624 + 80), (uint16_t) (TM_BODY_Y + 44), "KB 已用（按进程求和）", TM_MUTED);
    osui_progress((osui_rect_t) { (uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 78),
                                   224, 14 },
                  g_enum_ok ? 50u : 0u);
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 102),
              "分页/非分页池：暂未上报", TM_MUTED);

    /* network card */
    osui_card((osui_rect_t) { (uint16_t) (TM_WIN_X + 608), (uint16_t) (TM_BODY_Y + 150),
                              256, 130 });
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 162), "网络活动", TM_TEXT);
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 188), "发送包:", TM_MUTED);
    draw_u32((uint16_t) (TM_WIN_X + 690), (uint16_t) (TM_BODY_Y + 188),
             g_status.net_tx_packets, TM_TEXT);
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 214), "接收包:", TM_MUTED);
    draw_u32((uint16_t) (TM_WIN_X + 690), (uint16_t) (TM_BODY_Y + 214),
             g_status.net_rx_packets, TM_TEXT);
    draw_text((uint16_t) (TM_WIN_X + 624), (uint16_t) (TM_BODY_Y + 244),
              g_status.net_connected ? "已连接" : "未连接", TM_MUTED);

    /* disk card */
    osui_card((osui_rect_t) { chart_x - 12, (uint16_t) (TM_BODY_Y + 260), 584, 90 });
    draw_text(chart_x, (uint16_t) (TM_BODY_Y + 278), "磁盘活动", TM_TEXT);
    draw_text(chart_x, (uint16_t) (TM_BODY_Y + 306),
              "读写速率：内核尚未提供块层统计", TM_MUTED);
}

/* ---------------- details tab ---------------- */
static void draw_details_tab(void)
{
    uint32_t i;
    if (!g_enum_ok) {
        osui_callout((osui_rect_t) { (uint16_t) (TM_WIN_X + 120), (uint16_t) (TM_BODY_Y + 80),
                                     660, 90 },
                     "进程信息暂不可用",
                     "详细属性需要内核进程枚举支持。",
                     OSUI_STATE_WARNING);
        return;
    }
    for (i = 0; i < g_enum.count; i++) {
        app_process_info_t *p = &g_enum.entries[i];
        char line[128];
        osui_rect_t rr = { (uint16_t) (TM_WIN_X + 16),
                           (uint16_t) (TM_BODY_Y + 10 + i * 24),
                           (uint16_t) (TM_WIN_W - 32), 22 };
        if (rr.y + 24 > TM_WIN_Y + TM_WIN_H - 40) {
            break;
        }
        line[0] = '\0';
        append_str(line, p->name, sizeof(line));
        {
            char t[16];
            append_str(line, "  PID ", sizeof(line));
            fmt_s32(t, p->pid); append_str(line, t, sizeof(line));
            append_str(line, "  父PID ", sizeof(line));
            fmt_s32(t, p->parent_pid); append_str(line, t, sizeof(line));
            append_str(line, "  线程 ", sizeof(line));
            fmt_u32(t, p->thread_count); append_str(line, t, sizeof(line));
            append_str(line, "  内存 ", sizeof(line));
            fmt_u32(t, p->mem_kb); append_str(line, t, sizeof(line));
            append_str(line, "KB", sizeof(line));
        }
        draw_text(rr.x, (uint16_t) (rr.y + 4), line, TM_TEXT);
    }
}

/* ---------------- confirm dialog ---------------- */
static void draw_confirm_dialog(void)
{
    char msg[128];
    osui_rect_t panel = { (uint16_t) (TM_WIN_X + 150), 250, 600, 170 };

    /* dim the background */
    app_graphics_fill_rect(TM_WIN_X, TM_WIN_Y, TM_WIN_W, TM_WIN_H, 0x00000000);
    /* note: dimming via semi-transparent is unsupported; instead repaint panel */
    osui_panel(panel);
    osui_titlebar((osui_rect_t) { panel.x, panel.y, panel.width, 32 }, "确认结束任务", true);

    msg[0] = '\0';
    append_str(msg, "确定要结束任务 '", sizeof(msg));
    append_str(msg, g_confirm_name, sizeof(msg));
    append_str(msg, "' (PID ", sizeof(msg));
    {
        char t[12];
        fmt_s32(t, g_confirm_pid);
        append_str(msg, t, sizeof(msg));
    }
    append_str(msg, ") 吗？", sizeof(msg));
    draw_text((uint16_t) (panel.x + 24), (uint16_t) (panel.y + 60), msg, TM_TEXT);
    draw_text((uint16_t) (panel.x + 24), (uint16_t) (panel.y + 92),
              "该操作不可撤销。", TM_MUTED);

    osui_button((osui_rect_t) { (uint16_t) (panel.x + 380), (uint16_t) (panel.y + 120),
                                90, 32 }, "结束", OSUI_BUTTON_DANGER);
    osui_button((osui_rect_t) { (uint16_t) (panel.x + 486), (uint16_t) (panel.y + 120),
                                90, 32 }, "取消", OSUI_BUTTON_GHOST);
}

/* ---------------- hit testing ---------------- */
static int in_rect(int32_t x, int32_t y, osui_rect_t r)
{
    return x >= (int32_t) r.x && x < (int32_t) (r.x + r.width) &&
           y >= (int32_t) r.y && y < (int32_t) (r.y + r.height);
}

static void handle_click(int32_t mx, int32_t my)
{
    osui_rect_t tab_rect = { (uint16_t) (TM_WIN_X + 8), 106,
                             (uint16_t) (TM_WIN_W - 16), 34 };
    uint32_t tab_w = tab_rect.width / 3u;
    uint32_t i;

    if (g_confirm_open) {
        osui_rect_t panel = { (uint16_t) (TM_WIN_X + 150), 250, 600, 170 };
        if (in_rect(mx, my, (osui_rect_t) {
                (uint16_t) (panel.x + 380), (uint16_t) (panel.y + 120), 90, 32 })) {
            syscall1(SYS_PROCESS_TERMINATE, (uint64_t) g_confirm_pid);
            g_confirm_open = 0;
            g_selected = -1;
            refresh_data();
        } else if (in_rect(mx, my, (osui_rect_t) {
                       (uint16_t) (panel.x + 486), (uint16_t) (panel.y + 120), 90, 32 })) {
            g_confirm_open = 0;
        }
        return;
    }

    /* tabs */
    for (i = 0; i < 3; i++) {
        osui_rect_t tr = { (uint16_t) (tab_rect.x + i * tab_w), tab_rect.y,
                           (uint16_t) tab_w, tab_rect.height };
        if (in_rect(mx, my, tr)) {
            g_tab = i;
            return;
        }
    }

    if (g_tab == 0) {
        /* column headers: cycle sort */
        if (my >= TM_BODY_Y && my < TM_BODY_Y + 22) {
            uint16_t col_x[4] = { (uint16_t) (TM_WIN_X + 14), (uint16_t) (TM_WIN_X + 330),
                                  (uint16_t) (TM_WIN_X + 430), (uint16_t) (TM_WIN_X + 520) };
            for (i = 0; i < 4; i++) {
                if (mx >= col_x[i] && mx < col_x[i] + 90) {
                    if (g_sort == i) {
                        g_sort_desc = !g_sort_desc;
                    } else {
                        g_sort = i;
                        g_sort_desc = 0;
                    }
                    return;
                }
            }
        }
        /* rows */
        if (g_enum_ok) {
            uint8_t order[TM_MAX_PROCS];
            uint32_t n = g_enum.count;
            uint32_t k;
            for (k = 0; k < n; k++) {
                order[k] = (uint8_t) k;
            }
            sort_rows(order, n);
            for (k = 0; k < n; k++) {
                osui_rect_t rr = { (uint16_t) (TM_WIN_X + 8),
                                   (uint16_t) (TM_BODY_Y + 26 + k * TM_ROW_H),
                                   (uint16_t) (TM_WIN_W - 16), (uint16_t) (TM_ROW_H - 2) };
                if (in_rect(mx, my, rr)) {
                    g_selected = (int) order[k];
                    return;
                }
            }
        }
        /* terminate button */
        if (in_rect(mx, my, (osui_rect_t) {
                (uint16_t) (TM_WIN_X + TM_WIN_W - 170),
                (uint16_t) (TM_WIN_Y + TM_WIN_H - 62), 140, 30 })) {
            if (g_selected >= 0 && (uint32_t) g_selected < g_enum.count) {
                app_process_info_t *p = &g_enum.entries[g_selected];
                g_confirm_pid = p->pid;
                strcpy(g_confirm_name, p->name);
                g_confirm_open = 1;
            }
        }
    }
}

/* ---------------- main GUI loop ---------------- */
static int run_gui(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_buttons = 0;
    const char *menus = "文件(F)   选项(O)   查看(V)";
    char status_left[64];
    char status_right[64];

    app_enter_graphics_mode();
    memset(&g_enum, 0, sizeof(g_enum));
    memset(g_prev_ticks, 0, sizeof(g_prev_ticks));
    memset(g_cpu_hist, 0, sizeof(g_cpu_hist));
    g_last_refresh = app_ticks();
    refresh_data();

    for (;;) {
        uint64_t now = app_ticks();
        app_get_mouse(&mouse);

        /* left-button click edge */
        if ((mouse.buttons & 0x01u) && !(prev_buttons & 0x01u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        /* right click = exit */
        if (mouse.buttons & 0x02u) {
            return 0;
        }
        prev_buttons = mouse.buttons;

        if (now - g_last_refresh >= TM_REFRESH_TICKS) {
            refresh_data();
        }

        /* ---- frame ---- */
        osui_canvas(TM_CANVAS);
        osui_panel((osui_rect_t) { TM_WIN_X, TM_WIN_Y, TM_WIN_W, TM_WIN_H });
        osui_titlebar((osui_rect_t) { TM_WIN_X, TM_WIN_Y, TM_WIN_W, 36 },
                      "任务管理器", true);

        /* menu bar */
        draw_text((uint16_t) (TM_WIN_X + 14), (uint16_t) (TM_WIN_Y + 44), menus, TM_MUTED);

        /* tabs */
        osui_tabbar((osui_rect_t) { (uint16_t) (TM_WIN_X + 8), 106,
                                    (uint16_t) (TM_WIN_W - 16), 34 },
                    g_tabs, 3, g_tab);

        if (g_tab == 0) {
            draw_process_tab();
        } else if (g_tab == 1) {
            draw_perf_tab();
        } else {
            draw_details_tab();
        }

        /* status bar */
        status_left[0] = '\0';
        append_str(status_left, "进程数 ", sizeof(status_left));
        {
            char t[12];
            fmt_u32(t, g_enum_ok ? g_enum.count : 0u);
            append_str(status_left, t, sizeof(status_left));
        }
        append_str(status_left, "   线程数 ", sizeof(status_left));
        {
            char t[12];
            fmt_u32(t, g_status.task_count);
            append_str(status_left, t, sizeof(status_left));
        }
        status_right[0] = '\0';
        append_str(status_right, "右键退出   每秒刷新", sizeof(status_right));
        osui_statusbar((osui_rect_t) { TM_WIN_X, (uint16_t) (TM_WIN_Y + TM_WIN_H - 28),
                                       TM_WIN_W, 28 },
                       status_left, status_right, OSUI_STATE_SUCCESS);

        if (g_confirm_open) {
            draw_confirm_dialog();
        }

        osui_present();
        app_sleep_ticks(10);
    }
}

int main(int argc, char **argv)
{
    /* `taskmgr kill <pid>` stays in console mode. */
    if (argc >= 3 && argv[1][0] == 'k' && argv[1][1] == 'i' && argv[1][2] == 'l') {
        return console_kill(argv[2]);
    }

    /* default: graphical task manager */
    return run_gui();
}
