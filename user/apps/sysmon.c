/*
 * sysmon.c - Monios 统一系统监控面板。
 *
 * 标签页：
 *   概览   实时 CPU 占用曲线（最近 60 采样）+ 内存使用条 + 网络收发包速率
 *   进程   进程列表（PID/名称/CPU%/内存/状态），选中后可结束进程
 *
 * 数据来源：
 *   SYS_PROCESS_ENUM(51) / SYS_PROCESS_TERMINATE(52)
 *   SYS_MEMORY_STATS(79)
 *   app_get_system_status()
 *
 * 右键退出。刷新间隔默认 1 秒（100 ticks）。
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

#define SM_MAX_PROCS   16u
#define SM_CPU_HIST    60u

typedef struct {
    int32_t  pid;
    int32_t  parent_pid;
    char     name[32];
    char     state_name[12];
    uint32_t state;
    uint64_t cpu_user_ticks;
    uint64_t cpu_kernel_ticks;
    uint64_t cpu_total_ticks;
    uint64_t uptime_ticks;
    uint32_t mem_pages;
    uint32_t mem_kb;
    uint32_t thread_count;
} sm_proc_t;

typedef struct {
    uint32_t count;
    sm_proc_t entries[SM_MAX_PROCS];
} sm_proc_enum_t;

/* 与内核 memstats.h 镜像（仅取需要的字段）。 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t kernel_text_bytes;
    uint64_t heap_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_free_bytes;
    uint64_t page_table_bytes;
    uint64_t reserved_bytes;
    uint32_t kmalloc_alloc_count;
    uint32_t kmalloc_free_count;
    uint64_t high_water_used;
    uint32_t frame_total;
    uint32_t frame_used;
    uint32_t frame_reserved;
    uint32_t buddy_free_pages[13];
    uint32_t buddy_alloc_count;
    uint32_t buddy_free_count;
    uint32_t buddy_split_count;
    uint32_t pool_count;
    uint32_t pool_slots_total;
    uint32_t pool_slots_used;
    uint32_t pool_alloc_ops;
    uint32_t pool_free_ops;
    uint32_t proc_count;
    struct {
        int32_t  pid;
        char     name[32];
        uint32_t mem_kb;
        uint32_t mem_pages;
        uint64_t cpu_ticks;
    } procs[SM_MAX_PROCS];
} sm_mem_t;

#define SM_WIN_X   50
#define SM_WIN_Y   30
#define SM_WIN_W   940
#define SM_WIN_H   680

#define SM_CANVAS  0x00EAF0F6
#define SM_TEXT    0x001C2930
#define SM_MUTED   0x005C6A70
#define SM_ACCENT  0x0000717F
#define SM_BAR     0x003974D9
#define SM_GREEN   0x002F8E5D
#define SM_ORANGE  0x00D08A2E
#define SM_DANGER  0x00C83E50

static const char *const g_tabs[2] = { "概览", "进程" };

static sm_proc_enum_t g_enum;
static uint64_t g_prev_ticks[SM_MAX_PROCS];
static uint8_t  g_cpu_pct[SM_MAX_PROCS];
static uint8_t  g_cpu_hist[SM_CPU_HIST];
static uint32_t g_cpu_hist_pos;
static uint32_t g_cpu_cur;
static uint64_t g_last_refresh;
static uint32_t g_refresh_ticks = 100u; /* 1 秒 */
static app_system_status_t g_status;
static sm_mem_t g_mem;
static uint32_t g_have_mem;
static uint32_t g_prev_tx;
static uint32_t g_prev_rx;
static uint32_t g_net_tx_rate;
static uint32_t g_net_rx_rate;
static uint32_t g_tab;
static uint32_t g_selected;
static char     g_msg[96];

static void fmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
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

static void refresh_data(void)
{
    uint64_t now = app_ticks();
    uint64_t elapsed = now - g_last_refresh;
    uint32_t total = 0;
    uint32_t i;

    g_last_refresh = now;
    if (elapsed == 0) elapsed = 1;

    syscall1(SYS_PROCESS_ENUM, (uint64_t)&g_enum);
    for (i = 0; i < g_enum.count && i < SM_MAX_PROCS; i++) {
        sm_proc_t *p = &g_enum.entries[i];
        uint64_t prev = g_prev_ticks[i];
        uint64_t delta = (p->cpu_total_ticks >= prev)
                         ? (p->cpu_total_ticks - prev) : 0;
        uint32_t pct = (uint32_t)((delta * 100u) / elapsed);
        if (pct > 100) pct = 100;
        g_cpu_pct[i] = (uint8_t)pct;
        total += pct;
        g_prev_ticks[i] = p->cpu_total_ticks;
    }
    g_cpu_cur = total > 100 ? 100 : total;
    g_cpu_hist[g_cpu_hist_pos] = (uint8_t)g_cpu_cur;
    g_cpu_hist_pos = (g_cpu_hist_pos + 1u) % SM_CPU_HIST;

    app_get_system_status(&g_status);
    {
        uint32_t tx = g_status.net_tx_packets;
        uint32_t rx = g_status.net_rx_packets;
        uint64_t dtx = (tx >= g_prev_tx) ? (tx - g_prev_tx) : 0;
        uint64_t drx = (rx >= g_prev_rx) ? (rx - g_prev_rx) : 0;
        g_net_tx_rate = (uint32_t)((dtx * 1000u) / elapsed);
        g_net_rx_rate = (uint32_t)((drx * 1000u) / elapsed);
        g_prev_tx = tx;
        g_prev_rx = rx;
    }

    if (syscall1(SYS_MEMORY_STATS, (uint64_t)&g_mem) == 0) {
        g_have_mem = 1;
    }
}

