/*
 * drvmgr.c - Monios 驱动管理器。
 *
 * 左侧：驱动分类（存储/网络/输入/音频/显卡/USB/其他）
 * 右侧：驱动列表（名称/状态/标志）
 * 底部：详细信息面板 + 加载/卸载/刷新按钮
 *
 * 数据来源：
 *   app_driver_query()      → 枚举已注册驱动
 *   app_driver_load()      → 加载 .sys 文件
 *   app_driver_unload()    → 卸载驱动
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

#define DRV_WIN_X   40
#define DRV_WIN_Y   30
#define DRV_WIN_W   944
#define DRV_WIN_H   680

#define DRV_CANVAS  0x00EAF0F6
#define DRV_TEXT    0x001C2930
#define DRV_MUTED   0x005C6A70
#define DRV_ACCENT  0x0000717F
#define DRV_GREEN   0x002F8E5D
#define DRV_ORANGE  0x00D08A2E
#define DRV_DANGER  0x00C83E50
#define DRV_PANEL   0x00FFFFFF
#define DRV_ITEM_BG 0x00F3F6F7
#define DRV_SEL_BG  0x00DCE6F5

/* 分类 */
enum {
    DCAT_ALL = 0,
    DCAT_STORAGE,
    DCAT_NET,
    DCAT_INPUT,
    DCAT_AUDIO,
    DCAT_GPU,
    DCAT_USB,
    DCAT_OTHER,
    DCAT_COUNT
};

static const char *const g_dcats[DCAT_COUNT] = {
    "全部", "存储", "网络", "输入", "音频", "显卡", "USB", "其他"
};

static uint32_t g_category;
static uint32_t g_selected;
static driver_status_snapshot_t g_drv;
static uint32_t g_have_drv;
static char g_msg[128];

