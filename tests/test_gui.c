/*
 * test_gui.c — host-side (Windows / MinGW) GUI / window-manager tests.
 *
 * Real kernel logic pulled in by #include:
 *   - kernel/ui/osui/component.c : the osui component registry (register /
 *     dedup-merge / custom manifest parsing / enumeration). The renderer
 *     and framebuffer are stubbed out.
 *
 * The graphics.c window manager itself is tightly coupled to the frame
 * buffer (335 KB of drawing code), so per the task brief the geometric
 * window-manager invariants — z-order raise/lower, rectangle clipping and
 * point hit-testing — are exercised here as self-contained unit tests on a
 * Monios-style {x,y,w,h} window list, mirroring the operations the real
 * Wm performs in graphics.c.
 *
 * Build: gcc -I ../include -I . -o test_gui.exe test_gui.c stubs_ext.c
 */

int printf(const char *fmt, ...);

#include "common.h"
#include "file.h"
#include "kernel.h"
#include "osui.h"
#include "path.h"
#include "string.h"
#include "ui.h"

/* ---- code under test: osui component registry (same TU => statics OK) ---- */
#include "../kernel/ui/osui/component.c"

/* ============================================================
 *  Tiny test harness
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) { g_pass++; }                                            \
        else {                                                             \
            g_fail++;                                                      \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);   \
        }                                                                  \
    } while (0)

static void section(const char *name) { printf("\n== %s ==\n", name); }

/* ============================================================
 *  osui component registry (real component.c)
 * ============================================================ */
static void test_osui_register(void)
{
    section("osui_component_register / enumeration");
    /* wipe the registry directly (same-TU static) */
    g_component_count = 0;

    CHECK(osui_component_register((const char *) 0, OSUI_COMPONENT_WIDGET) == false);
    CHECK(osui_component_register("", OSUI_COMPONENT_WIDGET) == false);

    CHECK(osui_component_register("button", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET) == true);
    CHECK(osui_component_count() == 1);
    CHECK(osui_component_register("slider", OSUI_COMPONENT_WIDGET) == true);
    CHECK(osui_component_count() == 2);

    const osui_component_info_t *c = osui_component_at(0);
    CHECK(c != 0);
    CHECK(strcmp(c->name, "button") == 0);
    CHECK((c->flags & OSUI_COMPONENT_WIDGET) != 0);
    CHECK((c->flags & OSUI_COMPONENT_BUILTIN) != 0);

    CHECK(osui_component_at(2) == 0);          /* out of range */
    CHECK(osui_component_at(999) == 0);
}

static void test_osui_dedup(void)
{
    section("osui duplicate registration merges flags");
    g_component_count = 0;
    osui_component_register("card", OSUI_COMPONENT_WIDGET);
    CHECK(osui_component_count() == 1);
    /* second register of the same name ORs flags rather than adding a row */
    osui_component_register("card", OSUI_COMPONENT_LAYOUT);
    CHECK(osui_component_count() == 1);
    const osui_component_info_t *c = osui_component_at(0);
    CHECK((c->flags & OSUI_COMPONENT_WIDGET) != 0);
    CHECK((c->flags & OSUI_COMPONENT_LAYOUT) != 0);
}

static void test_osui_custom_manifest(void)
{
    section("osui_component_register_custom (manifest parsing)");
    g_component_count = 0;

    const char *manifest =
        "# a comment line\n"
        "my-widget\n"
        "name=my-widget\n"
        "version=1.2.3\n"
        "flags=widget, custom\n"
        "priority=50\n";
    CHECK(osui_component_register_custom("C:\\Comps\\my.osc", manifest) == true);
    CHECK(osui_component_count() == 1);

    const osui_component_info_t *c = osui_component_at(0);
    CHECK(strcmp(c->name, "my-widget") == 0);
    CHECK(strcmp(c->version, "1.2.3") == 0);
    CHECK((c->flags & OSUI_COMPONENT_WIDGET) != 0);
    CHECK((c->flags & OSUI_COMPONENT_CUSTOM) != 0);
    CHECK(c->priority == 50);

    /* missing path / manifest rejected */
    CHECK(osui_component_register_custom("", manifest) == false);
    CHECK(osui_component_register_custom("x", (const char *) 0) == false);
}

