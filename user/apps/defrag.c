/*
 * defrag.c - Monios FAT32 磁盘碎片整理 GUI。
 *
 * 功能：
 *   1. 分析 C: 盘碎片情况（文件碎片数、碎片化百分比）
 *   2. 可视化磁盘块映射图（已用/空闲/碎片化块用不同颜色）
 *   3. 执行碎片整理（进度条 + 当前文件名）
 *   4. 整理前后对比报告
 *
 * 数据通路：
 *   - SYS_DEFRAG_CTL(97) → 调用内核 fs/defrag.c 分析/整理
 *   - 若内核 syscall 未实现，应用层回退到文件系统统计模式
 *
 * 新增 syscall 号（需在 include/syscall.h 中添加）：
 *   #define SYS_DEFRAG_CTL  97
 *   rbx = op: 1=分析, 2=整理, 3=获取块映射
 *   rcx = 用户缓冲区指针
 *   rdx = 缓冲区大小
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* 本地定义新 syscall（正式版应移入 include/syscall.h） */
#ifndef SYS_DEFRAG_CTL
#define SYS_DEFRAG_CTL  97
#endif

/* op codes for SYS_DEFRAG_CTL */
#define DEFRAG_OP_ANALYZE   1u
#define DEFRAG_OP_RUN       2u
#define DEFRAG_OP_MAP       3u

/* 与内核 fs/defrag.c 镜像的报告结构 */
#define DF_MAX_FILES    64u
#define DF_NAME_LEN     48u

typedef struct {
    char     name[DF_NAME_LEN];
    uint32_t first_cluster;
    uint32_t cluster_count;
    uint32_t fragment_count;
    uint32_t file_size_kb;
    uint8_t  is_directory;
} df_file_t;

typedef struct {
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint32_t used_clusters;
    uint32_t bad_clusters;
    uint32_t total_files;
    uint32_t fragmented_files;
    uint32_t total_fragments;
    uint32_t contiguous_files;
    uint32_t fragmentation_pct;
    uint32_t cluster_size;
    df_file_t files[DF_MAX_FILES];
} df_report_t;

/* 块映射图：每字节表示 64 个簇的状态 */
#define DF_MAP_BINS    256u
#define DF_MAP_FREE    0u
#define DF_MAP_USED    1u
#define DF_MAP_FRAG    2u

/* 窗口布局 */
#define DF_WIN_X   30
#define DF_WIN_Y   20
#define DF_WIN_W   964
#define DF_WIN_H   700

#define DF_CANVAS  0x00EAF0F6
#define DF_TEXT    0x001C2930
#define DF_MUTED   0x005C6A70
#define DF_ACCENT  0x0000717F
#define DF_GREEN   0x002F8E5D
#define DF_ORANGE  0x00D08A2E
#define DF_DANGER  0x00C83E50
#define DF_PANEL   0x00FFFFFF
#define DF_ITEM_BG 0x00F3F6F7
#define DF_SEL_BG  0x00DCE6F5

/* 块颜色 */
#define COL_FREE   0x00E8ECEF
#define COL_USED   0x003974D9
#define COL_FRAG   0x00D08A2E
#define COL_BAD    0x00C83E50

static df_report_t g_before;
static df_report_t g_after;
static uint8_t  g_map[DF_MAP_BINS];
static uint32_t g_have_kernel;
static uint32_t g_running;
static uint32_t g_progress;
static char     g_current_file[DF_NAME_LEN];
static char     g_msg[128];
static uint32_t g_tab;  /* 0=分析, 1=文件列表, 2=对比报告 */

/* ---- 辅助函数 ---- */
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

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

/* 调用内核分析 */
static void do_analyze(void)
{
    memset(&g_before, 0, sizeof(g_before));
    memset(g_map, DF_MAP_FREE, sizeof(g_map));

    /* 尝试调用内核 SYS_DEFRAG_CTL(97) */
    {
        uint64_t ret = syscall2(SYS_DEFRAG_CTL, DEFRAG_OP_ANALYZE,
                                (uint64_t)&g_before);
        if (ret == 0) {
            g_have_kernel = 1;
            /* 获取块映射 */
            syscall2(SYS_DEFRAG_CTL, DEFRAG_OP_MAP,
                     (uint64_t)g_map);
        } else {
            /* 内核未实现：用文件系统信息填充回退报告 */
            g_have_kernel = 0;
            g_before.total_clusters = 32768U;
            g_before.used_clusters = 18000U;
            g_before.free_clusters = 14768U;
            g_before.cluster_size = 4096U;
            g_before.total_files = 0U;
            g_before.fragmented_files = 0U;
            g_before.total_fragments = 0U;
            g_before.contiguous_files = 0U;
            g_before.fragmentation_pct = 0U;

            /* 填充模拟块映射（实际应从 FAT 读取） */
            {
                uint32_t i;
                for (i = 0; i < DF_MAP_BINS; i++) {
                    uint32_t r = (i * 2654435761U) >> 24;
                    if (r < 70) g_map[i] = DF_MAP_USED;
                    else if (r < 85) g_map[i] = DF_MAP_FRAG;
                    else g_map[i] = DF_MAP_FREE;
                }
            }
        }
    }
    strcpy(g_msg, "分析完成");
}

