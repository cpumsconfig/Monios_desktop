/*
 * record.c -- Monios screen recorder (Task 22).
 *
 *   record            start recording (interactive, Q/Esc to stop)
 *   record start      same
 *   record stop       print a hint (state is per-run; stop = Q/Esc in the
 *                     recording session)
 *   -f <fps>          frame rate 1..30 (default 10)
 *   -r <x,y,w,h>      capture a region (default full screen)
 *   -png              store PNG frames (slower); default BMP (fast, no
 *                     compression) per the performance guidance in the task
 *
 * Output: C:\Users\root\Videos\recording_YYYYMMDD_HHMMSS\
 *           frame_00001.bmp / frame_00002.bmp ...
 *           playlist.m3u
 *           recording_info.txt
 *
 * A red indicator dot and elapsed time are drawn on screen before every
 * capture, so they become part of the recording.
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

static uint8_t g_raw[MAX_W * MAX_H * 4u]; /* BGRA -> in-place RGB */
static uint8_t g_out[MAX_W * MAX_H * 3u + 65536u];

static uint16_t g_sw;
static uint16_t g_sh;

static void put_dec(char **p, uint32_t v, uint32_t width)
{
    char tmp[12];
    uint32_t n = 0;

    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n < width && n < sizeof(tmp)) {
        tmp[n++] = '0';
    }
    while (n > 0) {
        *((*p)++) = tmp[--n];
    }
}

static void build_dir(char *buf)
{
    uint8_t t[8];
    uint16_t year;

    strcpy(buf, "C:\\Users\\root\\Videos\\recording_");
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
        put_dec(&buf, (uint32_t) app_ticks(), 8);
    }
    *buf = 0;
}

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

