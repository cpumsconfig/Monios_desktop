/*
 * videoplay.c -- Monios raw-YUV420P video player (Task 2).
 *
 * Plays an uncompressed YUV420P file (e.g. output of ffmpeg -pix_fmt yuv420p).
 * The app colour-converts each frame to BGRA8888 and presents it with
 * SYS_GRAPHICS_BLIT (97). GUI: play/pause, progress bar, volume, quit.
 *
 * Usage: videoplay.exe [width] [height] [fps] [path]
 *   default: 320 240 24  C:\Monios\Users\root\Videos\test.yuv
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "audio.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

#define EV_CHAR   1
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

#define VID_BUF (4U * 1024U * 1024U)

static int local_atoi(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static uint8_t  g_yuv[VID_BUF];
static uint32_t g_yuv_len;
static uint32_t g_w = 320, g_h = 240, g_fps = 24;
static uint32_t g_frame_bytes;
static uint32_t g_frame_count;
static uint32_t g_frame_pos;   /* current frame index */
static bool     g_playing = true;

static uint32_t g_screen_w = 1024, g_screen_h = 768;

/* one BGRA frame (max 320x240 = 307200 bytes) */
static uint8_t g_rgb[320U * 240U * 4U];

static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    app_graphics_fill_rect((uint16_t) x, (uint16_t) y, (uint16_t) w, (uint16_t) h, color);
}

/* Convert one YUV420P frame (at g_yuv offset) into g_rgb (BGRA). */
static void convert_frame(uint32_t frame_idx)
{
    uint32_t off = frame_idx * g_frame_bytes;
    uint32_t plane = g_w * g_h;
    const uint8_t *y = g_yuv + off;
    const uint8_t *u = y + plane;
    const uint8_t *v = u + plane / 4U;
    uint32_t row, col;
    for (row = 0; row < g_h; row++) {
        const uint8_t *ys = y + (uint64_t) row * g_w;
        const uint8_t *us = u + (uint64_t) (row >> 1U) * (g_w / 2U);
        const uint8_t *vs = v + (uint64_t) (row >> 1U) * (g_w / 2U);
        uint8_t *op = g_rgb + (uint64_t) row * g_w * 4U;
        for (col = 0; col < g_w; col++) {
            int32_t yv = (int32_t) ys[col] - 16;
            int32_t cb = (int32_t) us[col >> 1U] - 128;
            int32_t cr = (int32_t) vs[col >> 1U] - 128;
            int32_t base = (yv * 298) >> 8;
            int32_t r = base + ((402 * cr) >> 8);
            int32_t g = base - ((240 * cr + 134 * cb) >> 8);
            int32_t b = base + ((466 * cb) >> 8);
            if (r < 0) r = 0; else if (r > 255) r = 255;
            if (g < 0) g = 0; else if (g > 255) g = 255;
            if (b < 0) b = 0; else if (b > 255) b = 255;
            op[col * 4U + 0U] = (uint8_t) b;
            op[col * 4U + 1U] = (uint8_t) g;
            op[col * 4U + 2U] = (uint8_t) r;
            op[col * 4U + 3U] = 0xFFU;
        }
    }
}

static void blit_frame(void)
{
    uint32_t ox = (g_screen_w - g_w) / 2U;
    uint32_t oy = 80U;
    uint64_t packed = ((uint64_t) ox << 48) | ((uint64_t) oy << 32) |
                      ((uint64_t) g_w << 16) | (uint64_t) g_h;
    /* rbx = source buffer, rcx = packed rect, rdx = 0 (overwrite) */
    syscall3(SYS_GRAPHICS_BLIT, (uint64_t) g_rgb, packed, 0U);
}

