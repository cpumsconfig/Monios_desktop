/*
 * gfxtest.c — Monios graphics rendering verification tool.
 *
 * Usage:
 *   gfxtest bench      fill-rect / draw-text / present throughput (FPS)
 *   gfxtest windows    draggable test windows (mouse driven), redraw storm
 *   gfxtest animate    moving shapes, per-frame latency histogram
 *   gfxtest info       framebuffer + GPU acceleration statistics
 *
 * All rendering goes through the existing SYS_GRAPHICS_* entry points wrapped
 * by app_graphics_*(); resolution comes from SYS_GRAPHICS_GET_WIDTH/HEIGHT
 * and GPU/window counters from SYS_SYSTEM_STATUS.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "string.h"
#include "stdint.h"
#include "syscall.h"

#define BENCH_FRAMES   120u
#define ANIM_FRAMES    200u
#define WINDOW_FRAMES  120u

static uint32_t g_width;
static uint32_t g_height;

static void gfx_open(void)
{
    app_enter_graphics_mode();
    g_width = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_height = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_width == 0) {
        g_width = 1024;
    }
    if (g_height == 0) {
        g_height = 768;
    }
}

static void report_times(const char *label, uint32_t frames,
                         uint64_t total_ticks, uint64_t min_dt, uint64_t max_dt,
                         uint32_t draw_ops)
{
    fputs("--- ");
    fputs(label);
    fputs(" ---\r\n  frames      : ");
    print_uint(frames);
    fputs("\r\n  total ticks : ");
    print_uint((uint32_t) total_ticks);
    fputs("\r\n  draw ops    : ");
    print_uint(draw_ops);
    fputs("\r\n  min frame   : ");
    print_uint((uint32_t) min_dt);
    fputs(" ticks\r\n  max frame   : ");
    print_uint((uint32_t) max_dt);
    fputs(" ticks\r\n  avg frame   : ");
    if (total_ticks > 0) {
        print_uint((uint32_t) (total_ticks / frames));
    } else {
        print_uint(0);
    }
    fputs(" ticks\r\n  throughput  : ");
    if (total_ticks > 0) {
        /* assume the legacy 100 Hz system tick for a coarse fps figure */
        uint32_t fps = (frames * 100u) / (uint32_t) total_ticks;
        print_uint(fps);
        fputs(" fps (approx @100Hz tick)\r\n");
    } else {
        fputs("n/a\r\n");
    }
}

static int cmd_bench(void)
{
    uint32_t i;
    uint32_t ops = 0;
    uint64_t wall_t0;
    uint64_t wall_t1;
    uint64_t min_dt = 0;
    uint64_t max_dt = 0;
    static const uint32_t colors[4] = { 0x001E3A5F, 0x0038BDF8, 0x00F8FAFC, 0x00F59E0B };

    gfx_open();
    wall_t0 = app_ticks();
    for (i = 0; i < BENCH_FRAMES; i++) {
        uint64_t f0 = app_ticks();
        uint32_t bx = (i * 137u) % (g_width - 128u);
        uint32_t by = (i * 89u) % (g_height - 96u);

        app_graphics_fill_rect(0, 0, (uint16_t) g_width, (uint16_t) g_height, 0x000F172A);
        ops++;
        for (uint32_t c = 0; c < 4; c++) {
            app_graphics_fill_rect((uint16_t) (bx + c * 30u), (uint16_t) (by + c * 20u),
                                   64, 48, colors[c]);
            ops++;
        }
        app_graphics_draw_text(16, 16, "gfxtest bench frame", 0x00FFFFFF);
        ops++;
        app_graphics_present();
        ops++;
        uint64_t dt = app_ticks() - f0;
        if (min_dt == 0 || dt < min_dt) {
            min_dt = dt;
        }
        if (dt > max_dt) {
            max_dt = dt;
        }
    }
    wall_t1 = app_ticks();
    report_times("bench", BENCH_FRAMES, wall_t1 - wall_t0, min_dt, max_dt, ops);
    return 0;
}