static void test_osui_init_seeds(void)
{
    section("osui_init seeds the builtin component set");
    osui_init();
    CHECK(osui_component_count() > 10);       /* many builtins seeded */
    /* 'window' and 'desktop' builtins must be present */
    bool saw_window = false, saw_desktop = false;
    for (uint32_t i = 0; i < osui_component_count(); i++) {
        const osui_component_info_t *c = osui_component_at(i);
        if (strcmp(c->name, "window") == 0)  { saw_window = true; }
        if (strcmp(c->name, "desktop") == 0) { saw_desktop = true; }
    }
    CHECK(saw_window);
    CHECK(saw_desktop);
}

/* ============================================================
 *  Window-manager geometry (self-contained, Monios-style windows).
 *
 *  Windows are stored in a z-ordered array: index 0 = bottom, the last
 *  entry = topmost (active) window. Hit-testing walks from the top down.
 * ============================================================ */
typedef struct {
    int32_t  id;
    int32_t  x, y;
    uint32_t w, h;
} wm_window_t;

#define WM_MAX_WINDOWS 16
static wm_window_t g_wm[WM_MAX_WINDOWS];
static int32_t g_wm_count;

static void wm_reset(void) { g_wm_count = 0; }

static int32_t wm_create(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (g_wm_count >= WM_MAX_WINDOWS) {
        return -1;
    }
    int32_t id = g_wm_count + 1;
    g_wm[g_wm_count].id = id;
    g_wm[g_wm_count].x = x; g_wm[g_wm_count].y = y;
    g_wm[g_wm_count].w = w; g_wm[g_wm_count].h = h;
    g_wm_count++;
    return id;
}

/* bring window to the top (end of the z-order array) */
static bool wm_raise(int32_t id)
{
    int32_t idx = -1;
    for (int32_t i = 0; i < g_wm_count; i++) {
        if (g_wm[i].id == id) { idx = i; break; }
    }
    if (idx < 0) {
        return false;
    }
    wm_window_t tmp = g_wm[idx];
    for (int32_t i = idx; i < g_wm_count - 1; i++) {
        g_wm[i] = g_wm[i + 1];
    }
    g_wm[g_wm_count - 1] = tmp;
    return true;
}

static bool wm_destroy(int32_t id)
{
    int32_t idx = -1;
    for (int32_t i = 0; i < g_wm_count; i++) {
        if (g_wm[i].id == id) { idx = i; break; }
    }
    if (idx < 0) {
        return false;
    }
    for (int32_t i = idx; i < g_wm_count - 1; i++) {
        g_wm[i] = g_wm[i + 1];
    }
    g_wm_count--;
    return true;
}

/* move/resize in place */
static bool wm_move_resize(int32_t id, int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    for (int32_t i = 0; i < g_wm_count; i++) {
        if (g_wm[i].id == id) {
            g_wm[i].x = x; g_wm[i].y = y; g_wm[i].w = w; g_wm[i].h = h;
            return true;
        }
    }
    return false;
}

/* topmost window containing (px,py), or -1 */
static int32_t wm_hit_test(int32_t px, int32_t py)
{
    for (int32_t i = g_wm_count - 1; i >= 0; i--) {
        if (px >= g_wm[i].x && px < g_wm[i].x + (int32_t) g_wm[i].w &&
            py >= g_wm[i].y && py < g_wm[i].y + (int32_t) g_wm[i].h) {
            return g_wm[i].id;
        }
    }
    return -1;
}

static void test_wm_create_destroy(void)
{
    section("window create / destroy");
    wm_reset();
    CHECK(wm_create(0, 0, 100, 100) == 1);
    CHECK(wm_create(10, 10, 200, 200) == 2);
    CHECK(g_wm_count == 2);
    CHECK(wm_destroy(1) == true);
    CHECK(g_wm_count == 1);
    CHECK(wm_destroy(1) == false);          /* already gone */
    CHECK(wm_destroy(999) == false);        /* never existed */
}

