/*
 * gdbgui.c - Monios 图形化 GDB 调试器前端。
 *
 * 通过内核 SYS_DEBUG_CTL (67) 与 GDB stub 交互，无需串口：
 *   - DBG_READ_REGS (7)  读取 24 个寄存器
 *   - DBG_READ_MEM  (8)  按地址读取内存（十六进制查看器）
 *   - DBG_SWBP_SET/CLEAR/CLEAR_ALL (0/1/2) 软件断点
 *   - DBG_STEP_ON (5) / DBG_CONTINUE (6)   单步 / 继续
 *   - DBG_STATUS (10)     活动断点与附加状态
 *
 * 右键退出。
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* DBG_* opcodes (mirrors include/gdb_stub.h) */
#define GDBG_OP_SWBP_SET        0
#define GDBG_OP_SWBP_CLEAR      1
#define GDBG_OP_SWBP_CLEAR_ALL  2
#define GDBG_OP_STEP_ON         5
#define GDBG_OP_CONTINUE        6
#define GDBG_OP_READ_REGS       7
#define GDBG_OP_READ_MEM        8
#define GDBG_OP_WRITE_MEM       9
#define GDBG_OP_STATUS          10

#define GDBG_WIN_X   40
#define GDBG_WIN_Y   24
#define GDBG_WIN_W   960
#define GDBG_WIN_H   680

#define GDBG_CANVAS  0x00EAF0F6
#define GDBG_TEXT    0x001C2930
#define GDBG_MUTED   0x005C6A70
#define GDBG_ACCENT  0x0000717F
#define GDBG_PANEL2  0x00F3F6F7

#define GDBG_MEM_ROWS 14u
#define GDBG_MEM_COLS 16u

static const char *const g_reg_names[24] = {
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
    "rip", "rflg","cs",  "ss",  "ds",  "es",  "fs",  "gs"
};

static uint64_t g_regs[24];
static uint64_t g_mem_base = 0x0000000000400000ULL;
static uint8_t  g_mem[GDBG_MEM_ROWS * GDBG_MEM_COLS];
static int      g_mem_valid;
static uint64_t g_dbg_status;
static char     g_msg[96];

static void fmt_hex64(char *out, uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int i;
    out[0] = '0';
    out[1] = 'x';
    for (i = 0; i < 16; i++) {
        out[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xFu];
    }
    out[18] = '\0';
}

static void draw_text(uint16_t x, uint16_t y, const char *t, uint32_t c)
{
    app_graphics_draw_text(x, y, t, c);
}

static void refresh_regs(void)
{
    if (syscall3(SYS_DEBUG_CTL, GDBG_OP_READ_REGS,
                 (uint64_t)g_regs, 0) != 24) {
        memset(g_regs, 0, sizeof(g_regs));
    }
}

static void refresh_mem(void)
{
    uint64_t args[2];

    args[0] = (uint64_t)g_mem;
    args[1] = sizeof(g_mem);
    g_mem_valid = (int) syscall3(SYS_DEBUG_CTL, GDBG_OP_READ_MEM,
                                 g_mem_base, (uint64_t)args);
    if (g_mem_valid <= 0) {
        g_mem_valid = 0;
        memset(g_mem, 0, sizeof(g_mem));
    }
}

