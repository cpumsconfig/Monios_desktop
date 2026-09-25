/*
 * memmon.c - Monios 图形化内存监视器（Task 26b）。
 *
 * 标签页：
 *   概览   总量/已用/空闲大数字 + 进度条
 *   分布   堆叠条形图：内核/堆/页表/进程/空闲占比
 *   历史   最近 60 秒内存使用趋势曲线（应用端环形缓冲）
 *   进程   Top 10 内存占用进程
 *   分配器 buddy/pool/heap 计数详情
 *
 * 数据来源：SYS_MEMORY_STATS(79)。
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* 与内核 include/memstats.h 镜像。 */
#define MM_MAX_PROCS    16u
#define MM_BUDDY_ORDERS 13u
#define MM_MAGIC        0x4D454D53u

typedef struct {
    int32_t  pid;
    char     name[32];
    uint32_t mem_kb;
    uint32_t mem_pages;
    uint64_t cpu_ticks;
} mm_proc_t;

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
    uint32_t buddy_free_pages[MM_BUDDY_ORDERS];
    uint32_t buddy_alloc_count;
    uint32_t buddy_free_count;
    uint32_t buddy_split_count;
    uint32_t pool_count;
    uint32_t pool_slots_total;
    uint32_t pool_slots_used;
    uint32_t pool_alloc_ops;
    uint32_t pool_free_ops;
    uint32_t proc_count;
    mm_proc_t procs[MM_MAX_PROCS];
} mm_snap_t;

#define MM_WIN_X   60
#define MM_WIN_Y   40
#define MM_WIN_W   920
#define MM_WIN_H   640
#define MM_BODY_Y  140

#define MM_CANVAS  0x00EAF0F6
#define MM_TEXT    0x001C2930
#define MM_MUTED   0x005C6A70
#define MM_ACCENT  0x0000717F
#define MM_BLUE    0x003974D9
#define MM_GREEN   0x002F8E5D
#define MM_ORANGE  0x00D08A2E
#define MM_GRAY    0x009AA6AD

static const char *const g_tabs[5] = { "概览", "分布", "历史", "进程", "分配器" };
static uint32_t g_tab;
static mm_snap_t g_snap;
static uint64_t g_last_refresh;

/* 历史环形缓冲：已用字节 */
#define MM_HIST 60u
static uint64_t g_hist[MM_HIST];
static uint32_t g_hist_pos;

