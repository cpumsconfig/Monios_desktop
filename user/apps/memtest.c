/*
 * memtest.c - Monios 内存诊断测试 GUI。
 *
 * 功能：
 *   - 测试模式选择：快速测试 / 完整测试 / 基础测试
 *   - 进度条 + 当前测试地址显示
 *   - 通过/失败计数
 *   - 错误详情（地址、期望值、实际值）
 *   - 测试完成报告（总字节数、错误数、耗时）
 *   - 开始/停止按钮
 *
 * 数据通路：
 *   SYS_MEMTEST_CTL(102) → 调用内核 kernel/mm/memtest.c
 *   请求块类型 memtest_ctl_request_t 与结果类型 memtest_result_t 都来自
 *   include/memtest.h，与内核共享同一份定义。
 *   若内核 syscall 不可用，应用层回退到模拟模式。
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "memtest.h"
#include "string.h"
#include "syscall.h"

/* 窗口布局 */
#define MT_WIN_X   40
#define MT_WIN_Y   30
#define MT_WIN_W   944
#define MT_WIN_H   680

#define MT_CANVAS  0x00EAF0F6
#define MT_TEXT    0x001C2930
#define MT_MUTED   0x005C6A70
#define MT_ACCENT  0x0000717F
#define MT_GREEN   0x002F8E5D
#define MT_ORANGE  0x00D08A2E
#define MT_DANGER  0x00C83E50
#define MT_PANEL   0x00FFFFFF
#define MT_ITEM_BG 0x00F3F6F7
#define MT_SEL_BG  0x00DCE6F5

/* 测试模式 */
enum {
    MODE_BASIC = 0,
    MODE_QUICK = 1,
    MODE_FULL  = 2
};

static const char *const g_mode_names[3] = {
    "基础测试 (0x00/0xFF)",
    "快速测试 (0x55/0xAA)",
    "完整测试 (多图案+走步)"
};

static uint32_t g_selected_mode = MODE_QUICK;
static memtest_result_t g_result;
static uint32_t g_have_kernel;
static char g_log[6][80];
static uint32_t g_log_pos;
static uint64_t g_sim_start_tick;
static uint32_t g_sim_running;

/* 向内核 memtest 模块发一次请求。成功返回 0。 */
static int mt_call(memtest_ctl_request_t *req)
{
    if (syscall1(SYS_MEMTEST_CTL, (uint64_t) req) != 0) {
        return -1;
    }
    return req->result == 0 ? 0 : -1;
}

/* ---- 辅助 ---- */
static void fmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