static void draw_overview(void)
{
    uint16_t cx = (uint16_t)(SM_WIN_X + 24);
    uint16_t cy = 120;
    uint16_t cw = 560, ch = 200;
    uint32_t k;
    char line[128];
    char t[12];

    osui_card((osui_rect_t){(uint16_t)(cx - 12), (uint16_t)(cy - 26),
                           (uint16_t)(cw + 24), (uint16_t)(ch + 60)});
    draw_text(cx, (uint16_t)(cy - 16), "CPU 使用率（最近 60 采样）", SM_TEXT);
    app_graphics_fill_rect(cx, cy, cw, ch, 0x00F3F6F7);
    for (k = 1; k < 4; k++)
        app_graphics_fill_rect(cx, (uint16_t)(cy + ch * k / 4u), cw, 1, 0x00D5DEE0);
    for (k = 0; k < SM_CPU_HIST; k++) {
        uint32_t idx = (g_cpu_hist_pos + k) % SM_CPU_HIST;
        uint8_t v = g_cpu_hist[idx];
        uint16_t bx = (uint16_t)(cx + k * cw / SM_CPU_HIST);
        uint16_t bh = (uint16_t)(ch * v / 100u);
        if (bh == 0) bh = 1;
        app_graphics_fill_rect(bx, (uint16_t)(cy + ch - bh), 6, bh,
                               (k == SM_CPU_HIST - 1) ? SM_ACCENT : SM_BAR);
    }

    /* memory bar */
    {
        uint16_t my = (uint16_t)(cy + ch + 20);
        uint64_t used = g_have_mem ? g_mem.used_bytes : 0;
        uint64_t total = g_have_mem ? g_mem.total_bytes : 0;
        uint32_t used_w = 0;

        draw_text(cx, (uint16_t)(my - 16), "内存使用", SM_TEXT);
        app_graphics_fill_rect(cx, my, cw, 18, 0x00F3F6F7);
        if (total > 0) {
            used_w = (uint32_t)(cw * used / total);
        }
        app_graphics_fill_rect(cx, my, (uint16_t)used_w, 18, SM_GREEN);
        line[0] = '\0';
        append_str(line, "已用 ", sizeof(line));
        fmt_u32(t, g_have_mem ? (uint32_t)(used / 1024U) : 0);
        append_str(line, t, sizeof(line));
        append_str(line, " KB / 共 ", sizeof(line));
        fmt_u32(t, g_have_mem ? (uint32_t)(total / 1024U) : 0);
        append_str(line, t, sizeof(line));
        append_str(line, " KB", sizeof(line));
        draw_text(cx, (uint16_t)(my + 22), line, SM_MUTED);
    }

    /* network card */
    osui_card((osui_rect_t){(uint16_t)(SM_WIN_X + 620), 96, 280, 200});
    draw_text((uint16_t)(SM_WIN_X + 636), 110, "网络速率", SM_TEXT);
    line[0] = '\0';
    append_str(line, "发包 ", sizeof(line));
    fmt_u32(t, g_net_tx_rate); append_str(line, t, sizeof(line));
    append_str(line, " p/s", sizeof(line));
    draw_text((uint16_t)(SM_WIN_X + 636), 140, line, SM_TEXT);
    line[0] = '\0';
    append_str(line, "收包 ", sizeof(line));
    fmt_u32(t, g_net_rx_rate); append_str(line, t, sizeof(line));
    append_str(line, " p/s", sizeof(line));
    draw_text((uint16_t)(SM_WIN_X + 636), 166, line, SM_TEXT);
    line[0] = '\0';
    append_str(line, "运行线程 ", sizeof(line));
    fmt_u32(t, g_status.task_count); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(SM_WIN_X + 636), 192, line, SM_MUTED);
    line[0] = '\0';
    append_str(line, "进程数 ", sizeof(line));
    fmt_u32(t, g_enum.count); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(SM_WIN_X + 636), 218, line, SM_MUTED);
}