static void fmt_u32(char *out, uint32_t v)
{
    char tmp[12]; int i = 0, j = 0;
    if (v == 0) { out[0]='0'; out[1]='\0'; return; }
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
static void draw_kb(uint16_t x, uint16_t y, uint64_t bytes, uint32_t color)
{
    /* 转成 MB 显示 */
    uint64_t mb = bytes / (1024u * 1024u);
    char line[32]; char t[16];
    line[0]='\0';
    fmt_u32(t, (uint32_t)mb);
    append_str(line, t, sizeof(line));
    append_str(line, " MB", sizeof(line));
    draw_text(x, y, line, color);
}

static void refresh(void)
{
    uint64_t now = app_ticks();
    if (now - g_last_refresh < 100u) return;
    g_last_refresh = now;

    memset(&g_snap, 0, sizeof(g_snap));
    syscall1(SYS_MEMORY_STATS, (uint64_t)&g_snap);

    g_hist[g_hist_pos] = g_snap.used_bytes;
    g_hist_pos = (g_hist_pos + 1u) % MM_HIST;
}

static void draw_overview(void)
{
    uint16_t x = (uint16_t)(MM_WIN_X + 24);
    uint16_t y = (uint16_t)(MM_BODY_Y + 20);
    uint32_t pct = g_snap.total_bytes ?
                   (uint32_t)(g_snap.used_bytes * 100u / g_snap.total_bytes) : 0;

    osui_card((osui_rect_t){x, y, 420, 200});
    draw_text(x + 16, y + 16, "内存总览", MM_TEXT);
    draw_kb(x + 16, y + 50, g_snap.used_bytes, MM_ACCENT);
    draw_text(x + 160, y + 56, "已用", MM_MUTED);
    draw_kb(x + 16, y + 84, g_snap.free_bytes, MM_GREEN);
    draw_text(x + 160, y + 90, "空闲", MM_MUTED);
    draw_kb(x + 16, y + 118, g_snap.total_bytes, MM_TEXT);
    draw_text(x + 160, y + 124, "总量", MM_MUTED);
    osui_progress((osui_rect_t){x + 16, (uint16_t)(y + 150), 380, 16}, pct);
    {
        char line[32]; char t[8];
        line[0]='\0'; fmt_u32(t, pct);
        append_str(line, t, sizeof(line)); append_str(line, "% 已用", sizeof(line));
        draw_text(x + 200, (uint16_t)(y + 172), line, MM_MUTED);
    }

    /* 堆卡片 */
    osui_card((osui_rect_t){(uint16_t)(MM_WIN_X + 470), y, 400, 200});
    draw_text((uint16_t)(MM_WIN_X + 486), y + 16, "内核堆", MM_TEXT);
    draw_kb((uint16_t)(MM_WIN_X + 486), y + 50, g_snap.heap_used_bytes, MM_TEXT);
    draw_text((uint16_t)(MM_WIN_X + 650), y + 56, "已用", MM_MUTED);
    draw_kb((uint16_t)(MM_WIN_X + 486), y + 84, g_snap.heap_free_bytes, MM_GREEN);
    draw_text((uint16_t)(MM_WIN_X + 650), y + 90, "空闲", MM_MUTED);
    draw_kb((uint16_t)(MM_WIN_X + 486), y + 118, g_snap.high_water_used, MM_ORANGE);
    draw_text((uint16_t)(MM_WIN_X + 650), y + 124, "高水位", MM_MUTED);
}

static void draw_distribution(void)
{
    uint16_t x = (uint16_t)(MM_WIN_X + 24);
    uint16_t y = (uint16_t)(MM_BODY_Y + 40);
    uint16_t w = 840;
    uint64_t total = g_snap.total_bytes ? g_snap.total_bytes : 1;
    /* 分段：内核代码 / 堆 / 页表 / 保留 / 空闲 */
    uint64_t segs[5];
    uint32_t colors[5] = { MM_BLUE, MM_GREEN, MM_ORANGE, MM_GRAY, 0x00DDE4E8 };
    const char *names[5] = { "内核代码", "内核堆", "页表", "保留", "空闲" };
    segs[0] = g_snap.kernel_text_bytes;
    segs[1] = g_snap.heap_used_bytes;
    segs[2] = g_snap.page_table_bytes;
    segs[3] = g_snap.reserved_bytes;
    segs[4] = g_snap.free_bytes;

    draw_text(x, (uint16_t)(y - 24), "内存分布（堆叠条）", MM_TEXT);
    uint16_t cx = x;
    for (int i = 0; i < 5; i++) {
        uint16_t sw = (uint16_t)(w * segs[i] / total);
        app_graphics_fill_rect(cx, y, sw, 30, colors[i]);
        cx += sw;
    }
    cx = x;
    for (int i = 0; i < 5; i++) {
        uint16_t sw = (uint16_t)(w * segs[i] / total);
        char line[64]; char t[16];
        line[0]='\0';
        append_str(line, names[i], sizeof(line));
        append_str(line, " ", sizeof(line));
        fmt_u32(t, (uint32_t)(segs[i] / (1024u*1024u)));
        append_str(line, t, sizeof(line));
        append_str(line, "MB", sizeof(line));
        draw_text(cx, (uint16_t)(y + 44), line, MM_TEXT);
        cx += sw;
    }
}

static void draw_history(void)
{
    uint16_t x = (uint16_t)(MM_WIN_X + 24);
    uint16_t y = (uint16_t)(MM_BODY_Y + 30);
    uint16_t w = 840, h = 240;
    uint64_t maxv = 1;
    uint32_t i;
    for (i = 0; i < MM_HIST; i++) if (g_hist[i] > maxv) maxv = g_hist[i];
    draw_text(x, (uint16_t)(y - 20), "内存使用趋势（最近 60 秒）", MM_TEXT);
    app_graphics_fill_rect(x, y, w, h, 0x00F3F6F7);
    for (i = 0; i < MM_HIST; i++) {
        uint32_t idx = (g_hist_pos + i) % MM_HIST;
        uint16_t bx = (uint16_t)(x + i * w / MM_HIST);
        uint16_t bh = (uint16_t)(h * g_hist[idx] / maxv);
        if (bh == 0) bh = 1;
        app_graphics_fill_rect(bx, (uint16_t)(y + h - bh), 8, bh, MM_BLUE);
    }
}

static void draw_procs(void)
{
    uint16_t y = (uint16_t)(MM_BODY_Y + 16);
    uint32_t i;
    draw_text((uint16_t)(MM_WIN_X + 16), y, "进程内存占用 Top 10", MM_TEXT);
    y += 26;
    for (i = 0; i < g_snap.proc_count && i < 10; i++) {
        mm_proc_t *p = &g_snap.procs[i];
        char line[96]; char t[12];
        line[0]='\0';
        append_str(line, p->name, sizeof(line));
        append_str(line, "  pid=", sizeof(line));
        fmt_u32(t, (uint32_t)p->pid); append_str(line, t, sizeof(line));
        draw_text((uint16_t)(MM_WIN_X + 16), y, line, MM_TEXT);
        {
            char kb[16];
            kb[0]='\0';
            fmt_u32(kb, p->mem_kb); append_str(kb, "KB", sizeof(kb));
            draw_text((uint16_t)(MM_WIN_X + 400), y, kb, MM_MUTED);
        }
        y += 22;
    }
}

static void draw_allocators(void)
{
    uint16_t y = (uint16_t)(MM_BODY_Y + 16);
    char line[128]; char t[12];

    line[0]='\0';
    append_str(line, "kmalloc alloc=", sizeof(line));
    fmt_u32(t, g_snap.kmalloc_alloc_count); append_str(line, t, sizeof(line));
    append_str(line, " free=", sizeof(line));
    fmt_u32(t, g_snap.kmalloc_free_count); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(MM_WIN_X + 16), y, line, MM_TEXT); y += 26;

    line[0]='\0';
    append_str(line, "frame total=", sizeof(line));
    fmt_u32(t, g_snap.frame_total); append_str(line, t, sizeof(line));
    append_str(line, " used=", sizeof(line));
    fmt_u32(t, g_snap.frame_used); append_str(line, t, sizeof(line));
    append_str(line, " reserved=", sizeof(line));
    fmt_u32(t, g_snap.frame_reserved); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(MM_WIN_X + 16), y, line, MM_TEXT); y += 26;

    line[0]='\0';
    append_str(line, "buddy alloc=", sizeof(line));
    fmt_u32(t, g_snap.buddy_alloc_count); append_str(line, t, sizeof(line));
    append_str(line, " free=", sizeof(line));
    fmt_u32(t, g_snap.buddy_free_count); append_str(line, t, sizeof(line));
    append_str(line, " split=", sizeof(line));
    fmt_u32(t, g_snap.buddy_split_count); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(MM_WIN_X + 16), y, line, MM_TEXT); y += 26;

    line[0]='\0';
    append_str(line, "pool slots used=", sizeof(line));
    fmt_u32(t, g_snap.pool_slots_used); append_str(line, t, sizeof(line));
    append_str(line, "/", sizeof(line));
    fmt_u32(t, g_snap.pool_slots_total); append_str(line, t, sizeof(line));
    append_str(line, "  pools=", sizeof(line));
    fmt_u32(t, g_snap.pool_count); append_str(line, t, sizeof(line));
    draw_text((uint16_t)(MM_WIN_X + 16), y, line, MM_TEXT); y += 26;
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev = 0;
    app_enter_graphics_mode();
    memset(&g_snap, 0, sizeof(g_snap));
    g_last_refresh = 0;
    refresh();

    for (;;) {
        uint64_t now = app_ticks();
        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev & 1u)) {
            osui_rect_t tab = { (uint16_t)(MM_WIN_X + 8), 106,
                                (uint16_t)(MM_WIN_W - 16), 34 };
            uint32_t tw = tab.width / 5u;
            uint32_t i;
            for (i = 0; i < 5; i++) {
                osui_rect_t tr = { (uint16_t)(tab.x + i * tw), tab.y, (uint16_t)tw, tab.height };
                if (in_rect(mouse.x_pixels, mouse.y_pixels, tr)) { g_tab = i; break; }
            }
        }
        if (mouse.buttons & 2u) return 0;
        prev = mouse.buttons;
        refresh();

        osui_canvas(MM_CANVAS);
        osui_panel((osui_rect_t){MM_WIN_X, MM_WIN_Y, MM_WIN_W, MM_WIN_H});
        osui_titlebar((osui_rect_t){MM_WIN_X, MM_WIN_Y, MM_WIN_W, 36},
                      "内存监视器", true);
        osui_tabbar((osui_rect_t){(uint16_t)(MM_WIN_X + 8), 106,
                                  (uint16_t)(MM_WIN_W - 16), 34},
                    g_tabs, 5, g_tab);
        switch (g_tab) {
            case 0: draw_overview(); break;
            case 1: draw_distribution(); break;
            case 2: draw_history(); break;
            case 3: draw_procs(); break;
            default: draw_allocators(); break;
        }
        osui_statusbar((osui_rect_t){MM_WIN_X, (uint16_t)(MM_WIN_Y + MM_WIN_H - 28),
                                     MM_WIN_W, 28},
                       "memmon", "右键退出", OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