static int cmd_windows(void)
{
    static struct {
        uint32_t x;
        uint32_t y;
        uint32_t w;
        uint32_t h;
        uint32_t color;
        const char *title;
    } wins[3] = {
        { 60, 60, 260, 160, 0x001E3A5F, "w1" },
        { 380, 120, 260, 160, 0x0038BDF8, "w2" },
        { 200, 320, 260, 160, 0x00F59E0B, "w3" },
    };
    uint32_t i;
    uint32_t ops = 0;
    uint64_t wall_t0;
    uint64_t wall_t1;
    uint64_t min_dt = 0;
    uint64_t max_dt = 0;
    app_mouse_snapshot_t mouse;

    gfx_open();
    wall_t0 = app_ticks();
    for (i = 0; i < WINDOW_FRAMES; i++) {
        uint64_t f0 = app_ticks();

        app_get_mouse(&mouse);
        /* drag the top window toward the cursor (模拟窗口拖拽) */
        if (mouse.buttons & 0x01u) {
            int32_t nx = mouse.x_pixels - (int32_t) (wins[0].w / 2u);
            int32_t ny = mouse.y_pixels - (int32_t) 20;
            if (nx < 0) { nx = 0; }
            if (ny < 0) { ny = 0; }
            wins[0].x = (uint32_t) nx;
            wins[0].y = (uint32_t) ny;
        }

        app_graphics_fill_rect(0, 0, (uint16_t) g_width, (uint16_t) g_height, 0x00111827);
        ops++;
        for (uint32_t w = 0; w < 3; w++) {
            app_graphics_fill_rect((uint16_t) wins[w].x, (uint16_t) wins[w].y,
                                   (uint16_t) wins[w].w, (uint16_t) wins[w].h,
                                   wins[w].color);
            ops++;
            app_graphics_fill_rect((uint16_t) wins[w].x, (uint16_t) wins[w].y,
                                   (uint16_t) wins[w].w, 16, 0x000F172A);
            ops++;
            app_graphics_draw_text((uint16_t) (wins[w].x + 6u), (uint16_t) (wins[w].y + 2u),
                                   wins[w].title, 0x00FFFFFF);
            ops++;
        }
        app_graphics_present();
        ops++;
        uint64_t dt = app_ticks() - f0;
        if (min_dt == 0 || dt < min_dt) { min_dt = dt; }
        if (dt > max_dt) { max_dt = dt; }
    }
    wall_t1 = app_ticks();
    report_times("windows", WINDOW_FRAMES, wall_t1 - wall_t0, min_dt, max_dt, ops);
    return 0;
}

static int cmd_animate(void)
{
    uint32_t i;
    uint32_t ops = 0;
    uint64_t wall_t0;
    uint64_t wall_t1;
    uint64_t min_dt = 0;
    uint64_t max_dt = 0;

    gfx_open();
    wall_t0 = app_ticks();
    for (i = 0; i < ANIM_FRAMES; i++) {
        uint64_t f0 = app_ticks();
        uint32_t rx = (i * 7u) % (g_width - 40u);
        uint32_t ry = ((i * 5u) % (g_height - 40u));
        uint32_t bx = g_width - 40u - ((i * 3u) % (g_width - 40u));

        app_graphics_fill_rect(0, 0, (uint16_t) g_width, (uint16_t) g_height, 0x000B1220);
        ops++;
        app_graphics_fill_rect((uint16_t) rx, (uint16_t) ry, 40, 40, 0x0022D3EE);
        ops++;
        app_graphics_fill_rect((uint16_t) bx, 200, 32, 32, 0x00A3E635);
        ops++;
        app_graphics_draw_text(16, (uint16_t) (g_height - 24u), "anim running", 0x00E5E7EB);
        ops++;
        app_graphics_present();
        ops++;
        uint64_t dt = app_ticks() - f0;
        if (min_dt == 0 || dt < min_dt) { min_dt = dt; }
        if (dt > max_dt) { max_dt = dt; }
    }
    wall_t1 = app_ticks();
    report_times("animate", ANIM_FRAMES, wall_t1 - wall_t0, min_dt, max_dt, ops);
    return 0;
}

static int cmd_info(void)
{
    app_system_status_t st;

    g_width = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_height = (uint32_t) monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);

    fputs("Monios graphics device\r\n");
    fputs("  resolution   : ");
    print_uint(g_width);
    fputs(" x ");
    print_uint(g_height);
    fputs("\r\n");
    if (app_get_system_status(&st) == 0) {
        fputs("  gpu submits  : ");
        print_uint(st.gpu_submits);
        fputs("\r\n  gpu presents : ");
        print_uint(st.gpu_presents);
        fputs("\r\n  gpu pending  : ");
        print_uint(st.gpu_pending);
        fputs("\r\n  open windows : ");
        print_uint(st.wm_windows);
        fputs("  focused: ");
        print_uint(st.wm_focused);
        fputs("\r\n");
    } else {
        fputs("  (system status unavailable)\r\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("usage:\r\n");
        fputs("  gfxtest bench\r\n");
        fputs("  gfxtest windows\r\n");
        fputs("  gfxtest animate\r\n");
        fputs("  gfxtest info\r\n");
        return 1;
    }
    if (strcmp(argv[1], "bench") == 0) { return cmd_bench(); }
    if (strcmp(argv[1], "windows") == 0) { return cmd_windows(); }
    if (strcmp(argv[1], "animate") == 0) { return cmd_animate(); }
    if (strcmp(argv[1], "info") == 0) { return cmd_info(); }

    fputs("unknown command\r\n");
    return 1;
}