static void test_wm_move_resize(void)
{
    section("window move / resize");
    wm_reset();
    int32_t id = wm_create(0, 0, 50, 50);
    CHECK(wm_move_resize(id, 20, 30, 80, 90) == true);
    CHECK(g_wm[0].x == 20 && g_wm[0].y == 30);
    CHECK(g_wm[0].w == 80 && g_wm[0].h == 90);
    CHECK(wm_move_resize(42, 0, 0, 10, 10) == false);
}

static void test_wm_zorder(void)
{
    section("window z-order raise");
    wm_reset();
    int32_t a = wm_create(0, 0, 200, 200);       /* bottom */
    int32_t b = wm_create(50, 50, 200, 200);
    int32_t c = wm_create(100, 100, 200, 200);   /* top */
    (void) b;
    /* point (150,150) is inside all three -> topmost c */
    CHECK(wm_hit_test(150, 150) == c);
    /* raise a -> a becomes top */
    CHECK(wm_raise(a) == true);
    CHECK(wm_hit_test(150, 150) == a);
    /* raise a nonexistent fails */
    CHECK(wm_raise(999) == false);
}

static void test_wm_hit_test(void)
{
    section("window hit-testing");
    wm_reset();
    wm_create(0, 0, 100, 100);                    /* id 1 */
    wm_create(200, 200, 50, 50);                  /* id 2 */
    /* inside first window */
    CHECK(wm_hit_test(10, 10) == 1);
    /* inside second window */
    CHECK(wm_hit_test(220, 220) == 2);
    /* in the gap -> none */
    CHECK(wm_hit_test(150, 150) == -1);
    /* right/bottom edges are exclusive */
    CHECK(wm_hit_test(100, 50) == -1);
    CHECK(wm_hit_test(50, 100) == -1);
}

/* rectangle clipping: intersect [x,y,w,h] with clip rect; returns false
 * when there is no overlap (used to clip a child window to its parent). */
static bool rect_intersect(int32_t ax, int32_t ay, uint32_t aw, uint32_t ah,
                           int32_t bx, int32_t by, uint32_t bw, uint32_t bh,
                           int32_t *ox, int32_t *oy, uint32_t *ow, uint32_t *oh)
{
    int32_t x1 = ax > bx ? ax : bx;
    int32_t y1 = ay > by ? ay : by;
    int32_t x2a = ax + (int32_t) aw, y2a = ay + (int32_t) ah;
    int32_t x2b = bx + (int32_t) bw, y2b = by + (int32_t) bh;
    int32_t x2 = x2a < x2b ? x2a : x2b;
    int32_t y2 = y2a < y2b ? y2a : y2b;
    if (x2 <= x1 || y2 <= y1) {
        return false;
    }
    *ox = x1; *oy = y1; *ow = (uint32_t) (x2 - x1); *oh = (uint32_t) (y2 - y1);
    return true;
}

static void test_rect_clip(void)
{
    section("rectangle clipping / intersection");
    int32_t ox, oy; uint32_t ow, oh;

    /* partial overlap */
    CHECK(rect_intersect(0, 0, 100, 100, 50, 50, 100, 100, &ox, &oy, &ow, &oh) == true);
    CHECK(ox == 50 && oy == 50 && ow == 50 && oh == 50);

    /* contained */
    CHECK(rect_intersect(10, 10, 20, 20, 0, 0, 100, 100, &ox, &oy, &ow, &oh) == true);
    CHECK(ox == 10 && oy == 10 && ow == 20 && oh == 20);

    /* no overlap */
    CHECK(rect_intersect(0, 0, 10, 10, 100, 100, 10, 10, &ox, &oy, &ow, &oh) == false);

    /* edge-touching (zero-width intersection) counts as no overlap */
    CHECK(rect_intersect(0, 0, 10, 10, 10, 0, 10, 10, &ox, &oy, &ow, &oh) == false);
}

/* ============================================================ */
int main(void)
{
    printf("Monios GUI / window-manager host regression test\n");

    test_osui_register();
    test_osui_dedup();
    test_osui_custom_manifest();
    test_osui_init_seeds();

    test_wm_create_destroy();
    test_wm_move_resize();
    test_wm_zorder();
    test_wm_hit_test();
    test_rect_clip();

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