static void draw_procs(void)
{
    uint16_t y = 110;
    uint32_t i;

    draw_text((uint16_t)(SM_WIN_X + 16), y, "PID", SM_MUTED);
    draw_text((uint16_t)(SM_WIN_X + 80), y, "名称", SM_MUTED);
    draw_text((uint16_t)(SM_WIN_X + 360), y, "CPU%", SM_MUTED);
    draw_text((uint16_t)(SM_WIN_X + 460), y, "内存KB", SM_MUTED);
    y += 22;
    for (i = 0; i < g_enum.count && i < SM_MAX_PROCS; i++) {
        sm_proc_t *p = &g_enum.entries[i];
        char line[96];
        char t[12];

        if (i == g_selected) {
            app_graphics_fill_rect((uint16_t)(SM_WIN_X + 8),
                                   y, 860, 20, 0x00DCE6F5);
        }
        line[0] = '\0';
        fmt_u32(t, (uint32_t)p->pid); append_str(line, t, sizeof(line));
        draw_text((uint16_t)(SM_WIN_X + 16), y, line, SM_TEXT);

        draw_text((uint16_t)(SM_WIN_X + 80), y, p->name, SM_TEXT);

        line[0] = '\0';
        fmt_u32(t, g_cpu_pct[i]); append_str(line, t, sizeof(line));
        append_str(line, "%", sizeof(line));
        draw_text((uint16_t)(SM_WIN_X + 360), y, line, SM_TEXT);

        line[0] = '\0';
        fmt_u32(t, p->mem_kb); append_str(line, t, sizeof(line));
        draw_text((uint16_t)(SM_WIN_X + 460), y, line, SM_TEXT);

        y += 24;
    }
    if (g_msg[0] != '\0') {
        draw_text((uint16_t)(SM_WIN_X + 16), (uint16_t)(SM_WIN_Y + SM_WIN_H - 90),
                  g_msg, SM_ACCENT);
    }
    osui_button((osui_rect_t){(uint16_t)(SM_WIN_X + 16),
                              (uint16_t)(SM_WIN_Y + SM_WIN_H - 60),
                              160, 32},
                "结束选中进程", OSUI_BUTTON_DANGER);
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

static void handle_click(int32_t mx, int32_t my)
{
    osui_rect_t tab = { (uint16_t)(SM_WIN_X + 8), 96,
                        (uint16_t)(SM_WIN_W - 16), 32 };
    uint32_t tw = tab.width / 2u;
    uint32_t i;

    for (i = 0; i < 2; i++) {
        osui_rect_t tr = { (uint16_t)(tab.x + i * tw), tab.y,
                           (uint16_t)tw, tab.height };
        if (in_rect(mx, my, tr)) { g_tab = i; return; }
    }
    if (g_tab == 1) {
        /* select row */
        for (i = 0; i < g_enum.count && i < SM_MAX_PROCS; i++) {
            osui_rect_t row = { (uint16_t)(SM_WIN_X + 8),
                                (uint16_t)(132 + i * 24), 860, 20 };
            if (in_rect(mx, my, row)) { g_selected = i; return; }
        }
        if (in_rect(mx, my, (osui_rect_t){(uint16_t)(SM_WIN_X + 16),
                                          (uint16_t)(SM_WIN_Y + SM_WIN_H - 60),
                                          160, 32})) {
            if (g_selected < g_enum.count) {
                int32_t pid = g_enum.entries[g_selected].pid;
                syscall1(SYS_PROCESS_TERMINATE, (uint64_t)pid);
                sprintf(g_msg, "terminated pid %d", (int)pid);
            }
        }
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;

    app_enter_graphics_mode();
    memset(&g_enum, 0, sizeof(g_enum));
    memset(&g_mem, 0, sizeof(g_mem));
    g_msg[0] = '\0';
    g_last_refresh = app_ticks();
    refresh_data();

    for (;;) {
        uint64_t now = app_ticks();
        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        if (now - g_last_refresh >= g_refresh_ticks) refresh_data();

        osui_canvas(SM_CANVAS);
        osui_panel((osui_rect_t){SM_WIN_X, SM_WIN_Y, SM_WIN_W, SM_WIN_H});
        osui_titlebar((osui_rect_t){SM_WIN_X, SM_WIN_Y, SM_WIN_W, 36},
                      "系统监视器", true);
        osui_tabbar((osui_rect_t){(uint16_t)(SM_WIN_X + 8), 96,
                                  (uint16_t)(SM_WIN_W - 16), 32},
                    g_tabs, 2, g_tab);
        if (g_tab == 0) draw_overview();
        else draw_procs();
        osui_statusbar((osui_rect_t){SM_WIN_X,
                                      (uint16_t)(SM_WIN_Y + SM_WIN_H - 28),
                                      SM_WIN_W, 28},
                       "sysmon", "右键退出", OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