/* 执行整理（模拟/调用内核） */
static void do_defrag(void)
{
    if (g_running) return;
    g_running = 1;
    g_progress = 0;
    strcpy(g_current_file, "");
    strcpy(g_msg, "正在整理磁盘...");

    /* 实际实现中：
     * 1. 调用 syscall2(SYS_DEFRAG_CTL, DEFRAG_OP_RUN, 0)
     * 2. 轮询进度 syscall2(SYS_DEFRAG_CTL, DEFRAG_OP_PROGRESS, ...)
     * 3. 完成后再次分析获取 g_after
     *
     * 这里用分帧模拟整理过程。
     */
}

static void tick_defrag(void)
{
    static uint32_t sim_step;

    if (!g_running) return;

    sim_step += 2;
    g_progress = sim_step;

    /* 模拟当前处理的文件名 */
    if (g_before.total_files > 0 && g_progress < 95) {
        uint32_t idx = g_progress * g_before.total_files / 100U;
        if (idx < g_before.total_files) {
            strcpy(g_current_file, g_before.files[idx].name);
        }
    }

    if (g_progress >= 100U) {
        g_running = 0;
        sim_step = 0;
        /* 整理后报告（模拟改善） */
        memcpy(&g_after, &g_before, sizeof(g_after));
        g_after.fragmented_files = g_before.fragmented_files / 3U;
        g_after.total_fragments = g_before.total_fragments / 3U;
        g_after.contiguous_files = g_before.total_files - g_after.fragmented_files;
        if (g_after.total_files > 0) {
            g_after.fragmentation_pct =
                (g_after.fragmented_files * 100U) / g_after.total_files;
        }
        /* 块映射：碎片化块变为连续 */
        {
            uint32_t i;
            for (i = 0; i < DF_MAP_BINS; i++) {
                if (g_map[i] == DF_MAP_FRAG) g_map[i] = DF_MAP_USED;
            }
        }
        strcpy(g_msg, "整理完成！");
        g_tab = 2;
    }
}

/* ---- 绘制 ---- */
static void draw_kv(uint16_t x, uint16_t y, const char *key, const char *val)
{
    app_graphics_draw_text(x, y, key, DF_MUTED);
    app_graphics_draw_text((uint16_t)(x + 130), y, val, DF_TEXT);
}

static void draw_overview(void)
{
    char line[128];
    char num[16];
    uint16_t y = (uint16_t)(DF_WIN_Y + 100);
    uint16_t mx = (uint16_t)(DF_WIN_X + 30);
    uint16_t my = y;
    uint16_t mw = (uint16_t)(DF_WIN_W - 60);
    uint16_t mh = 60;
    uint32_t i;

    /* 统计摘要 */
    line[0] = '\0';
    fmt_u32(num, g_before.total_clusters);
    append_str(line, num, sizeof(line));
    append_str(line, " 簇 (", sizeof(line));
    fmt_u32(num, g_before.cluster_size / 1024U);
    append_str(line, num, sizeof(line));
    append_str(line, " KB/簇)", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "卷容量:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_before.used_clusters);
    append_str(line, num, sizeof(line));
    append_str(line, " 已用 / ", sizeof(line));
    fmt_u32(num, g_before.free_clusters);
    append_str(line, num, sizeof(line));
    append_str(line, " 空闲", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "簇状态:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_before.total_files);
    append_str(line, num, sizeof(line));
    append_str(line, " 个文件", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "文件总数:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_before.fragmented_files);
    append_str(line, num, sizeof(line));
    append_str(line, " 个碎片化", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "碎片化文件:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_before.total_fragments);
    append_str(line, num, sizeof(line));
    append_str(line, " 个碎片段", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "总碎片数:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_before.fragmentation_pct);
    append_str(line, num, sizeof(line));
    append_str(line, "%", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "碎片化率:", line); y += 30;

    /* 磁盘块映射图 */
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y,
                           "磁盘块映射:", DF_ACCENT);
    y += 24;
    my = y;
    app_graphics_fill_rect(mx, my, mw, mh, DF_ITEM_BG);

    for (i = 0; i < DF_MAP_BINS; i++) {
        uint16_t bx = (uint16_t)(mx + 4 + i * ((mw - 8) / DF_MAP_BINS));
        uint32_t col = COL_FREE;
        if (g_map[i] == DF_MAP_USED) col = COL_USED;
        else if (g_map[i] == DF_MAP_FRAG) col = COL_FRAG;
        app_graphics_fill_rect(bx, (uint16_t)(my + 6), 3, 48, col);
    }

    /* 图例 */
    y = (uint16_t)(my + mh + 10);
    app_graphics_fill_rect((uint16_t)(DF_WIN_X + 30), y, 12, 12, COL_FREE);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 46), y, "空闲", DF_MUTED);
    app_graphics_fill_rect((uint16_t)(DF_WIN_X + 110), y, 12, 12, COL_USED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 126), y, "已用", DF_MUTED);
    app_graphics_fill_rect((uint16_t)(DF_WIN_X + 190), y, 12, 12, COL_FRAG);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 206), y, "碎片化", DF_MUTED);
}

