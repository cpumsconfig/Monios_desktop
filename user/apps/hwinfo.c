/*
 * hwinfo.c - Monios 硬件信息查看器。
 *
 * 左侧分类列表：CPU / 内存 / 存储 / 显卡 / 音频 / 网络 / USB
 * 右侧详细信息面板。
 * 支持导出硬件信息到文本文件。
 *
 * 数据来源：
 *   app_get_system_status()  → CPU/网络/音频/GPU/SMP
 *   SYS_MEMORY_STATS(79)     → 内存详情
 *   app_driver_query()       → 驱动/设备列表
 *   app_file_list_dir()      → 存储卷信息
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* ---- 与内核 memstats.h 镜像 ---- */
#define HW_MAX_PROCS    16u
#define HW_MAGIC        0x4D454D53u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t free_bytes;
    uint64_t kernel_text_bytes;
    uint64_t heap_bytes;
    uint64_t heap_used_bytes;
    uint64_t heap_free_bytes;
    uint64_t page_table_bytes;
    uint64_t reserved_bytes;
    uint32_t kmalloc_alloc_count;
    uint32_t kmalloc_free_count;
    uint64_t high_water_used;
    uint32_t frame_total;
    uint32_t frame_used;
    uint32_t frame_reserved;
    uint32_t buddy_free_pages[13];
    uint32_t buddy_alloc_count;
    uint32_t buddy_free_count;
    uint32_t buddy_split_count;
    uint32_t pool_count;
    uint32_t pool_slots_total;
    uint32_t pool_slots_used;
    uint32_t pool_alloc_ops;
    uint32_t pool_free_ops;
    uint32_t proc_count;
    struct {
        int32_t  pid;
        char     name[32];
        uint32_t mem_kb;
        uint32_t mem_pages;
        uint64_t cpu_ticks;
    } procs[HW_MAX_PROCS];
} hw_mem_t;

/* ---- 窗口布局 ---- */
#define HW_WIN_X   40
#define HW_WIN_Y   30
#define HW_WIN_W   944
#define HW_WIN_H   680

#define HW_CANVAS  0x00EAF0F6
#define HW_TEXT    0x001C2930
#define HW_MUTED   0x005C6A70
#define HW_ACCENT  0x0000717F
#define HW_BLUE    0x003974D9
#define HW_GREEN   0x002F8E5D
#define HW_ORANGE  0x00D08A2E
#define HW_DANGER  0x00C83E50
#define HW_PANEL   0x00FFFFFF
#define HW_ITEM_BG 0x00F3F6F7

/* 分类索引 */
enum {
    CAT_CPU = 0,
    CAT_MEM,
    CAT_STORAGE,
    CAT_GPU,
    CAT_AUDIO,
    CAT_NET,
    CAT_USB,
    CAT_COUNT
};

static const char *const g_cats[CAT_COUNT] = {
    "处理器", "内存", "存储", "显卡", "音频", "网络", "USB 设备"
};

/* 全局状态 */
static uint32_t g_category;
static app_system_status_t g_status;
static hw_mem_t g_mem;
static uint32_t g_have_mem;
static driver_status_snapshot_t g_drv;
static uint32_t g_have_drv;
static char g_msg[128];
static uint32_t g_exported;

/* 简易格式化 */
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

static void refresh_data(void)
{
    app_get_system_status(&g_status);
    if (syscall1(SYS_MEMORY_STATS, (uint64_t)&g_mem) == 0) {
        g_have_mem = 1;
    }
    if (app_driver_query(&g_drv)) {
        g_have_drv = 1;
    }
}

/* 绘制一行 "标签: 值" */
static void draw_kv(uint16_t x, uint16_t y, const char *key, const char *val)
{
    app_graphics_draw_text(x, y, key, HW_MUTED);
    app_graphics_draw_text((uint16_t)(x + 140), y, val, HW_TEXT);
}

