/*
 * screenshot.c -- Monios screen capture tool (Task 21).
 *
 *   screenshot            full screen, PNG
 *   screenshot -f         full screen
 *   screenshot -r         drag a rectangle to capture (Enter confirm, Esc cancel)
 *   screenshot -w         window capture (falls back to full screen: no
 *                         window-info syscall exists yet)
 *   screenshot -d <secs>  delay N seconds before capturing
 *   screenshot -b         save as BMP instead of PNG
 *   screenshot -o <path>  explicit output path
 *
 * Output defaults to C:\Users\root\Pictures\screenshot_YYYYMMDD_HHMMSS.png
 *
 * Pixel data comes from SYS_GRAPHICS_READ_FRAMEBUFFER (73); the RTC filename
 * stamp comes from SYS_GET_RTC_TIME (75).
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"
#include "png.h"

#define EV_CHAR   1
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

#define MAX_W 1024u
#define MAX_H 768u

/* BGRA capture buffer (first), then RGB compacted in place (3 bytes/px). */
static uint8_t g_raw[MAX_W * MAX_H * 4u];
static uint8_t g_out[MAX_W * MAX_H * 3u + 65536u];

static uint16_t g_sw;
static uint16_t g_sh;

static void put_dec(char **p, uint32_t v, uint32_t width)
{
    char tmp[8];
    uint32_t n = 0;

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n < width) {
        tmp[n++] = '0';
    }
    while (n > 0) {
        *((*p)++) = tmp[--n];
    }
}

/* Build default output path. Returns length written. */
static void build_default_path(char *buf, const char *ext)
{
    uint8_t t[8];
    uint16_t year;

    strcpy(buf, "C:\\Users\\root\\Pictures\\screenshot_");
    if (syscall1(SYS_GET_RTC_TIME, (uint64_t) t) == 0) {
        year = (uint16_t)(t[0] | (t[1] << 8));
        put_dec(&buf, year, 4);
        put_dec(&buf, t[2], 2);
        put_dec(&buf, t[3], 2);
        *buf++ = '_';
        put_dec(&buf, t[4], 2);
        put_dec(&buf, t[5], 2);
        put_dec(&buf, t[6], 2);
    } else {
        uint32_t ticks = (uint32_t) app_ticks();
        put_dec(&buf, ticks, 8);
    }
    strcpy(buf, ext);
}

static void ensure_dirs(void)
{
    if (!app_file_exists("C:\\Users")) {
        app_file_mkdir("C:\\Users");
    }
    if (!app_file_exists("C:\\Users\\root")) {
        app_file_mkdir("C:\\Users\\root");
    }
    if (!app_file_exists("C:\\Users\\root\\Pictures")) {
        app_file_mkdir("C:\\Users\\root\\Pictures");
    }
}

/* Capture rect into g_raw, compact BGRA->RGB in place. Returns pixel count. */
static uint32_t capture(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint64_t packed = ((uint64_t) x << 48) | ((uint64_t) y << 32) |
                      ((uint64_t) w << 16) | (uint64_t) h;
    uint64_t got = syscall2(SYS_GRAPHICS_READ_FRAMEBUFFER, (uint64_t) g_raw, packed);
    uint32_t pixels;
    uint32_t i;

    if (got == (uint64_t)-1 || got == 0) {
        return 0;
    }
    pixels = (uint32_t) w * h;
    /* compact BGRA (B,G,R,A) -> R,G,B in place; dst always trails src */
    for (i = 0; i < pixels; i++) {
        uint8_t b = g_raw[(uint64_t) i * 4u + 0u];
        uint8_t g = g_raw[(uint64_t) i * 4u + 1u];
        uint8_t r = g_raw[(uint64_t) i * 4u + 2u];

        g_raw[(uint64_t) i * 3u + 0u] = r;
        g_raw[(uint64_t) i * 3u + 1u] = g;
        g_raw[(uint64_t) i * 3u + 2u] = b;
    }
    return pixels;
}

static void draw_thumb(uint16_t ox, uint16_t oy, uint32_t w, uint32_t h, uint32_t cell)
{
    uint32_t tw = 240 / cell;
    uint32_t th = 160 / cell;
    uint32_t ty;

    for (ty = 0; ty < th; ty++) {
        uint32_t tx;
        uint32_t sy = (ty * h) / th;

        for (tx = 0; tx < tw; tx++) {
            uint32_t sx = (tx * w) / tw;
            uint32_t idx = (sy * w + sx) * 3u;
            uint32_t color = ((uint32_t) g_raw[idx] << 16) |
                             ((uint32_t) g_raw[idx + 1u] << 8) |
                             (uint32_t) g_raw[idx + 2u];

            app_graphics_fill_rect((uint16_t)(ox + tx * cell),
                                   (uint16_t)(oy + ty * cell),
                                   (uint16_t) cell, (uint16_t) cell, color);
        }
    }
}