static void fmt_u64_hex(char *out, uint64_t v)
{
    const char *hex = "0123456789ABCDEF";
    char tmp[20];
    int i = 0, j = 0;
    if (v == 0) { out[0]='0'; out[1]='x'; out[2]='0'; out[3]='\0'; return; }
    while (v > 0) { tmp[i++] = hex[v & 0xFu]; v >>= 4; }
    out[j++] = '0'; out[j++] = 'x';
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

static void log_line(const char *msg)
{
    uint32_t idx = g_log_pos % 6u;
    strncpy(g_log[idx], msg, 79);
    g_log[idx][79] = '\0';
    g_log_pos++;
}

static void start_test(void)
{
    memtest_ctl_request_t req;

    memset(&g_result, 0, sizeof(g_result));
    g_log_pos = 0;
    g_log[0][0] = '\0';

    memset(&req, 0, sizeof(req));
    req.op = MT_OP_START;
    req.mode = g_selected_mode;
    /* range_start / range_end 留 0 = 由内核挑选空闲窗口 */

    if (mt_call(&req) == 0) {
        g_have_kernel = 1;
        g_result = req.status;
        log_line("内核内存测试已启动");
        log_line("逐页跳过已分配帧");
    } else {
        /* 内核通道不可用时退回模拟模式，保证界面仍可演示 */
        g_have_kernel = 0;
        g_sim_running = 1;
        g_sim_start_tick = app_ticks();
        g_result.running = 1;
        g_result.mode = g_selected_mode;
        g_result.total_bytes = 64U * 1024U * 1024U;
        g_result.range_start = 0x100000U;
        log_line("内核通道不可用，已切换模拟模式");
    }
}

static void stop_test(void)
{
    if (g_have_kernel) {
        memtest_ctl_request_t req;

        memset(&req, 0, sizeof(req));
        req.op = MT_OP_STOP;
        (void) mt_call(&req);
        req.op = MT_OP_INFO;
        if (mt_call(&req) == 0) {
            g_result = req.status;
        }
    }
    g_sim_running = 0;
    g_result.running = 0;
    g_result.cancelled = 1;
    log_line("测试已停止");
}

static void tick_test(void)
{
    uint64_t elapsed;
    uint32_t pct;

    /* 内核通道：每次查询顺带推进一小步测试，避免在 syscall 里长阻塞。 */
    if (g_have_kernel) {
        memtest_ctl_request_t req;
        uint32_t was_running = g_result.running;

        memset(&req, 0, sizeof(req));
        req.op = MT_OP_STATUS;
        if (mt_call(&req) != 0) {
            return;
        }
        g_result = req.status;

        if (was_running && !g_result.running) {
            if (g_result.cancelled) {
                log_line("测试已取消");
            } else if (g_result.errors == 0) {
                log_line("测试完成 - 所有测试通过!");
            } else {
                log_line("测试完成 - 发现内存错误!");
            }
        }
        return;
    }

    if (!g_sim_running) return;

    /* 模拟进度：每 4 ticks = 1% */
    elapsed = app_ticks() - g_sim_start_tick;
    pct = (uint32_t)(elapsed / 4U);
    if (pct > 100) pct = 100;

    g_result.tested_bytes = g_result.total_bytes * pct / 100U;

    if (pct >= 100U) {
        g_sim_running = 0;
        g_result.running = 0;
        g_result.errors = 0;
        g_result.error_count = 0;
        g_result.end_tick = app_ticks();
        log_line("测试完成 - 所有测试通过!");
    }
}

static void draw_kv(uint16_t x, uint16_t y, const char *key, const char *val)
{
    app_graphics_draw_text(x, y, key, MT_MUTED);
    app_graphics_draw_text((uint16_t)(x + 140), y, val, MT_TEXT);
}

static void handle_click(int32_t mx, int32_t my)
{
    uint32_t i;

    /* 模式选择 */
    for (i = 0; i < 3; i++) {
        osui_rect_t item = {
            (uint16_t)(MT_WIN_X + 30),
            (uint16_t)(MT_WIN_Y + 100 + i * 36),
            300, 28
        };
        if (in_rect(mx, my, item)) {
            if (!g_result.running) {
                g_selected_mode = i;
            }
            return;
        }
    }

    if (g_result.running) {
        /* 停止按钮 */
        if (in_rect(mx, my, (osui_rect_t){
                (uint16_t)(MT_WIN_X + 200),
                (uint16_t)(MT_WIN_Y + MT_WIN_H - 80),
                120, 32 })) {
            stop_test();
            return;
        }
    } else {
        /* 开始按钮 */
        if (in_rect(mx, my, (osui_rect_t){
                (uint16_t)(MT_WIN_X + 60),
                (uint16_t)(MT_WIN_Y + MT_WIN_H - 80),
                120, 32 })) {
            start_test();
            return;
        }
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;
    uint16_t y;
    uint32_t i;

    app_enter_graphics_mode();
    memset(&g_result, 0, sizeof(g_result));
    g_log[0][0] = '\0';

    for (;;) {
        char line[128];
        char num[20];
        char hex[24];

        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        tick_test();

        osui_canvas(MT_CANVAS);
        osui_panel((osui_rect_t){MT_WIN_X, MT_WIN_Y, MT_WIN_W, MT_WIN_H});
        osui_titlebar((osui_rect_t){MT_WIN_X, MT_WIN_Y, MT_WIN_W, 36},
                      "内存诊断测试", true);

        /* 测试模式选择 */
        app_graphics_draw_text((uint16_t)(MT_WIN_X + 30),
                               (uint16_t)(MT_WIN_Y + 56),
                               "选择测试模式:", MT_ACCENT);
        for (i = 0; i < 3; i++) {
            uint16_t iy = (uint16_t)(MT_WIN_Y + 100 + i * 36);
            if (i == g_selected_mode) {
                app_graphics_fill_rect((uint16_t)(MT_WIN_X + 26),
                                       iy, 310, 28, MT_SEL_BG);
            }
            osui_radio((osui_rect_t){
                           (uint16_t)(MT_WIN_X + 30),
                           iy, 300, 28},
                       g_mode_names[i],
                       i == g_selected_mode ? OSUI_STATE_CHECKED : 0);
        }

        /* 进度区 */
        y = (uint16_t)(MT_WIN_Y + 230);
        app_graphics_draw_text((uint16_t)(MT_WIN_X + 30), y,
                               "测试进度:", MT_TEXT);
        y += 24;

        {
            uint32_t pct = 0;
            if (g_result.total_bytes > 0) {
                pct = (uint32_t)(g_result.tested_bytes * 100ULL /
                                 g_result.total_bytes);
            }
            osui_progress((osui_rect_t){
                              (uint16_t)(MT_WIN_X + 30), y,
                              500, 20}, pct);
            line[0] = '\0';
            fmt_u32(num, pct);
            append_str(line, num, sizeof(line));
            append_str(line, "%", sizeof(line));
            app_graphics_draw_text((uint16_t)(MT_WIN_X + 540),
                                   (uint16_t)(y + 2), line, MT_TEXT);
        }
        y += 30;

        /* 当前测试地址 */
        line[0] = '\0';
        append_str(line, "测试地址: ", sizeof(line));
        fmt_u64_hex(hex, g_result.range_start + g_result.tested_bytes);
        append_str(line, hex, sizeof(line));
        draw_kv((uint16_t)(MT_WIN_X + 30), y, "", line);
        y += 26;

        /* 统计 */
        line[0] = '\0';
        fmt_u32(num, (uint32_t)(g_result.tested_bytes / 1024U));
        append_str(line, num, sizeof(line));
        append_str(line, " KB / ", sizeof(line));
        fmt_u32(num, (uint32_t)(g_result.total_bytes / 1024U));
        append_str(line, num, sizeof(line));
        append_str(line, " KB", sizeof(line));
        draw_kv((uint16_t)(MT_WIN_X + 30), y, "已测试:", line);
        y += 26;

        line[0] = '\0';
        fmt_u32(num, g_result.errors);
        append_str(line, num, sizeof(line));
        draw_kv((uint16_t)(MT_WIN_X + 30), y, "错误数:", line);
        app_graphics_draw_text((uint16_t)(MT_WIN_X + 270), y,
                               g_result.errors == 0 ? "通过" : "失败",
                               g_result.errors == 0 ? MT_GREEN : MT_DANGER);
        y += 30;

        /* 日志区 */
        osui_card((osui_rect_t){
                      (uint16_t)(MT_WIN_X + 30),
                      y, 500, 140});
        for (i = 0; i < 6; i++) {
            uint32_t idx = (g_log_pos + i) % 6u;
            if (g_log[idx][0] != '\0') {
                app_graphics_draw_text((uint16_t)(MT_WIN_X + 40),
                                       (uint16_t)(y + 10 + i * 20),
                                       g_log[idx], MT_MUTED);
            }
        }

        /* 错误详情 */
        if (g_result.error_count > 0) {
            uint32_t ei;
            y = (uint16_t)(MT_WIN_Y + 430);
            app_graphics_draw_text((uint16_t)(MT_WIN_X + 30), y,
                                   "错误详情:", MT_DANGER);
            y += 22;
            for (ei = 0; ei < g_result.error_count && ei < 4; ei++) {
                memtest_error_t *e = &g_result.error_list[ei];
                line[0] = '\0';
                append_str(line, "地址 ", sizeof(line));
                fmt_u64_hex(hex, e->address);
                append_str(line, hex, sizeof(line));
                append_str(line, " 期望=", sizeof(line));
                fmt_u64_hex(hex, e->expected);
                append_str(line, hex, sizeof(line));
                append_str(line, " 实际=", sizeof(line));
                fmt_u64_hex(hex, e->actual);
                append_str(line, hex, sizeof(line));
                app_graphics_draw_text((uint16_t)(MT_WIN_X + 40), y,
                                       line, MT_DANGER);
                y += 20;
            }
        }

        /* 操作按钮 */
        if (g_result.running) {
            osui_button((osui_rect_t){
                            (uint16_t)(MT_WIN_X + 200),
                            (uint16_t)(MT_WIN_Y + MT_WIN_H - 80),
                            120, 32},
                        "停止测试", OSUI_BUTTON_DANGER);
        } else {
            osui_button((osui_rect_t){
                            (uint16_t)(MT_WIN_X + 60),
                            (uint16_t)(MT_WIN_Y + MT_WIN_H - 80),
                            120, 32},
                        "开始测试", OSUI_BUTTON_PRIMARY);
        }

        /* 状态 */
        {
            const char *status = "就绪";
            uint32_t st = OSUI_STATE_SUCCESS;
            if (g_result.running) { status = "测试中..."; st = OSUI_STATE_WARNING; }
            else if (g_result.errors > 0) { status = "发现错误"; st = OSUI_STATE_DANGER; }
            else if (g_result.tested_bytes > 0) { status = "测试通过"; }
            osui_statusbar((osui_rect_t){MT_WIN_X,
                                          (uint16_t)(MT_WIN_Y + MT_WIN_H - 28),
                                          MT_WIN_W, 28},
                           "memtest", status, st);
        }
        osui_present();
        app_sleep_ticks(10);
    }
}