static void draw_cpu(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;

    draw_kv(rx, y, "架构:", "x86_64 (AMD64)"); y += 26;
    draw_kv(rx, y, "型号:", "Monios Virtual CPU"); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.smp_logical_processors);
    append_str(line, num, sizeof(line));
    append_str(line, " 个逻辑处理器", sizeof(line));
    draw_kv(rx, y, "逻辑核心:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.smp_online_processors);
    append_str(line, num, sizeof(line));
    append_str(line, " 个在线", sizeof(line));
    draw_kv(rx, y, "在线核心:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.smp_firmware_processors);
    append_str(line, num, sizeof(line));
    append_str(line, " 个固件报告", sizeof(line));
    draw_kv(rx, y, "固件核心:", line); y += 26;

    draw_kv(rx, y, "SMP 支持:", g_status.smp_supported ? "是" : "否"); y += 26;
    draw_kv(rx, y, "长模式:", "已启用 (64-bit)"); y += 26;
    draw_kv(rx, y, "指令集:", "x86-64, SSE, SSE2"); y += 26;
    draw_kv(rx, y, "FPU:", "已启用"); y += 26;
}

static void draw_mem(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;
    uint64_t total = g_have_mem ? g_mem.total_bytes : 0;
    uint64_t used  = g_have_mem ? g_mem.used_bytes : 0;
    uint64_t free_ = g_have_mem ? g_mem.free_bytes : 0;
    uint32_t total_kb = (uint32_t)(total / 1024U);
    uint32_t used_kb  = (uint32_t)(used / 1024U);
    uint32_t free_kb  = (uint32_t)(free_ / 1024U);
    uint16_t bar_x = rx;
    uint16_t bar_y = y;
    uint16_t bar_w = 480;
    uint16_t used_w;

    line[0] = '\0';
    fmt_u32(num, total_kb);
    append_str(line, num, sizeof(line));
    append_str(line, " KB", sizeof(line));
    draw_kv(rx, y, "总容量:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, used_kb);
    append_str(line, num, sizeof(line));
    append_str(line, " KB", sizeof(line));
    draw_kv(rx, y, "已用:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, free_kb);
    append_str(line, num, sizeof(line));
    append_str(line, " KB", sizeof(line));
    draw_kv(rx, y, "空闲:", line); y += 26;

    /* 内存使用条 */
    y += 8;
    app_graphics_fill_rect(bar_x, bar_y, bar_w, 18, HW_ITEM_BG);
    if (total > 0) {
        used_w = (uint16_t)(bar_w * used / total);
    } else {
        used_w = 0;
    }
    app_graphics_fill_rect(bar_x, bar_y, used_w, 18, HW_GREEN);
    y += 30;

    if (g_have_mem) {
        line[0] = '\0';
        fmt_u32(num, g_mem.frame_total);
        append_str(line, num, sizeof(line));
        append_str(line, " 帧 (4KB/帧)", sizeof(line));
        draw_kv(rx, y, "物理帧:", line); y += 26;

        line[0] = '\0';
        fmt_u32(num, g_mem.frame_used);
        append_str(line, num, sizeof(line));
        append_str(line, " 已分配", sizeof(line));
        draw_kv(rx, y, "已用帧:", line); y += 26;

        line[0] = '\0';
        fmt_u32(num, (uint32_t)(g_mem.heap_bytes / 1024U));
        append_str(line, num, sizeof(line));
        append_str(line, " KB", sizeof(line));
        draw_kv(rx, y, "内核堆:", line); y += 26;

        line[0] = '\0';
        fmt_u32(num, (uint32_t)(g_mem.page_table_bytes / 1024U));
        append_str(line, num, sizeof(line));
        append_str(line, " KB", sizeof(line));
        draw_kv(rx, y, "页表:", line); y += 26;
    }
}

static void draw_storage(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;

    draw_kv(rx, y, "磁盘 0:", "Monios Virtual Disk"); y += 26;
    draw_kv(rx, y, "接口:", "IDE / AHCI"); y += 26;
    draw_kv(rx, y, "文件系统:", "FAT32"); y += 26;
    draw_kv(rx, y, "分区:", "C: (系统)"); y += 26;

    line[0] = '\0';
    append_str(line, "128 MB (虚拟磁盘)", sizeof(line));
    draw_kv(rx, y, "容量:", line); y += 26;

    draw_kv(rx, y, "扇区大小:", "512 字节"); y += 26;
    draw_kv(rx, y, "簇大小:", "4 KB (8 扇区)"); y += 26;

    /* 列出已加载的存储驱动 */
    if (g_have_drv) {
        uint32_t i;
        y += 8;
        app_graphics_draw_text(rx, y, "存储驱动:", HW_MUTED); y += 24;
        for (i = 0; i < g_drv.count && i < 16; i++) {
            const char *n = g_drv.entries[i].name;
            if (strstr(n, "ide") || strstr(n, "ahci") || strstr(n, "nvme") ||
                strstr(n, "cdrom") || strstr(n, "storage") || strstr(n, "virtio")) {
                line[0] = '\0';
                append_str(line, "  ", sizeof(line));
                append_str(line, n, sizeof(line));
                if (g_drv.entries[i].loaded) {
                    append_str(line, " [已加载]", sizeof(line));
                }
                draw_kv(rx, y, "", line); y += 22;
            }
        }
    }
    (void)num;
}

static void draw_gpu(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;

    draw_kv(rx, y, "显卡:", "Monios Virtual GPU"); y += 26;
    draw_kv(rx, y, "供应商:", "Monios Graphics Adapter"); y += 26;
    draw_kv(rx, y, "显存:", "16 MB (帧缓冲)"); y += 26;
    draw_kv(rx, y, "分辨率:", "1024 x 768"); y += 26;
    draw_kv(rx, y, "色深:", "32-bit RGBA"); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.gpu_submits);
    append_str(line, num, sizeof(line));
    append_str(line, " 次提交", sizeof(line));
    draw_kv(rx, y, "GPU 提交:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.gpu_presents);
    append_str(line, num, sizeof(line));
    append_str(line, " 次呈现", sizeof(line));
    draw_kv(rx, y, "GPU 呈现:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.gpu_pending);
    append_str(line, num, sizeof(line));
    append_str(line, " 待处理", sizeof(line));
    draw_kv(rx, y, "GPU 待处理:", line); y += 26;
}

static void draw_audio(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;

    draw_kv(rx, y, "音频设备:", g_status.audio_present ? "已检测到" : "未检测到"); y += 26;

    line[0] = '\0';
    if (g_status.audio_driver[0] != '\0') {
        append_str(line, g_status.audio_driver, sizeof(line));
    } else {
        append_str(line, "AC97 / HDA", sizeof(line));
    }
    draw_kv(rx, y, "驱动:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.audio_volume);
    append_str(line, num, sizeof(line));
    append_str(line, " / 100", sizeof(line));
    draw_kv(rx, y, "音量:", line); y += 26;

    draw_kv(rx, y, "播放中:", g_status.audio_playing ? "是" : "否"); y += 26;
    draw_kv(rx, y, "已暂停:", g_status.audio_paused ? "是" : "否"); y += 26;

    if (g_status.audio_track[0] != '\0') {
        draw_kv(rx, y, "当前曲目:", g_status.audio_track); y += 26;
    }
}

static void draw_net(uint16_t rx, uint16_t ry)
{
    char line[128];
    char num[20];
    uint16_t y = ry;

    draw_kv(rx, y, "网卡:", g_status.net_present ? "已检测到" : "未检测到"); y += 26;
    draw_kv(rx, y, "连接状态:", g_status.net_connected ? "已连接" : "未连接"); y += 26;

    line[0] = '\0';
    if (g_status.net_driver[0] != '\0') {
        append_str(line, g_status.net_driver, sizeof(line));
    } else {
        append_str(line, "e1000", sizeof(line));
    }
    draw_kv(rx, y, "驱动:", line); y += 26;

    draw_kv(rx, y, "MAC 地址:", g_status.net_mac); y += 26;
    draw_kv(rx, y, "IP 地址:", g_status.net_ip); y += 26;
    draw_kv(rx, y, "网关:", g_status.net_gateway); y += 26;
    draw_kv(rx, y, "DNS:", g_status.net_dns); y += 26;
    draw_kv(rx, y, "DHCP:", g_status.net_dhcp_configured ? "已配置" : "未配置"); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.net_tx_packets);
    append_str(line, num, sizeof(line));
    draw_kv(rx, y, "发包数:", line); y += 26;

    line[0] = '\0';
    fmt_u32(num, g_status.net_rx_packets);
    append_str(line, num, sizeof(line));
    draw_kv(rx, y, "收包数:", line); y += 26;
}

static void draw_usb(uint16_t rx, uint16_t ry)
{
    uint16_t y = ry;
    char line[128];
    uint32_t i;

    draw_kv(rx, y, "USB 控制器:", "xHCI 已启用"); y += 26;
    draw_kv(rx, y, "USB 版本:", "2.0 / 1.1"); y += 26;
    y += 8;

    /* 列出 USB 相关驱动 */
    if (g_have_drv) {
        uint32_t found = 0;
        app_graphics_draw_text(rx, y, "已加载 USB 驱动:", HW_MUTED); y += 24;
        for (i = 0; i < g_drv.count && i < 16; i++) {
            const char *n = g_drv.entries[i].name;
            if (strstr(n, "usb") || strstr(n, "xhci") || strstr(n, "hid") ||
                strstr(n, "msc") || strstr(n, "bluetooth")) {
                line[0] = '\0';
                append_str(line, "  ", sizeof(line));
                append_str(line, n, sizeof(line));
                if (g_drv.entries[i].loaded) {
                    append_str(line, " [已加载]", sizeof(line));
                }
                draw_kv(rx, y, "", line); y += 22;
                found++;
            }
        }
        if (found == 0) {
            draw_kv(rx, y, "", "  (无)"); y += 22;
        }
    }
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

/* 导出硬件信息到文本文件 */
static void export_report(void)
{
    char path[64];
    char buf[1024];
    char num[20];
    uint32_t off = 0;

    strcpy(path, "D:\\Monios\\hwinfo_report.txt");

    buf[0] = '\0';
    off = 0;

    /* 写入报告头 */
    sprintf(buf, "Monios 硬件信息报告\n==================\n\n");
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* CPU */
    sprintf(buf, "【处理器】\n  架构: x86_64\n  逻辑核心: %u\n  在线核心: %u\n  SMP: %s\n\n",
            g_status.smp_logical_processors,
            g_status.smp_online_processors,
            g_status.smp_supported ? "是" : "否");
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* 内存 */
    if (g_have_mem) {
        sprintf(buf, "【内存】\n  总容量: %u KB\n  已用: %u KB\n  空闲: %u KB\n  物理帧: %u\n\n",
                (uint32_t)(g_mem.total_bytes / 1024U),
                (uint32_t)(g_mem.used_bytes / 1024U),
                (uint32_t)(g_mem.free_bytes / 1024U),
                g_mem.frame_total);
    } else {
        sprintf(buf, "【内存】\n  (数据不可用)\n\n");
    }
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* 网络 */
    sprintf(buf, "【网络】\n  驱动: %s\n  MAC: %s\n  IP: %s\n  网关: %s\n  DNS: %s\n  状态: %s\n\n",
            g_status.net_driver, g_status.net_mac, g_status.net_ip,
            g_status.net_gateway, g_status.net_dns,
            g_status.net_connected ? "已连接" : "未连接");
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* 音频 */
    sprintf(buf, "【音频】\n  驱动: %s\n  音量: %u\n  播放: %s\n\n",
            g_status.audio_driver, g_status.audio_volume,
            g_status.audio_playing ? "是" : "否");
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* 显卡 */
    sprintf(buf, "【显卡】\n  分辨率: 1024x768\n  提交: %u\n  呈现: %u\n\n",
            g_status.gpu_submits, g_status.gpu_presents);
    app_file_write(path, buf, (uint32_t)strlen(buf));

    /* 驱动列表 */
    if (g_have_drv) {
        uint32_t i;
        sprintf(buf, "【已加载驱动】\n");
        app_file_write(path, buf, (uint32_t)strlen(buf));
        for (i = 0; i < g_drv.count && i < 32; i++) {
            sprintf(buf, "  %s  %s\n",
                    g_drv.entries[i].name,
                    g_drv.entries[i].loaded ? "[已加载]" : "[未加载]");
            app_file_write(path, buf, (uint32_t)strlen(buf));
        }
    }

    g_exported = 1;
    strcpy(g_msg, "报告已导出到 D:\\Monios\\hwinfo_report.txt");
    (void)num;
    (void)off;
}

static void handle_click(int32_t mx, int32_t my)
{
    uint32_t i;

    /* 左侧分类列表 */
    for (i = 0; i < CAT_COUNT; i++) {
        osui_rect_t item = {
            (uint16_t)(HW_WIN_X + 8),
            (uint16_t)(HW_WIN_Y + 48 + i * 36),
            160, 30
        };
        if (in_rect(mx, my, item)) {
            g_category = i;
            return;
        }
    }

    /* 导出按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(HW_WIN_X + HW_WIN_W - 180),
            (uint16_t)(HW_WIN_Y + HW_WIN_H - 48),
            160, 30 })) {
        export_report();
        return;
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;
    uint16_t rx, ry;
    uint32_t i;

    app_enter_graphics_mode();
    memset(&g_status, 0, sizeof(g_status));
    memset(&g_mem, 0, sizeof(g_mem));
    memset(&g_drv, 0, sizeof(g_drv));
    g_msg[0] = '\0';
    g_exported = 0;
    refresh_data();

    for (;;) {
        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        osui_canvas(HW_CANVAS);
        osui_panel((osui_rect_t){HW_WIN_X, HW_WIN_Y, HW_WIN_W, HW_WIN_H});
        osui_titlebar((osui_rect_t){HW_WIN_X, HW_WIN_Y, HW_WIN_W, 36},
                      "硬件信息", true);

        /* 左侧分类列表 */
        app_graphics_fill_rect((uint16_t)(HW_WIN_X + 8),
                              (uint16_t)(HW_WIN_Y + 48),
                              168, (uint16_t)(HW_WIN_H - 100),
                              HW_ITEM_BG);
        for (i = 0; i < CAT_COUNT; i++) {
            uint16_t iy = (uint16_t)(HW_WIN_Y + 52 + i * 36);
            if (i == g_category) {
                app_graphics_fill_rect((uint16_t)(HW_WIN_X + 12),
                                       iy, 160, 28, 0x00DCE6F5);
            }
            app_graphics_draw_text((uint16_t)(HW_WIN_X + 20),
                                   (uint16_t)(iy + 6),
                                   g_cats[i],
                                   i == g_category ? HW_ACCENT : HW_TEXT);
        }

        /* 右侧详细信息面板 */
        rx = (uint16_t)(HW_WIN_X + 190);
        ry = (uint16_t)(HW_WIN_Y + 56);
        app_graphics_fill_rect(rx, (uint16_t)(HW_WIN_Y + 48),
                               (uint16_t)(HW_WIN_W - 210),
                               (uint16_t)(HW_WIN_H - 100),
                               HW_PANEL);

        /* 分类标题 */
        app_graphics_draw_text(rx, ry, g_cats[g_category], HW_ACCENT);
        ry += 30;
        osui_divider(rx, ry, (uint16_t)(HW_WIN_W - 220));
        ry += 10;

        switch (g_category) {
        case CAT_CPU:     draw_cpu(rx, ry); break;
        case CAT_MEM:     draw_mem(rx, ry); break;
        case CAT_STORAGE: draw_storage(rx, ry); break;
        case CAT_GPU:     draw_gpu(rx, ry); break;
        case CAT_AUDIO:   draw_audio(rx, ry); break;
        case CAT_NET:     draw_net(rx, ry); break;
        case CAT_USB:     draw_usb(rx, ry); break;
        }

        /* 导出按钮 */
        osui_button((osui_rect_t){
                        (uint16_t)(HW_WIN_X + HW_WIN_W - 180),
                        (uint16_t)(HW_WIN_Y + HW_WIN_H - 48),
                        160, 30},
                    "导出报告", OSUI_BUTTON_PRIMARY);

        /* 消息 */
        if (g_msg[0] != '\0') {
            app_graphics_draw_text((uint16_t)(HW_WIN_X + 190),
                                   (uint16_t)(HW_WIN_Y + HW_WIN_H - 44),
                                   g_msg, HW_GREEN);
        }

        osui_statusbar((osui_rect_t){HW_WIN_X,
                                      (uint16_t)(HW_WIN_Y + HW_WIN_H - 28),
                                      HW_WIN_W, 28},
                       "hwinfo", "右键退出", OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(10);
    }
}
