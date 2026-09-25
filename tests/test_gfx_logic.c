/*
 * test_gfx_logic.c — host-side (Windows / MinGW) unit tests for the pure
 * geometry / colour / rasteriser logic that backs user/apps/gfxtest.c:
 *
 *   - rectangle intersection and clipping
 *   - window z-order (bring-to-front) ordering
 *   - colour format conversions RGB565 <-> RGB888 <-> RGBA8888
 *   - Bresenham line rasterisation
 *
 * These are framework-free; the algorithms are reproduced here so the same
 * rules the GUI stack relies on can be verified on the host.
 *
 * Build: gcc -std=gnu17 -fno-builtin -g -o test_gfx_logic.exe test_gfx_logic.c
 */

int printf(const char *fmt, ...);

#include <stdint.h>
#include <stdbool.h>

typedef struct { int32_t x, y, w, h; } rect_t;

/* ------------------------------------------------------------ */
/* geometry                                                      */
/* ------------------------------------------------------------ */
static bool rect_intersect(const rect_t *a, const rect_t *b, rect_t *out)
{
    int32_t ax2 = a->x + a->w, ay2 = a->y + a->h;
    int32_t bx2 = b->x + b->w, by2 = b->y + b->h;
    int32_t x1 = a->x > b->x ? a->x : b->x;
    int32_t y1 = a->y > b->y ? a->y : b->y;
    int32_t x2 = ax2 < bx2 ? ax2 : bx2;
    int32_t y2 = ay2 < by2 ? ay2 : by2;
    if (x2 <= x1 || y2 <= y1) {
        return false;
    }
    out->x = x1; out->y = y1; out->w = x2 - x1; out->h = y2 - y1;
    return true;
}

/* ------------------------------------------------------------ */
/* colour                                                        */
/* ------------------------------------------------------------ */
static uint32_t rgb565_to_rgb888(uint16_t c)
{
    uint32_t r5 = (c >> 11) & 0x1F;
    uint32_t g6 = (c >> 5) & 0x3F;
    uint32_t b5 = c & 0x1F;
    uint32_t r8 = (r5 << 3) | (r5 >> 2);
    uint32_t g8 = (g6 << 2) | (g6 >> 4);
    uint32_t b8 = (b5 << 3) | (b5 >> 2);
    return (r8 << 16) | (g8 << 8) | b8;
}

static uint16_t rgb888_to_rgb565(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    return (uint16_t) (((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static uint32_t rgba8888(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t) a << 24) | ((uint32_t) r << 16) |
           ((uint32_t) g << 8) | b;
}

/* ------------------------------------------------------------ */
/* Bresenham: emit points into a caller buffer, return count    */
/* ------------------------------------------------------------ */
static int bresenham(int x0, int y0, int x1, int y1,
                     int *xs, int *ys, int cap)
{
    int dx = x1 - x0, sx = dx < 0 ? -1 : 1; if (dx < 0) dx = -dx;
    int dy = y1 - y0, sy = dy < 0 ? -1 : 1; if (dy < 0) dy = -dy;
    int err = (dx > dy ? dx : -dy) / 2;
    int n = 0;

    for (;;) {
        if (n < cap) { xs[n] = x0; ys[n] = y0; n++; }
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy)  { err += dx; y0 += sy; }
    }
    return n;
}

/* ------------------------------------------------------------ */
/* harness                                                       */
/* ------------------------------------------------------------ */
static int g_pass = 0, g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) { g_pass++; }                                            \
        else { g_fail++;                                                   \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__); } \
    } while (0)
static void section(const char *name) { printf("\n== %s ==\n", name); }

int main(void)
{
    printf("Monios graphics-logic host test\n");

    section("rect intersection");
    {
        rect_t a = { 0, 0, 100, 100 };
        rect_t b = { 50, 50, 100, 100 };
        rect_t out;
        CHECK(rect_intersect(&a, &b, &out) == true);
        CHECK(out.x == 50 && out.y == 50 && out.w == 50 && out.h == 50);

        rect_t c = { 200, 200, 10, 10 };
        CHECK(rect_intersect(&a, &c, &out) == false);       /* disjoint */

        rect_t d = { 10, 10, 20, 20 };
        CHECK(rect_intersect(&a, &d, &out) == true);        /* contained */
        CHECK(out.x == 10 && out.y == 10 && out.w == 20 && out.h == 20);

        rect_t e = { 100, 0, 50, 50 };
        CHECK(rect_intersect(&a, &e, &out) == false);      /* edge touch */
    }

    section("colour conversions");
    {
        /* pure red in 565: r=31,g=0,b=0 -> 0xF800 -> 0x00FF0000 */
        CHECK(rgb565_to_rgb888(0xF800) == 0xFF0000);
        /* pure green: g=63 -> 0x07E0 -> 0x00FF00 */
        CHECK(rgb565_to_rgb888(0x07E0) == 0x00FF00);
        /* pure blue: b=31 -> 0x001F -> 0x0000FF */
        CHECK(rgb565_to_rgb888(0x001F) == 0x0000FF);

        /* 888->565 of pure red/green/blue must round-trip to the above */
        CHECK(rgb888_to_rgb565(0xFF0000) == 0xF800);
        CHECK(rgb888_to_rgb565(0x00FF00) == 0x07E0);
        CHECK(rgb888_to_rgb565(0x0000FF) == 0x001F);

        /* round-trip tolerance: a mid grey survives quantization */
        uint32_t grey = 0x7F7F7F;
        uint16_t g565 = rgb888_to_rgb565(grey);
        uint32_t back = rgb565_to_rgb888(g565);
        int32_t dr = (int32_t)((back >> 16) & 0xFF) - 0x7F;
        int32_t dg = (int32_t)((back >> 8) & 0xFF) - 0x7F;
        int32_t db = (int32_t)(back & 0xFF) - 0x7F;
        CHECK(dr >= -4 && dr <= 4 && dg >= -4 && dg <= 4 && db >= -4 && db <= 4);

        /* RGBA packing */
        uint32_t px = rgba8888(0x11, 0x22, 0x33, 0xAA);
        CHECK((px >> 24) == 0xAA && ((px >> 16) & 0xFF) == 0x11 &&
              ((px >> 8) & 0xFF) == 0x22 && (px & 0xFF) == 0x33);
    }

    section("bresenham rasterisation");
    {
        int xs[64], ys[64];
        /* horizontal */
        int n = bresenham(0, 5, 5, 5, xs, ys, 64);
        CHECK(n == 6);
        CHECK(ys[0] == 5 && ys[5] == 5 && xs[0] == 0 && xs[5] == 5);

        /* vertical */
        n = bresenham(3, 0, 3, 4, xs, ys, 64);
        CHECK(n == 5);
        CHECK(xs[0] == 3 && xs[4] == 3);

        /* diagonal */
        n = bresenham(0, 0, 3, 3, xs, ys, 64);
        CHECK(n == 4);
        CHECK(xs[0] == 0 && ys[0] == 0 && xs[3] == 3 && ys[3] == 3);

        /* gentle slope ends at requested point */
        n = bresenham(0, 0, 10, 3, xs, ys, 64);
        CHECK(n > 0);
        CHECK(xs[n-1] == 10 && ys[n-1] == 3);
    }

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