static void refresh_status(void)
{
    g_dbg_status = (uint64_t) syscall1(SYS_DEBUG_CTL, GDBG_OP_STATUS);
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

static void do_action(int action)
{
    uint64_t r;
    char *m = g_msg;

    m[0] = '\0';
    switch (action) {
    case 0: /* refresh */
        refresh_regs();
        refresh_mem();
        refresh_status();
        sprintf(g_msg, "registers and memory refreshed");
        break;
    case 1: /* step */
        r = (uint64_t) syscall1(SYS_DEBUG_CTL, GDBG_OP_STEP_ON);
        sprintf(g_msg, "single-step armed (%s)", r == 0 ? "ok" : "err");
        refresh_status();
        break;
    case 2: /* continue */
        syscall1(SYS_DEBUG_CTL, GDBG_OP_CONTINUE);
        sprintf(g_msg, "continued (run to next trap)");
        refresh_status();
        break;
    case 3: /* clear all sw breakpoints */
        r = (uint64_t) syscall1(SYS_DEBUG_CTL, GDBG_OP_SWBP_CLEAR_ALL);
        sprintf(g_msg, "cleared %u software breakpoint(s)", (unsigned)r);
        refresh_status();
        break;
    case 4: /* set sw breakpoint at current rip */
        r = (uint64_t) syscall3(SYS_DEBUG_CTL, GDBG_OP_SWBP_SET,
                                g_regs[16], 0);
        if ((int64_t)r >= 0) {
            sprintf(g_msg, "sw breakpoint #%u set at rip", (unsigned)r);
        } else {
            sprintf(g_msg, "sw breakpoint rejected (slot full?)");
        }
        refresh_status();
        break;
    case 5: /* mem page down */
        g_mem_base -= GDBG_MEM_COLS;
        refresh_mem();
        sprintf(g_msg, "memory base adjusted");
        break;
    case 6: /* mem page up */
        g_mem_base += GDBG_MEM_COLS;
        refresh_mem();
        sprintf(g_msg, "memory base adjusted");
        break;
    default:
        break;
    }
}

static osui_rect_t g_btn_rects[7];

static void draw_registers(void)
{
    uint16_t x = (uint16_t)(GDBG_WIN_X + 16);
    uint16_t y = 92;
    uint32_t i;

    draw_text(x, y - 18, "寄存器 (GPR + RIP + 段)", GDBG_TEXT);
    for (i = 0; i < 24; i++) {
        char line[48];
        char hex[20];

        fmt_hex64(hex, g_regs[i]);
        sprintf(line, "%-5s %s", g_reg_names[i], hex);
        draw_text(x, y, line,
                  (i == 16) ? GDBG_ACCENT : GDBG_TEXT);
        y += 22;
    }
}

static void draw_memory(void)
{
    uint16_t x = (uint16_t)(GDBG_WIN_X + 360);
    uint16_t y = 92;
    char line[96];
    uint32_t row;
    char hex[20];

    draw_text(x, y - 18, "内存查看器 (hex dump)", GDBG_TEXT);
    fmt_hex64(hex, g_mem_base);
    sprintf(line, "base = %s   (%s)", hex,
            g_mem_valid > 0 ? "ok" : "unreachable");
    draw_text(x, y, line, GDBG_MUTED);
    y += 22;
    for (row = 0; row < GDBG_MEM_ROWS; row++) {
        uint32_t col;
        uint32_t off = row * GDBG_MEM_COLS;
        int n = 0;
        uint64_t addr = g_mem_base + off;
        char ahex[20];

        fmt_hex64(ahex, addr);
        n += sprintf(line + n, "%s  ", ahex + 2);
        for (col = 0; col < GDBG_MEM_COLS; col++) {
            static const char h[] = "0123456789ABCDEF";
            uint8_t b = g_mem[off + col];

            line[n++] = h[(b >> 4) & 0xFu];
            line[n++] = h[b & 0xFu];
            line[n++] = ' ';
            if (col == 7) {
                line[n++] = ' ';
            }
        }
        line[n] = '\0';
        draw_text(x, y, line, GDBG_TEXT);
        y += 18;
    }
}

static void draw_breakpoints(void)
{
    uint16_t y = (uint16_t)(GDBG_WIN_Y + GDBG_WIN_H - 150);
    uint16_t x = (uint16_t)(GDBG_WIN_X + 16);
    char line[96];
    uint32_t sw = (uint32_t)((g_dbg_status >> 0) & 0xFFu);
    uint32_t hw = (uint32_t)((g_dbg_status >> 8) & 0xFFu);
    uint32_t step = (uint32_t)((g_dbg_status >> 16) & 1u);
    uint32_t attached = (uint32_t)((g_dbg_status >> 17) & 1u);

    draw_text(x, y, "断点 / 状态", GDBG_TEXT);
    y += 22;
    sprintf(line, "软件断点 %u   硬件断点 %u   单步 %s   GDB附加 %s",
            sw, hw, step ? "是" : "否", attached ? "是" : "否");
    draw_text(x, y, line, GDBG_MUTED);
    y += 22;
    if (g_msg[0] != '\0') {
        draw_text(x, y, g_msg, GDBG_ACCENT);
    }
}

static void draw_buttons(void)
{
    uint16_t y = (uint16_t)(GDBG_WIN_Y + GDBG_WIN_H - 60);
    uint16_t x = (uint16_t)(GDBG_WIN_X + 16);
    const char *labels[7] = {
        "刷新", "单步", "继续", "清空断点",
        "在RIP设断", "内存-", "内存+"
    };
    uint32_t i;

    for (i = 0; i < 7; i++) {
        g_btn_rects[i].x = (uint16_t)(x + i * 128);
        g_btn_rects[i].y = y;
        g_btn_rects[i].width = 118;
        g_btn_rects[i].height = 34;
        osui_button(g_btn_rects[i], labels[i],
                    (i == 1 || i == 2) ? OSUI_BUTTON_PRIMARY
                                        : OSUI_BUTTON_GHOST);
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;

    app_enter_graphics_mode();
    memset(g_regs, 0, sizeof(g_regs));
    memset(g_mem, 0, sizeof(g_mem));
    g_msg[0] = '\0';
    refresh_regs();
    refresh_mem();
    refresh_status();

    for (;;) {
        uint64_t now = app_ticks();
        uint32_t i;

        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            for (i = 0; i < 7; i++) {
                if (in_rect(mouse.x_pixels, mouse.y_pixels,
                            g_btn_rects[i])) {
                    do_action((int)i);
                    break;
                }
            }
        }
        if (mouse.buttons & 2u) {
            return 0; /* right-click exits */
        }
        prev_btn = mouse.buttons;
        (void)now;

        osui_canvas(GDBG_CANVAS);
        osui_panel((osui_rect_t){GDBG_WIN_X, GDBG_WIN_Y,
                                 GDBG_WIN_W, GDBG_WIN_H});
        osui_titlebar((osui_rect_t){GDBG_WIN_X, GDBG_WIN_Y,
                                    GDBG_WIN_W, 36},
                      "GDB GUI 调试器", true);
        osui_card((osui_rect_t){(uint16_t)(GDBG_WIN_X + 8), 48,
                                330, 560});
        osui_card((osui_rect_t){(uint16_t)(GDBG_WIN_X + 348), 48,
                                596, 430});
        draw_registers();
        draw_memory();
        draw_breakpoints();
        draw_buttons();
        osui_statusbar((osui_rect_t){GDBG_WIN_X,
                                      (uint16_t)(GDBG_WIN_Y + GDBG_WIN_H - 28),
                                      GDBG_WIN_W, 28},
                       "gdbgui", "右键退出", OSUI_STATE_PRIMARY);
        osui_present();
        app_sleep_ticks(10);
    }
}