/* 简易数字格式化 */
static void fmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    int i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    while (v > 0) { tmp[i++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

static void append_str(char *out, const char *s, uint32_t cap)
{
    uint32_t l = (uint32_t)strlen(out);
    while (*s && l + 1 < cap) out[l++] = *s++;
    out[l] = '\0';
}

/* 判断驱动属于哪个分类 */
static uint32_t classify_driver(const char *name)
{
    if (strstr(name, "ide") || strstr(name, "ahci") || strstr(name, "nvme") ||
        strstr(name, "cdrom") || strstr(name, "storage") || strstr(name, "virtio_blk"))
        return DCAT_STORAGE;
    if (strstr(name, "e1000") || strstr(name, "pcnet") || strstr(name, "rtl") ||
        strstr(name, "virtio_net") || strstr(name, "net"))
        return DCAT_NET;
    if (strstr(name, "hid") || strstr(name, "keyboard") || strstr(name, "mouse"))
        return DCAT_INPUT;
    if (strstr(name, "audio") || strstr(name, "aac") || strstr(name, "hda") ||
        strstr(name, "es1371") || strstr(name, "ac97"))
        return DCAT_AUDIO;
    if (strstr(name, "gpu") || strstr(name, "opengl") || strstr(name, "vulkan") ||
        strstr(name, "nvidia") || strstr(name, "igpu"))
        return DCAT_GPU;
    if (strstr(name, "usb") || strstr(name, "xhci") || strstr(name, "hid") ||
        strstr(name, "msc") || strstr(name, "bluetooth"))
        return DCAT_USB;
    return DCAT_OTHER;
}

static uint32_t drv_visible_count(void)
{
    uint32_t i, n = 0;
    if (!g_have_drv) return 0;
    for (i = 0; i < g_drv.count && i < DRIVER_STATUS_MAX; i++) {
        if (g_category == DCAT_ALL ||
            classify_driver(g_drv.entries[i].name) == g_category) {
            n++;
        }
    }
    return n;
}

static uint32_t drv_index_to_visible(uint32_t idx)
{
    uint32_t i, n = 0;
    for (i = 0; i < g_drv.count && i < DRIVER_STATUS_MAX; i++) {
        if (g_category == DCAT_ALL ||
            classify_driver(g_drv.entries[i].name) == g_category) {
            if (n == idx) return i;
            n++;
        }
    }
    return 0;
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

static void refresh_drivers(void)
{
    if (app_driver_query(&g_drv)) {
        g_have_drv = 1;
    }
    if (g_selected >= g_drv.count) {
        g_selected = 0;
    }
}

static void handle_click(int32_t mx, int32_t my)
{
    uint32_t i;
    uint32_t vis_count = drv_visible_count();

    /* 左侧分类 */
    for (i = 0; i < DCAT_COUNT; i++) {
        osui_rect_t item = {
            (uint16_t)(DRV_WIN_X + 8),
            (uint16_t)(DRV_WIN_Y + 48 + i * 34),
            140, 28
        };
        if (in_rect(mx, my, item)) {
            g_category = i;
            g_selected = 0;
            return;
        }
    }

    /* 右侧驱动列表 */
    for (i = 0; i < vis_count && i < 18; i++) {
        osui_rect_t row = {
            (uint16_t)(DRV_WIN_X + 160),
            (uint16_t)(DRV_WIN_Y + 52 + i * 26),
            500, 22
        };
        if (in_rect(mx, my, row)) {
            g_selected = drv_index_to_visible(i);
            return;
        }
    }

    /* 刷新按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(DRV_WIN_X + 160),
            (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
            100, 28 })) {
        refresh_drivers();
        strcpy(g_msg, "已刷新驱动列表");
        return;
    }

    /* 加载按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(DRV_WIN_X + 280),
            (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
            100, 28 })) {
        /* 尝试加载一个示例驱动 */
        if (app_driver_load("\\Monios\\driver\\e1000.sys")) {
            strcpy(g_msg, "驱动加载成功");
        } else {
            strcpy(g_msg, "驱动加载失败（文件可能不存在）");
        }
        refresh_drivers();
        return;
    }

    /* 卸载按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(DRV_WIN_X + 400),
            (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
            100, 28 })) {
        if (g_have_drv && g_selected < g_drv.count) {
            const char *name = g_drv.entries[g_selected].name;
            if (app_driver_unload(name, false)) {
                strcpy(g_msg, "驱动已卸载");
            } else {
                strcpy(g_msg, "卸载失败（驱动可能为关键驱动）");
            }
            refresh_drivers();
        }
        return;
    }
}

static void draw_flags(uint16_t x, uint16_t y, uint32_t flags)
{
    char line[64];
    line[0] = '\0';
    if (flags & DRIVER_STATUS_FLAG_EXTERNAL)  append_str(line, "外部 ", sizeof(line));
    if (flags & DRIVER_STATUS_FLAG_VERIFIED)  append_str(line, "已验证 ", sizeof(line));
    if (flags & DRIVER_STATUS_FLAG_CRITICAL)  append_str(line, "关键 ", sizeof(line));
    if (flags & DRIVER_STATUS_FLAG_UNLOADABLE) append_str(line, "可卸载 ", sizeof(line));
    if (flags & DRIVER_STATUS_FLAG_DEGRADED)  append_str(line, "降级 ", sizeof(line));
    if (line[0] == '\0') append_str(line, "无", sizeof(line));
    app_graphics_draw_text(x, y, line, DRV_MUTED);
}

static void draw_kv(uint16_t x, uint16_t y, const char *key, const char *val)
{
    app_graphics_draw_text(x, y, key, DRV_MUTED);
    app_graphics_draw_text((uint16_t)(x + 100), y, val, DRV_TEXT);
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;
    uint32_t i;
    uint16_t ly;
    uint32_t vis_count;

    app_enter_graphics_mode();
    memset(&g_drv, 0, sizeof(g_drv));
    g_msg[0] = '\0';
    refresh_drivers();

    for (;;) {
        char line[96];
        char num[12];

        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        vis_count = drv_visible_count();

        osui_canvas(DRV_CANVAS);
        osui_panel((osui_rect_t){DRV_WIN_X, DRV_WIN_Y, DRV_WIN_W, DRV_WIN_H});
        osui_titlebar((osui_rect_t){DRV_WIN_X, DRV_WIN_Y, DRV_WIN_W, 36},
                      "驱动管理器", true);

        /* 左侧分类 */
        app_graphics_fill_rect((uint16_t)(DRV_WIN_X + 8),
                              (uint16_t)(DRV_WIN_Y + 48),
                              150, (uint16_t)(DRV_WIN_H - 100),
                              DRV_ITEM_BG);
        for (i = 0; i < DCAT_COUNT; i++) {
            uint16_t iy = (uint16_t)(DRV_WIN_Y + 52 + i * 34);
            if (i == g_category) {
                app_graphics_fill_rect((uint16_t)(DRV_WIN_X + 12),
                                       iy, 142, 26, DRV_SEL_BG);
            }
            app_graphics_draw_text((uint16_t)(DRV_WIN_X + 20),
                                   (uint16_t)(iy + 5),
                                   g_dcats[i],
                                   i == g_category ? DRV_ACCENT : DRV_TEXT);
        }

        /* 右侧驱动列表标题 */
        ly = (uint16_t)(DRV_WIN_Y + 52);
        app_graphics_draw_text((uint16_t)(DRV_WIN_X + 168), ly,
                               "驱动名称", DRV_MUTED);
        app_graphics_draw_text((uint16_t)(DRV_WIN_X + 380), ly,
                               "状态", DRV_MUTED);
        app_graphics_draw_text((uint16_t)(DRV_WIN_X + 480), ly,
                               "标志", DRV_MUTED);
        ly += 24;
        osui_divider((uint16_t)(DRV_WIN_X + 160), ly, 520);
        ly += 6;

        /* 驱动行 */
        for (i = 0; i < vis_count && i < 18; i++) {
            uint32_t di = drv_index_to_visible(i);
            driver_status_entry_t *e = &g_drv.entries[di];
            uint16_t row_y = (uint16_t)(DRV_WIN_Y + 82 + i * 26);

            if (di == g_selected) {
                app_graphics_fill_rect((uint16_t)(DRV_WIN_X + 160),
                                       row_y, 520, 22, DRV_SEL_BG);
            }

            app_graphics_draw_text((uint16_t)(DRV_WIN_X + 168),
                                   (uint16_t)(row_y + 3),
                                   e->name, DRV_TEXT);

            app_graphics_draw_text((uint16_t)(DRV_WIN_X + 380),
                                   (uint16_t)(row_y + 3),
                                   e->loaded ? "已加载" : "未加载",
                                   e->loaded ? DRV_GREEN : DRV_MUTED);

            draw_flags((uint16_t)(DRV_WIN_X + 480),
                       (uint16_t)(row_y + 3), e->flags);
        }

        /* 底部详细信息面板 */
        {
            uint16_t dy = (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 150);
            app_graphics_fill_rect((uint16_t)(DRV_WIN_X + 8),
                                   dy,
                                   (uint16_t)(DRV_WIN_W - 16),
                                   60, DRV_ITEM_BG);
            if (g_have_drv && g_selected < g_drv.count) {
                driver_status_entry_t *e = &g_drv.entries[g_selected];
                draw_kv((uint16_t)(DRV_WIN_X + 16), (uint16_t)(dy + 6),
                        "名称:", e->name);

                line[0] = '\0';
                fmt_u32(num, e->priority);
                append_str(line, num, sizeof(line));
                draw_kv((uint16_t)(DRV_WIN_X + 16), (uint16_t)(dy + 28),
                        "优先级:", line);

                line[0] = '\0';
                fmt_u32(num, (uint32_t)e->adapter_score);
                append_str(line, num, sizeof(line));
                draw_kv((uint16_t)(DRV_WIN_X + 200), (uint16_t)(dy + 6),
                        "适配评分:", line);

                line[0] = '\0';
                append_str(line, e->loaded ? "已加载" : "未加载", sizeof(line));
                draw_kv((uint16_t)(DRV_WIN_X + 200), (uint16_t)(dy + 28),
                        "状态:", line);
            } else {
                app_graphics_draw_text((uint16_t)(DRV_WIN_X + 16),
                                       (uint16_t)(dy + 20),
                                       "(未选中驱动)", DRV_MUTED);
            }
        }

        /* 操作按钮 */
        osui_button((osui_rect_t){
                        (uint16_t)(DRV_WIN_X + 160),
                        (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
                        100, 28},
                    "刷新", OSUI_BUTTON_PRIMARY);
        osui_button((osui_rect_t){
                        (uint16_t)(DRV_WIN_X + 280),
                        (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
                        100, 28},
                    "加载驱动", OSUI_BUTTON_PRIMARY);
        osui_button((osui_rect_t){
                        (uint16_t)(DRV_WIN_X + 400),
                        (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 90),
                        100, 28},
                    "卸载", OSUI_BUTTON_DANGER);

        /* 消息 */
        if (g_msg[0] != '\0') {
            app_graphics_draw_text((uint16_t)(DRV_WIN_X + 520),
                                   (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 85),
                                   g_msg, DRV_ACCENT);
        }

        /* 统计 */
        line[0] = '\0';
        fmt_u32(num, g_have_drv ? g_drv.loaded_count : 0);
        append_str(line, num, sizeof(line));
        append_str(line, " / ", sizeof(line));
        fmt_u32(num, g_have_drv ? g_drv.count : 0);
        append_str(line, num, sizeof(line));
        append_str(line, " 已加载", sizeof(line));

        osui_statusbar((osui_rect_t){DRV_WIN_X,
                                      (uint16_t)(DRV_WIN_Y + DRV_WIN_H - 28),
                                      DRV_WIN_W, 28},
                       "drvmgr", line, OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
