/*
 * graphics_demo.c - MoniOS graphics example (feature 32).
 *
 * Demonstrates: enter graphics mode, fill rectangles, draw text,
 * emulate lines with thin rectangles, and present a frame.
 *
 * Build (see report):
 *   x86_64-w64-mingw32-gcc examples/graphics_demo.c \
 *       -I user/lib -I include -o C:\\Monios\\Apps\\graphics_demo.exe
 */
#include "appsys.h"
#include "console_dll.h"
#include <stdio.h>

/* 32-bit RGB colors used by app_graphics_fill_rect */
#define COL_BG     0x00101820u
#define COL_RED    0x00E04040u
#define COL_GREEN  0x0040C060u
#define COL_BLUE   0x004080E0u
#define COL_WHITE  0x00F0F0F0u
#define COL_YELLOW 0x00E8D040u

static void draw_hline(uint16_t x, uint16_t y, uint16_t len, uint32_t color)
{
    app_graphics_fill_rect(x, y, len, 2, color);
}
static void draw_vline(uint16_t x, uint16_t y, uint16_t len, uint32_t color)
{
    app_graphics_fill_rect(x, y, 2, len, color);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    console_set_title("MoniOS Graphics Demo");
    printf("Switching to graphics mode...\n");

    app_enter_graphics_mode();

    /* Background */
    app_graphics_fill_rect(0, 0, 1024, 768, COL_BG);

    /* A few filled rectangles of different colours */
    app_graphics_fill_rect(60,  60, 200, 140, COL_RED);
    app_graphics_fill_rect(300, 60, 200, 140, COL_GREEN);
    app_graphics_fill_rect(540, 60, 200, 140, COL_BLUE);

    /* Diagonal-ish lines (orthogonal: horizontal + vertical bars) */
    draw_hline(60, 260, 680, COL_YELLOW);
    draw_vline(60,  260, 200, COL_YELLOW);
    draw_vline(740, 260, 200, COL_YELLOW);

    /* Text overlays */
    app_graphics_draw_text(60,  30, "MoniOS Graphics Demo", COL_WHITE);
    app_graphics_draw_text(60, 280, "rects + lines + text", COL_WHITE);

    app_graphics_present();

    printf("Frame drawn. Press a key to return to console.\n");
    app_sleep_ticks(200);

    return 0;
}