int main(int argc, char **argv)
{
    uint32_t fps = 10;
    bool want_png = false;
    uint16_t rx = 0, ry = 0, rw = 0, rh = 0;
    char dir[PATH_MAX_LEN];
    char path[PATH_MAX_LEN];
    uint32_t frame = 0;
    uint64_t start_tick;
    uint64_t next_tick;
    uint32_t interval;
    bool stop = false;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            const char *s = argv[++i];
            while (*s >= '0' && *s <= '9') {
                v = v * 10u + (uint32_t)(*s - '0');
                s++;
            }
            fps = v;
        } else if (strcmp(argv[i], "-png") == 0) {
            want_png = true;
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            /* x,y,w,h */
            uint32_t vals[4] = { 0, 0, 0, 0 };
            uint32_t v = 0;
            int slot = 0;
            const char *s = argv[++i];

            while (*s && slot < 4) {
                if (*s >= '0' && *s <= '9') {
                    v = v * 10u + (uint32_t)(*s - '0');
                } else {
                    vals[slot++] = v;
                    v = 0;
                }
                s++;
            }
            if (slot < 4) {
                vals[slot] = v;
            }
            rx = (uint16_t) vals[0];
            ry = (uint16_t) vals[1];
            rw = (uint16_t) vals[2];
            rh = (uint16_t) vals[3];
        } else if (strcmp(argv[i], "stop") == 0) {
            fputs("record: stop signal has no shared state; quit an active\n");
            fputs("record session with Q or Esc.\r\n");
            return 0;
        }
    }

    app_enter_graphics_mode();
    g_sw = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_sh = (uint16_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_sw == 0) g_sw = 1024;
    if (g_sh == 0) g_sh = 768;
    if (rw == 0) {
        rx = 0; ry = 0; rw = g_sw; rh = g_sh;
    }
    if (fps < 1) fps = 1;
    if (fps > 30) fps = 30;

    /* prepare output dirs */
    if (!app_file_exists("C:\\Users")) app_file_mkdir("C:\\Users");
    if (!app_file_exists("C:\\Users\\root")) app_file_mkdir("C:\\Users\\root");
    if (!app_file_exists("C:\\Users\\root\\Videos")) app_file_mkdir("C:\\Users\\root\\Videos");
    build_dir(dir);
    if (!app_file_exists(dir)) {
        app_file_mkdir(dir);
    }

    interval = 100u / fps;
    if (interval < 1) interval = 1;
    start_tick = app_ticks();
    next_tick = start_tick;

    fputs("recording... press Q or Esc to stop\r\n");

    while (!stop) {
        uint64_t now = app_ticks();
        app_key_event_t ev;

        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC ||
                (ev.type == EV_CHAR && (ev.ch == 'q' || ev.ch == 'Q'))) {
                stop = true;
            }
        }

        if (!stop && now >= next_tick) {
            uint32_t elapsed_secs = (uint32_t)((now - start_tick) / 100u);

            /* draw indicator BEFORE capture so it lands in the frame */
            app_graphics_fill_rect(10, 10, 14, 14, 0x00EF4444);
            {
                char txt[24];
                uint32_t m = elapsed_secs / 60u;
                uint32_t s = elapsed_secs % 60u;
                char *p = txt;

                strcpy(p, "REC ");
                p += 4;
                put_dec(&p, m, 2);
                *p++ = ':';
                put_dec(&p, s, 2);
                *p = 0;
                app_graphics_draw_text(32, 12, txt, 0x00FFFFFF);
            }
            app_graphics_present();

            if (capture(rx, ry, rw, rh) > 0) {
                uint32_t encoded;

                frame++;
                if (want_png) {
                    encoded = png_encode_rgb(g_out, sizeof(g_out), g_raw, rw, rh);
                    strcpy(path, dir);
                    strcat(path, "\\frame_");
                    {
                        char *p = path + strlen(path);
                        put_dec(&p, frame, 5);
                        strcpy(p, ".png");
                    }
                } else {
                    encoded = bmp_encode_rgb(g_out, sizeof(g_out), g_raw, rw, rh);
                    strcpy(path, dir);
                    strcat(path, "\\frame_");
                    {
                        char *p = path + strlen(path);
                        put_dec(&p, frame, 5);
                        strcpy(p, ".bmp");
                    }
                }
                if (encoded > 0) {
                    app_file_write(path, g_out, encoded);
                }
            }
            next_tick += interval;
            /* catch up slowly rather than bursting */
            if ((int64_t)(now - next_tick) > (int64_t)(interval * 5u)) {
                next_tick = now;
            }
        }
        if (!stop) {
            app_sleep_ticks(1);
        }
    }

    /* write playlist + info */
    {
        uint32_t dur = (uint32_t)((app_ticks() - start_tick) / 100u);
        char info[512];
        char buf[PATH_MAX_LEN + 32];
        char *p = info;

        strcpy(p, "MoniOS screen recording\r\nresolution: ");
        p += strlen(p);
        put_dec(&p, rw, 0);
        strcpy(p, "x");
        p += 1;
        put_dec(&p, rh, 0);
        strcpy(p, "\r\nfps: ");
        p += strlen(p);
        put_dec(&p, fps, 0);
        strcpy(p, "\r\nframes: ");
        p += strlen(p);
        put_dec(&p, frame, 0);
        strcpy(p, "\r\nduration_sec: ");
        p += strlen(p);
        put_dec(&p, dur, 0);
        strcpy(p, "\r\nformat: ");
        p += strlen(p);
        strcpy(p, want_png ? "png" : "bmp");
        p += strlen(p);
        strcpy(p, "\r\n");
        p += strlen(p);

        strcpy(buf, dir);
        strcat(buf, "\\recording_info.txt");
        app_file_write(buf, info, (uint32_t) strlen(info));

        /* m3u playlist: one frame path per line */
        strcpy(buf, dir);
        strcat(buf, "\\playlist.m3u");
        {
            char list[1024];
            char *lp = list;
            uint32_t f;

            for (f = 1; f <= frame && (uint32_t)(lp - list) < sizeof(list) - 80u; f++) {
                strcpy(lp, "frame_");
                lp += 6;
                put_dec(&lp, f, 5);
                strcpy(lp, want_png ? ".png\r\n" : ".bmp\r\n");
                lp += strlen(lp);
            }
            app_file_write(buf, list, (uint32_t) strlen(list));
        }
    }

    fputs("stopped. frames written: ");
    {
        char n[12];
        char *p = n;
        put_dec(&p, frame, 0);
        *p = 0;
        fputs(n);
    }
    fputs("\r\n");
    app_exit(0);
    return 0;
}