/* Interactive rectangle selection. Returns false if cancelled. */
static bool select_rect(uint16_t *rx, uint16_t *ry, uint16_t *rw, uint16_t *rh)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev = 0;
    bool dragging = false;
    int32_t ax = 0, ay = 0;
    int32_t cx = 0, cy = 0;
    bool selected = false;

    for (;;) {
        app_key_event_t ev;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC) {
                return false;
            }
            if (selected && ev.type == EV_CHAR && ev.ch == '\n') {
                int32_t x1 = ax < cx ? ax : cx;
                int32_t y1 = ay < cy ? ay : cy;
                int32_t x2 = ax < cx ? cx : ax;
                int32_t y2 = ay < cy ? cy : ay;

                if (x2 - x1 < 4) x2 = x1 + 4;
                if (y2 - y1 < 4) y2 = y1 + 4;
                *rx = (uint16_t) x1;
                *ry = (uint16_t) y1;
                *rw = (uint16_t)(x2 - x1);
                *rh = (uint16_t)(y2 - y1);
                return true;
            }
        }

        app_get_mouse(&mouse);
        if ((mouse.buttons & 0x01u) && !(prev & 0x01u)) {
            dragging = true;
            selected = false;
            ax = mouse.x_pixels;
            ay = mouse.y_pixels;
            cx = ax;
            cy = ay;
        }
        if (mouse.buttons & 0x01u) {
            if (dragging) {
                cx = mouse.x_pixels;
                cy = mouse.y_pixels;
            }
        } else if (dragging) {
            dragging = false;
            selected = true;
        }
        prev = mouse.buttons;

        /* render */
        app_graphics_fill_rect(0, 0, g_sw, g_sh, 0x00000000);
        app_graphics_draw_text(20, 20, "Drag to select a region, then press Enter to save (Esc cancels)",
                               0x00FFFFFF);
        if (ax != cx || ay != cy) {
            int32_t x1 = ax < cx ? ax : cx;
            int32_t y1 = ay < cy ? ay : cy;
            int32_t x2 = ax < cx ? cx : ax;
            int32_t y2 = ay < cy ? cy : ay;

            /* outline the selection */
            app_graphics_fill_rect((uint16_t) x1, (uint16_t) y1, (uint16_t)(x2 - x1), 2, 0x003C6FEA);
            app_graphics_fill_rect((uint16_t) x1, (uint16_t)(y2 - 2), (uint16_t)(x2 - x1), 2, 0x003C6FEA);
            app_graphics_fill_rect((uint16_t) x1, (uint16_t) y1, 2, (uint16_t)(y2 - y1), 0x003C6FEA);
            app_graphics_fill_rect((uint16_t)(x2 - 2), (uint16_t) y1, 2, (uint16_t)(y2 - y1), 0x003C6FEA);
        }
        if (selected) {
            app_graphics_draw_text(20, 44, "Region selected. Enter = save, Esc = cancel, drag again = redo",
                                   0x007CFFB2);
        }
        app_graphics_present();
        app_sleep_ticks(2);
    }
}

int main(int argc, char **argv)
{
    bool region = false;
    bool bmp = false;
    uint32_t delay_secs = 0;
    const char *out_path = 0;
    uint16_t rx = 0, ry = 0, rw = 0, rh = 0;
    char path[PATH_MAX_LEN];
    uint32_t encoded;
    uint32_t pixels;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            region = true;
        } else if (strcmp(argv[i], "-b") == 0) {
            bmp = true;
        } else if (strcmp(argv[i], "-w") == 0) {
            /* no window-info syscall yet: fall back to full screen */
        } else if (strcmp(argv[i], "-f") == 0) {
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            const char *s = argv[++i];

            while (*s >= '0' && *s <= '9') {
                v = v * 10u + (uint32_t)(*s - '0');
                s++;
            }
            delay_secs = v;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        }
    }

    app_enter_graphics_mode();
    g_sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_sw == 0) g_sw = 1024;
    if (g_sh == 0) g_sh = 768;

    if (region) {
        if (!select_rect(&rx, &ry, &rw, &rh)) {
            fputs("screenshot: cancelled\r\n");
            app_exit(0);
        }
    } else {
        rx = 0;
        ry = 0;
        rw = g_sw;
        rh = g_sh;
    }

    if (delay_secs > 0) {
        for (uint32_t d = delay_secs; d > 0; d--) {
            app_graphics_fill_rect(0, 0, g_sw, g_sh, 0x00000000);
            app_graphics_draw_text(g_sw / 2u - 100u, g_sh / 2u, "Capturing in...", 0x00FFFFFF);
            {
                char num[4];
                num[0] = (char)('0' + d);
                num[1] = 0;
                app_graphics_draw_text(g_sw / 2u + 60u, g_sh / 2u, num, 0x00FACC15);
            }
            app_graphics_present();
            app_sleep_ticks(100);
        }
    }

    ensure_dirs();
    if (out_path == 0) {
        build_default_path(path, bmp ? ".bmp" : ".png");
        out_path = path;
    } else {
        strcpy(path, out_path);
    }

    pixels = capture(rx, ry, rw, rh);
    if (pixels == 0) {
        fputs("screenshot: framebuffer read failed\r\n");
        app_exit(1);
    }

    if (bmp) {
        encoded = bmp_encode_rgb(g_out, sizeof(g_out), g_raw, rw, rh);
    } else {
        encoded = png_encode_rgb(g_out, sizeof(g_out), g_raw, rw, rh);
    }
    if (encoded == 0) {
        fputs("screenshot: encode failed\r\n");
        app_exit(1);
    }
    if (app_file_write(out_path, g_out, encoded) != (int) encoded) {
        fputs("screenshot: write failed\r\n");
        app_exit(1);
    }

    /* preview + confirmation */
    app_graphics_fill_rect(0, 0, g_sw, g_sh, 0x00101826);
    app_graphics_draw_text(20, 20, "Screenshot saved:", 0x007CFFB2);
    app_graphics_draw_text(20, 44, out_path, 0x00FFFFFF);
    draw_thumb(20, 80, rw, rh, 2);
    app_graphics_draw_text(20, 260, "Press any key to exit", 0x0094A3B8);
    app_graphics_present();

    fputs("saved: ");
    fputs(out_path);
    fputs("\r\n");

    for (;;) {
        app_key_event_t ev;
        if (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            app_exit(0);
        }
        app_sleep_ticks(5);
    }
    return 0;
}