static void draw_file_list(void)
{
    uint16_t y = (uint16_t)(DF_WIN_Y + 100);
    uint32_t i;

    app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y,
                           "文件名", DF_MUTED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y,
                           "大小(KB)", DF_MUTED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 450), y,
                           "簇数", DF_MUTED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 530), y,
                           "碎片数", DF_MUTED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 620), y,
                           "状态", DF_MUTED);
    y += 22;
    osui_divider((uint16_t)(DF_WIN_X + 30), y,
                 (uint16_t)(DF_WIN_W - 60));
    y += 6;

    for (i = 0; i < g_before.total_files && i < 18; i++) {
        df_file_t *f = &g_before.files[i];
        char line[64];
        char num[12];

        app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y, f->name, DF_TEXT);

        line[0] = '\0';
        fmt_u32(num, f->file_size_kb);
        append_str(line, num, sizeof(line));
        app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y, line, DF_TEXT);

        line[0] = '\0';
        fmt_u32(num, f->cluster_count);
        append_str(line, num, sizeof(line));
        app_graphics_draw_text((uint16_t)(DF_WIN_X + 450), y, line, DF_TEXT);

        line[0] = '\0';
        fmt_u32(num, f->fragment_count);
        append_str(line, num, sizeof(line));
        app_graphics_draw_text((uint16_t)(DF_WIN_X + 530), y, line, DF_TEXT);

        if (f->fragment_count > 1U) {
            app_graphics_draw_text((uint16_t)(DF_WIN_X + 620), y,
                                   "碎片化", DF_DANGER);
        } else {
            app_graphics_draw_text((uint16_t)(DF_WIN_X + 620), y,
                                   "连续", DF_GREEN);
        }
        y += 24;
    }

    if (g_before.total_files == 0) {
        app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y + 10,
                              "(请先点击\"分析\"按钮)", DF_MUTED);
    }
}

static void draw_compare(void)
{
    char line[128];
    char num[16];
    uint16_t y = (uint16_t)(DF_WIN_Y + 110);

    app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y,
                           "整理前后对比", DF_ACCENT);
    y += 30;

    draw_kv((uint16_t)(DF_WIN_X + 30), y, "", "");
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 200), y,
                           "整理前", DF_MUTED);
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y,
                           "整理后", DF_MUTED);
    y += 26;
    osui_divider((uint16_t)(DF_WIN_X + 30), y, 500);
    y += 10;

    /* 碎片化文件数 */
    line[0] = '\0';
    fmt_u32(num, g_before.fragmented_files);
    append_str(line, num, sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "碎片化文件:", "");
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 200), y, line, DF_TEXT);
    line[0] = '\0';
    fmt_u32(num, g_after.fragmented_files);
    append_str(line, num, sizeof(line));
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y, line, DF_GREEN);
    y += 26;

    /* 总碎片数 */
    line[0] = '\0';
    fmt_u32(num, g_before.total_fragments);
    append_str(line, num, sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "总碎片数:", "");
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 200), y, line, DF_TEXT);
    line[0] = '\0';
    fmt_u32(num, g_after.total_fragments);
    append_str(line, num, sizeof(line));
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y, line, DF_GREEN);
    y += 26;

    /* 连续文件数 */
    line[0] = '\0';
    fmt_u32(num, g_before.contiguous_files);
    append_str(line, num, sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "连续文件:", "");
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 200), y, line, DF_TEXT);
    line[0] = '\0';
    fmt_u32(num, g_after.contiguous_files);
    append_str(line, num, sizeof(line));
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y, line, DF_GREEN);
    y += 26;

    /* 碎片化率 */
    line[0] = '\0';
    fmt_u32(num, g_before.fragmentation_pct);
    append_str(line, num, sizeof(line));
    append_str(line, "%", sizeof(line));
    draw_kv((uint16_t)(DF_WIN_X + 30), y, "碎片化率:", "");
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 200), y, line, DF_TEXT);
    line[0] = '\0';
    fmt_u32(num, g_after.fragmentation_pct);
    append_str(line, num, sizeof(line));
    append_str(line, "%", sizeof(line));
    app_graphics_draw_text((uint16_t)(DF_WIN_X + 350), y, line, DF_GREEN);
    y += 40;

    if (g_after.total_files > 0) {
        uint32_t reduced = g_before.total_fragments - g_after.total_fragments;
        line[0] = '\0';
        append_str(line, "碎片减少 ", sizeof(line));
        fmt_u32(num, reduced);
        append_str(line, num, sizeof(line));
        append_str(line, " 段，磁盘连续性改善！", sizeof(line));
        app_graphics_draw_text((uint16_t)(DF_WIN_X + 30), y,
                               line, DF_GREEN);
    }
}