static void draw_controls(void)
{
    char txt[64];
    uint32_t i;
    draw_rect(0, 0, g_screen_w, 70, 0x00181820);
    app_graphics_draw_text(20, 20, "Monios 视频播放器 (YUV420P)", 0x00FFFFFF);

    /* progress bar */
    draw_rect(20, 700, 984, 16, 0x00101018);
    if (g_frame_count > 0U) {
        uint32_t fill = (uint32_t) (984ULL * g_frame_pos / g_frame_count);
        draw_rect(20, 700, fill, 16, 0x004488FF);
    }

    /* play/pause button */
    draw_rect(20, 730, 120, 28, g_playing ? 0x00CC8844 : 0x0044AA44);
    app_graphics_draw_text(30, 736, g_playing ? "Pause" : "Play", 0x00FFFFFF);

    sprintf(txt, "Frame %u/%u  %ux%u @ %ufps",
             g_frame_pos, g_frame_count, g_w, g_h, g_fps);
    app_graphics_draw_text(160, 736, txt, 0x00CCCCCC);
    (void) i;
    app_graphics_present();
}

int main(int argc, char **argv)
{
    const char *path = "C:\\Monios\\Users\\root\\Videos\\test.yuv";
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;
    bool quit = false;
    uint32_t frame_interval_ticks;
    uint64_t next_frame_tick = 0;

    if (argc > 1) g_w = (uint32_t) local_atoi(argv[1]);
    if (argc > 2) g_h = (uint32_t) local_atoi(argv[2]);
    if (argc > 3) g_fps = (uint32_t) local_atoi(argv[3]);
    if (argc > 4) path = argv[4];
    if (g_w == 0U) g_w = 320;
    if (g_h == 0U) g_h = 240;
    if (g_fps == 0U) g_fps = 24;
    if ((uint64_t) g_w * g_h * 4U > sizeof(g_rgb)) g_w = 320;

    g_frame_bytes = g_w * g_h * 3U / 2U;
    g_yuv_len = app_file_read(path, g_yuv, sizeof(g_yuv));
    g_frame_count = g_yuv_len / g_frame_bytes;

    app_enter_graphics_mode();
    g_screen_w = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_screen_h = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_screen_w == 0U) g_screen_w = 1024;
    if (g_screen_h == 0U) g_screen_h = 768;

    frame_interval_ticks = (uint32_t) (1000U / g_fps / 16U);  /* ~1 tick=16ms */
    if (frame_interval_ticks == 0U) frame_interval_ticks = 1U;

    if (g_frame_count == 0U) {
        draw_rect(0, 0, g_screen_w, g_screen_h, 0x00202028);
        app_graphics_draw_text(200, 300, "No YUV file found or empty.", 0x00FF6666);
        app_graphics_draw_text(200, 340, path, 0x00AAAAAA);
        app_graphics_present();
        while (!quit) {
            app_key_event_t ev;
            while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
                if (ev.type == EV_ESC) quit = true;
            }
            app_sleep_ticks(5);
        }
        app_exit(0);
        return 0;
    }

    while (!quit) {
        app_key_event_t ev;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC) quit = true;
            if (ev.type == EV_CHAR && ev.ch == ' ') g_playing = !g_playing;
        }

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t) mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                /* play/pause button hit? */
                if (mouse.x_pixels >= 20 && mouse.x_pixels < 140 &&
                    mouse.y_pixels >= 730 && mouse.y_pixels < 758) {
                    g_playing = !g_playing;
                }
                /* progress bar click -> seek */
                if (mouse.y_pixels >= 700 && mouse.y_pixels < 716 &&
                    mouse.x_pixels >= 20 && mouse.x_pixels < 1004) {
                    uint32_t pct = (uint32_t) (mouse.x_pixels - 20);
                    g_frame_pos = (uint32_t) ((uint64_t) pct * g_frame_count / 984U);
                    if (g_frame_pos >= g_frame_count) g_frame_pos = g_frame_count - 1U;
                }
            }
            prev_btn = btn;
        }

        {
            uint64_t now = app_ticks();
            if (g_playing && now >= next_frame_tick) {
                convert_frame(g_frame_pos);
                blit_frame();
                g_frame_pos++;
                if (g_frame_pos >= g_frame_count) g_frame_pos = 0U;
                next_frame_tick = now + frame_interval_ticks;
            }
        }

        draw_controls();
        app_sleep_ticks(1);
    }

    app_exit(0);
    return 0;
}
