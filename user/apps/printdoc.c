/*
 * printdoc.c -- Monios print dialog (Task 3).
 *
 * GUI text editor with print controls: send text to LPT1 (syscall 100),
 * show printer status (online / paper / busy / error), basic bold toggle,
 * and print progress (lines printed / total lines).
 *
 * If no physical parallel port exists the kernel captures the output in a
 * software spool (QEMU emulation path).
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"
#include "lpt.h"

#define EV_CHAR   1
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

#define DOC_MAX 4096U

static char g_doc[DOC_MAX];
static uint32_t g_doc_len;
static bool g_bold;
static uint8_t g_status_packed;
static uint32_t g_lines_done, g_lines_total;

static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    app_graphics_fill_rect((uint16_t) x, (uint16_t) y, (uint16_t) w, (uint16_t) h, color);
}

static void query_status(void)
{
    g_status_packed = (uint8_t) syscall3(SYS_LPT_CTL, LPT_CTL_STATUS, 0, 0);
}

static void append_text(const char *txt, uint32_t len)
{
    uint32_t i;
    for (i = 0U; i < len && g_doc_len < DOC_MAX - 1U; i++) {
        g_doc[g_doc_len++] = txt[i];
    }
    g_doc[g_doc_len] = 0;
}

static void do_print(void)
{
    /* Build the formatted job: ESC/POS init, optional bold on/off, text, FF. */
    static uint8_t job[DOC_MAX + 16U];
    uint32_t n = 0U;
    uint32_t lines = 1U;
    uint32_t i;

    job[n++] = 0x1BU; job[n++] = '@';               /* init */
    if (g_bold) { job[n++] = 0x1BU; job[n++] = 'E'; job[n++] = 1U; }
    for (i = 0U; i < g_doc_len; i++) {
        job[n++] = (uint8_t) g_doc[i];
        if (g_doc[i] == '\n') lines++;
    }
    if (g_bold) { job[n++] = 0x1BU; job[n++] = 'E'; job[n++] = 0U; }
    job[n++] = 0x0CU;                              /* form feed */

    g_lines_total = lines;
    g_lines_done = 0;
    /* send in chunks via syscall 100 */
    {
        uint32_t off = 0U;
        while (off < n) {
            uint32_t chunk = n - off;
            if (chunk > 256U) chunk = 256U;
            syscall3(SYS_LPT_CTL, LPT_CTL_WRITE, (uint64_t) (job + off), chunk);
            off += chunk;
            g_lines_done++;
            app_sleep_ticks(2);
        }
    }
    g_lines_done = g_lines_total;
}

static void draw_ui(void)
{
    char txt[80];
    draw_rect(0, 0, 1024, 768, 0x00202028);
    app_graphics_draw_text(40, 30, "Monios 打印 (Print Document)", 0x00FFFFFF);

    /* edit area */
    draw_rect(40, 70, 944, 480, 0x00101018);
    app_graphics_draw_text(50, 80, g_doc[0] ? g_doc : "(type text here...)", 0x00DDDDDD);

    /* status line */
    {
        const char *st = "Offline";
        uint32_t col = 0x00FF6666;
        if (g_status_packed & 0x80U) { st = "Emulated spool"; col = 0x00FFCC44; }
        else if (g_status_packed & 8U) { st = "Online"; col = 0x0044DD44; }
        if (g_status_packed & 2U) { st = "Paper out!"; col = 0x00FF4444; }
        if (g_status_packed & 1U) { st = "Busy"; col = 0x00FFAA44; }
        app_graphics_draw_text(40, 565, st, col);
    }

    /* progress */
    sprintf(txt, "Progress: %u / %u lines", g_lines_done, g_lines_total);
    app_graphics_draw_text(300, 565, txt, 0x00CCCCCC);

    /* buttons */
    draw_rect(40, 620, 160, 50, 0x0044AA44);
    app_graphics_draw_text(60, 635, "Print", 0x00FFFFFF);
    draw_rect(220, 620, 160, 50, g_bold ? 0x00AA6644 : 0x004060A0);
    app_graphics_draw_text(230, 635, g_bold ? "Bold: On" : "Bold: Off", 0x00FFFFFF);
    draw_rect(400, 620, 160, 50, 0x00606060);
    app_graphics_draw_text(410, 635, "Reset", 0x00FFFFFF);
    draw_rect(580, 620, 160, 50, 0x00AA4444);
    app_graphics_draw_text(600, 635, "Quit", 0x00FFFFFF);

    app_graphics_present();
}

int main(int argc, char **argv)
{
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;
    bool quit = false;
    (void) argc; (void) argv;

    g_doc_len = 0;
    g_doc[0] = 0;
    append_text("Monios print demo.\r\nLine 2: bold toggle works.\r\n", 49U);

    app_enter_graphics_mode();
    syscall3(SYS_LPT_CTL, LPT_CTL_INIT, 0, 0);

    while (!quit) {
        app_key_event_t ev;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC) quit = true;
            else if (ev.type == EV_CHAR) {
                if (ev.ch == '\r') append_text("\r\n", 2U);
                else if (ev.ch == 127) { if (g_doc_len > 0) g_doc[--g_doc_len] = 0; }
                else append_text(&ev.ch, 1U);
            }
        }

        query_status();

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t) mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                if (mouse.x_pixels >= 40 && mouse.x_pixels < 200 &&
                    mouse.y_pixels >= 620 && mouse.y_pixels < 670) do_print();
                else if (mouse.x_pixels >= 220 && mouse.x_pixels < 380 &&
                         mouse.y_pixels >= 620 && mouse.y_pixels < 670) g_bold = !g_bold;
                else if (mouse.x_pixels >= 400 && mouse.x_pixels < 560 &&
                         mouse.y_pixels >= 620 && mouse.y_pixels < 670)
                    syscall3(SYS_LPT_CTL, LPT_CTL_RESET, 0, 0);
                else if (mouse.x_pixels >= 580 && mouse.x_pixels < 740 &&
                         mouse.y_pixels >= 620 && mouse.y_pixels < 670) quit = true;
            }
            prev_btn = btn;
        }

        draw_ui();
        app_sleep_ticks(2);
    }

    app_exit(0);
    return 0;
}