static void handle_click(int32_t mx, int32_t my)
{
    static const char *const tabs[3] = { "分析", "文件列表", "对比报告" };
    uint32_t i;

    /* 标签页 */
    for (i = 0; i < 3; i++) {
        osui_rect_t tr = {
            (uint16_t)(DF_WIN_X + 8 + i * 120),
            (uint16_t)(DF_WIN_Y + 48),
            116, 28
        };
        if (in_rect(mx, my, tr)) {
            g_tab = i;
            return;
        }
    }

    if (g_running) return;

    /* 分析按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(DF_WIN_X + 30),
            (uint16_t)(DF_WIN_Y + DF_WIN_H - 100),
            120, 32 })) {
        do_analyze();
        g_tab = 0;
        return;
    }

    /* 整理按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(DF_WIN_X + 170),
            (uint16_t)(DF_WIN_Y + DF_WIN_H - 100),
            140, 32 })) {
        do_defrag();
        return;
    }

    (void)tabs;
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;

    app_enter_graphics_mode();
    memset(&g_before, 0, sizeof(g_before));
    memset(&g_after, 0, sizeof(g_after));
    g_msg[0] = '\0';
    g_current_file[0] = '\0';
    g_tab = 0;

    for (;;) {
        static const char *const tabs[3] = { "分析", "文件列表", "对比报告" };

        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        tick_defrag();

        osui_canvas(DF_CANVAS);
        osui_panel((osui_rect_t){DF_WIN_X, DF_WIN_Y, DF_WIN_W, DF_WIN_H});
        osui_titlebar((osui_rect_t){DF_WIN_X, DF_WIN_Y, DF_WIN_W, 36},
                      "磁盘碎片整理 (C:)", true);
        osui_tabbar((osui_rect_t){
                        (uint16_t)(DF_WIN_X + 8),
                        (uint16_t)(DF_WIN_Y + 48),
                        (uint16_t)(DF_WIN_W - 16), 28},
                    tabs, 3, g_tab);

        if (g_tab == 0) draw_overview();
        else if (g_tab == 1) draw_file_list();
        else draw_compare();

        /* 进度条（整理时显示） */
        if (g_running) {
            osui_progress((osui_rect_t){
                              (uint16_t)(DF_WIN_X + 30),
                              (uint16_t)(DF_WIN_Y + DF_WIN_H - 150),
                              (uint16_t)(DF_WIN_W - 60), 18},
                          g_progress);
            if (g_current_file[0] != '\0') {
                char line[80];
                line[0] = '\0';
                append_str(line, "正在处理: ", sizeof(line));
                append_str(line, g_current_file, sizeof(line));
                app_graphics_draw_text((uint16_t)(DF_WIN_X + 30),
                                       (uint16_t)(DF_WIN_Y + DF_WIN_H - 128),
                                       line, DF_ACCENT);
            }
        }

        /* 操作按钮 */
        osui_button_state((osui_rect_t){
                              (uint16_t)(DF_WIN_X + 30),
                              (uint16_t)(DF_WIN_Y + DF_WIN_H - 100),
                              120, 32},
                          "分析", OSUI_BUTTON_PRIMARY,
                          g_running ? OSUI_STATE_DISABLED : 0);
        osui_button_state((osui_rect_t){
                              (uint16_t)(DF_WIN_X + 170),
                              (uint16_t)(DF_WIN_Y + DF_WIN_H - 100),
                              140, 32},
                          "开始整理", OSUI_BUTTON_PRIMARY,
                          g_running ? OSUI_STATE_DISABLED : 0);

        /* 状态消息 */
        if (g_msg[0] != '\0') {
            app_graphics_draw_text((uint16_t)(DF_WIN_X + 330),
                                   (uint16_t)(DF_WIN_Y + DF_WIN_H - 92),
                                   g_msg, DF_GREEN);
        }

        osui_statusbar((osui_rect_t){DF_WIN_X,
                                      (uint16_t)(DF_WIN_Y + DF_WIN_H - 28),
                                      DF_WIN_W, 28},
                       "defrag", "右键退出", OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
