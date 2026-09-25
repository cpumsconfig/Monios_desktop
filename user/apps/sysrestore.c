/*
 * sysrestore.c - Monios 系统还原（快照管理）。
 *
 * 功能：
 *   1. 创建系统快照：将 C: 盘完整备份到 D:\Monios\Backup\snapshot_YYYYMMDD_HHMMSS.img
 *   2. 快照列表：显示所有已创建的快照（日期/时间/大小/描述）
 *   3. 恢复快照：将系统分区恢复到快照状态（需确认）
 *   4. 删除快照：删除不需要的快照文件
 *
 * 底层使用 SYS_BACKUP_CTL(96) 原始分区备份/恢复接口。
 * 快照元数据保存在同名 .ini 文件中。
 *
 * 中文 GUI 界面：快照列表 + 创建/恢复/删除按钮 + 进度条。
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* SYS_BACKUP_CTL op codes（与 backup.c 一致） */
#define OP_BEGIN_BACKUP    1u
#define OP_READ_SRC        2u
#define OP_APPEND_IMG      3u
#define OP_END_BACKUP      4u
#define OP_BEGIN_RESTORE   5u
#define OP_READ_IMG        6u
#define OP_WRITE_SRC       7u
#define OP_END_RESTORE     8u

#define SR_MAX_SNAPSHOTS  16u
#define SR_NAME_LEN       64u
#define SR_DESC_LEN       128u
#define SR_BACKUP_DIR     "D:\\Monios\\Backup"
#define SR_CHUNK_SECTORS  256u
#define SR_BUF_SIZE       (SR_CHUNK_SECTORS * 512u)

typedef struct {
    char filename[SR_NAME_LEN];    /* snapshot_YYYYMMDD_HHMMSS.img */
    char desc[SR_DESC_LEN];        /* 用户描述 */
    uint32_t size_kb;              /* 镜像大小 KB */
    uint8_t exists;
} sr_snapshot_t;

/* 窗口布局 */
#define SR_WIN_X   40
#define SR_WIN_Y   30
#define SR_WIN_W   944
#define SR_WIN_H   680

#define SR_CANVAS  0x00EAF0F6
#define SR_TEXT    0x001C2930
#define SR_MUTED   0x005C6A70
#define SR_ACCENT  0x0000717F
#define SR_GREEN   0x002F8E5D
#define SR_ORANGE  0x00D08A2E
#define SR_DANGER  0x00C83E50
#define SR_PANEL   0x00FFFFFF
#define SR_ITEM_BG 0x00F3F6F7
#define SR_SEL_BG  0x00DCE6F5

static sr_snapshot_t g_snaps[SR_MAX_SNAPSHOTS];
static uint32_t g_snap_count;
static uint32_t g_selected;
static uint32_t g_busy;          /* 0=空闲, 1=创建中, 2=恢复中 */
static uint32_t g_progress;       /* 0-100 */
static char g_status_msg[160];
static char g_desc_input[SR_DESC_LEN];
static uint8_t g_buf[SR_BUF_SIZE];

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

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

/* 获取 RTC 时间戳用于文件名 */
static void get_timestamp(char *buf, uint32_t cap)
{
    uint8_t rtc[8];
    uint16_t year;
    uint8_t mon, day, hour, min, sec;
    char num[8];

    memset(rtc, 0, sizeof(rtc));
    syscall1(SYS_GET_RTC_TIME, (uint64_t)rtc);
    year = (uint16_t)(rtc[0] | (rtc[1] << 8));
    mon = rtc[2];
    day = rtc[3];
    hour = rtc[4];
    min = rtc[5];
    sec = rtc[6];

    buf[0] = '\0';
    fmt_u32(num, year); append_str(buf, num, cap);
    fmt_u32(num, mon);  append_str(buf, num, cap);
    fmt_u32(num, day);  append_str(buf, num, cap);
    append_str(buf, "_", cap);
    fmt_u32(num, hour); append_str(buf, num, cap);
    fmt_u32(num, min);  append_str(buf, num, cap);
    fmt_u32(num, sec);  append_str(buf, num, cap);
}

/* 扫描备份目录中的快照文件 */
static void scan_snapshots(void)
{
    char dirbuf[1024];
    char *p;
    uint32_t i = 0;

    g_snap_count = 0;
    memset(g_snaps, 0, sizeof(g_snaps));

    /* 确保备份目录存在 */
    app_file_mkdir(SR_BACKUP_DIR);

    memset(dirbuf, 0, sizeof(dirbuf));
    app_file_list_dir(SR_BACKUP_DIR, dirbuf, sizeof(dirbuf) - 1);

    p = dirbuf;
    while (*p && i < SR_MAX_SNAPSHOTS) {
        uint32_t len = (uint32_t)strlen(p);
        if (len > 4 && strstr(p, ".img") != 0) {
            sr_snapshot_t *s = &g_snaps[i];
            char fullpath[SR_NAME_LEN + 32];
            char inipath[SR_NAME_LEN + 32];
            char inibuf[128];

            strcpy(s->filename, p);
            s->exists = 1;

            /* 读取 .ini 描述 */
            sprintf(fullpath, "%s\\%s", SR_BACKUP_DIR, p);
            s->size_kb = (uint32_t)(app_file_size(fullpath) / 1024);

            /* 去掉 .img 后缀加 .ini */
            strcpy(inipath, fullpath);
            {
                char *dot = strstr(inipath, ".img");
                if (dot) strcpy(dot, ".ini");
            }
            memset(inibuf, 0, sizeof(inibuf));
            if (app_file_read(inipath, inibuf, sizeof(inibuf) - 1) > 0) {
                /* ini 格式: desc=描述文字 */
                char *dp = strstr(inibuf, "desc=");
                if (dp) {
                    dp += 5;
                    strncpy(s->desc, dp, SR_DESC_LEN - 1);
                    /* 去掉换行 */
                    {
                        char *nl = strchr(s->desc, '\n');
                        if (nl) *nl = '\0';
                    }
                }
            }
            if (s->desc[0] == '\0') {
                strcpy(s->desc, "(无描述)");
            }
            i++;
        }
        p += len + 1;
    }
    g_snap_count = i;
    if (g_selected >= g_snap_count && g_snap_count > 0) {
        g_selected = g_snap_count - 1;
    }
}

/* 创建快照（分块执行，每帧处理一个chunk以保持UI响应） */
static uint64_t g_create_lba;
static int64_t  g_create_total;
static char     g_create_path[SR_NAME_LEN + 32];
static uint8_t  g_create_started;

static void start_create(void)
{
    char ts[32];
    char inipath[SR_NAME_LEN + 32];
    char inibuf[160];

    if (g_busy) return;

    /* 请求 R2 权限 */
    if (!app_request_r2("创建系统快照需要原始磁盘访问权限")) {
        strcpy(g_status_msg, "权限被拒绝");
        return;
    }

    get_timestamp(ts, sizeof(ts));
    sprintf(g_create_path, "%s\\snapshot_%s.img", SR_BACKUP_DIR, ts);

    g_create_total = (int64_t)app_backup_ctl(OP_BEGIN_BACKUP,
                                             (uint64_t)g_create_path, 0, 0, 0);
    if (g_create_total <= 0) {
        strcpy(g_status_msg, "无法打开镜像或读取磁盘几何信息");
        return;
    }

    g_create_lba = 0;
    g_create_started = 1;
    g_busy = 1;
    g_progress = 0;
    strcpy(g_status_msg, "正在创建快照...");

    /* 写入 ini 元数据 */
    sprintf(inipath, "%s\\snapshot_%s.ini", SR_BACKUP_DIR, ts);
    sprintf(inibuf, "[snapshot]\ndesc=%s\n",
            g_desc_input[0] ? g_desc_input : "系统快照");
    app_file_write(inipath, inibuf, (uint32_t)strlen(inibuf));
}

static void tick_create(void)
{
    uint32_t sectors = SR_CHUNK_SECTORS;
    uint32_t rem;
    int64_t r, w;

    if (!g_create_started || g_busy != 1) return;
    if (g_create_lba >= (uint64_t)g_create_total) {
        /* 完成 */
        app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
        g_create_started = 0;
        g_busy = 0;
        g_progress = 100;
        strcpy(g_status_msg, "快照创建完成！");
        scan_snapshots();
        return;
    }

    rem = (uint32_t)((uint64_t)g_create_total - g_create_lba);
    if (sectors > rem) sectors = rem;

    r = (int64_t)app_backup_ctl(OP_READ_SRC, g_create_lba,
                                (uint64_t)sectors, (uint64_t)g_buf, 0);
    if (r != 0) {
        app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
        g_create_started = 0;
        g_busy = 0;
        strcpy(g_status_msg, "读取源磁盘失败");
        return;
    }
    w = (int64_t)app_backup_ctl(OP_APPEND_IMG, (uint64_t)g_buf,
                                (uint64_t)(sectors * 512u), 0, 0);
    if (w <= 0) {
        app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
        g_create_started = 0;
        g_busy = 0;
        strcpy(g_status_msg, "写入镜像失败（磁盘空间不足？）");
        return;
    }

    g_create_lba += sectors;
    g_progress = (uint32_t)(g_create_lba * 100ULL / (uint64_t)g_create_total);
}

/* 恢复快照 */
static uint64_t g_restore_offset;
static int64_t  g_restore_size;
static char     g_restore_path[SR_NAME_LEN + 32];
static uint8_t   g_restore_started;
static uint8_t   g_restore_confirm;

static void start_restore(void)
{
    if (g_busy || g_selected >= g_snap_count) return;
    if (!g_restore_confirm) {
        strcpy(g_status_msg, "警告：恢复将覆盖当前系统！再次点击确认恢复");
        g_restore_confirm = 1;
        return;
    }

    if (!app_request_r2("恢复系统快照需要原始磁盘写入权限")) {
        strcpy(g_status_msg, "权限被拒绝");
        return;
    }

    sprintf(g_restore_path, "%s\\%s", SR_BACKUP_DIR,
            g_snaps[g_selected].filename);

    g_restore_size = (int64_t)app_backup_ctl(OP_BEGIN_RESTORE,
                                             (uint64_t)g_restore_path, 0, 0, 0);
    if (g_restore_size <= 0) {
        strcpy(g_status_msg, "无法打开镜像文件");
        g_restore_confirm = 0;
        return;
    }

    g_restore_offset = 0;
    g_restore_started = 1;
    g_busy = 2;
    g_progress = 0;
    strcpy(g_status_msg, "正在恢复系统快照...");
}

static void tick_restore(void)
{
    uint32_t want = SR_BUF_SIZE;
    uint32_t rem;
    int64_t got, w;
    uint32_t sectors;

    if (!g_restore_started || g_busy != 2) return;
    if (g_restore_offset >= (uint64_t)g_restore_size) {
        app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
        g_restore_started = 0;
        g_busy = 0;
        g_progress = 100;
        g_restore_confirm = 0;
        strcpy(g_status_msg, "恢复完成！请重启系统。");
        return;
    }

    rem = (uint32_t)((uint64_t)g_restore_size - g_restore_offset);
    if (want > rem) want = rem;

    got = (int64_t)app_backup_ctl(OP_READ_IMG, (uint64_t)g_restore_path,
                                  g_restore_offset, (uint64_t)g_buf,
                                  (uint64_t)want);
    if (got <= 0) {
        app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
        g_restore_started = 0;
        g_busy = 0;
        g_restore_confirm = 0;
        strcpy(g_status_msg, "读取镜像失败");
        return;
    }

    sectors = (uint32_t)got / 512u;
    if (sectors > 0) {
        w = (int64_t)app_backup_ctl(OP_WRITE_SRC,
                                    g_restore_offset / 512u,
                                    (uint64_t)sectors,
                                    (uint64_t)g_buf, 0);
        if (w != 0) {
            app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
            g_restore_started = 0;
            g_busy = 0;
            g_restore_confirm = 0;
            strcpy(g_status_msg, "写入系统分区失败");
            return;
        }
    }
    g_restore_offset += (uint64_t)got;
    g_progress = (uint32_t)(g_restore_offset * 100ULL / (uint64_t)g_restore_size);
}

static void delete_selected(void)
{
    char fullpath[SR_NAME_LEN + 32];
    char inipath[SR_NAME_LEN + 32];

    if (g_busy || g_selected >= g_snap_count) return;

    sprintf(fullpath, "%s\\%s", SR_BACKUP_DIR,
            g_snaps[g_selected].filename);
    app_file_delete(fullpath);

    sprintf(inipath, "%s\\%s", SR_BACKUP_DIR,
            g_snaps[g_selected].filename);
    {
        char *dot = strstr(inipath, ".img");
        if (dot) strcpy(dot, ".ini");
    }
    app_file_delete(inipath);

    strcpy(g_status_msg, "快照已删除");
    g_restore_confirm = 0;
    scan_snapshots();
}

static void handle_click(int32_t mx, int32_t my)
{
    uint32_t i;

    /* 快照列表行 */
    for (i = 0; i < g_snap_count && i < 10; i++) {
        osui_rect_t row = {
            (uint16_t)(SR_WIN_X + 16),
            (uint16_t)(SR_WIN_Y + 90 + i * 32),
            (uint16_t)(SR_WIN_W - 32),
            28
        };
        if (in_rect(mx, my, row)) {
            g_selected = i;
            g_restore_confirm = 0;
            return;
        }
    }

    if (g_busy) return;

    /* 创建按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(SR_WIN_X + 16),
            (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
            140, 32 })) {
        start_create();
        return;
    }

    /* 恢复按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(SR_WIN_X + 170),
            (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
            140, 32 })) {
        start_restore();
        return;
    }

    /* 删除按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(SR_WIN_X + 324),
            (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
            140, 32 })) {
        delete_selected();
        return;
    }

    /* 刷新按钮 */
    if (in_rect(mx, my, (osui_rect_t){
            (uint16_t)(SR_WIN_X + 478),
            (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
            100, 32 })) {
        scan_snapshots();
        strcpy(g_status_msg, "已刷新快照列表");
        return;
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev_btn = 0;
    uint32_t i;

    app_enter_graphics_mode();
    memset(g_snaps, 0, sizeof(g_snaps));
    g_status_msg[0] = '\0';
    g_desc_input[0] = '\0';
    g_restore_confirm = 0;
    scan_snapshots();

    for (;;) {
        char line[128];
        char num[12];

        app_get_mouse(&mouse);
        if ((mouse.buttons & 1u) && !(prev_btn & 1u)) {
            handle_click(mouse.x_pixels, mouse.y_pixels);
        }
        if (mouse.buttons & 2u) return 0;
        prev_btn = mouse.buttons;

        /* 处理后台任务 */
        if (g_busy == 1) tick_create();
        else if (g_busy == 2) tick_restore();

        osui_canvas(SR_CANVAS);
        osui_panel((osui_rect_t){SR_WIN_X, SR_WIN_Y, SR_WIN_W, SR_WIN_H});
        osui_titlebar((osui_rect_t){SR_WIN_X, SR_WIN_Y, SR_WIN_W, 36},
                      "系统还原 - 快照管理", true);

        /* 快照列表标题 */
        app_graphics_draw_text((uint16_t)(SR_WIN_X + 16),
                               (uint16_t)(SR_WIN_Y + 52),
                               "快照列表", SR_ACCENT);
        app_graphics_draw_text((uint16_t)(SR_WIN_X + 200),
                               (uint16_t)(SR_WIN_Y + 52),
                               "大小", SR_MUTED);
        app_graphics_draw_text((uint16_t)(SR_WIN_X + 320),
                               (uint16_t)(SR_WIN_Y + 52),
                               "描述", SR_MUTED);
        osui_divider((uint16_t)(SR_WIN_X + 16),
                     (uint16_t)(SR_WIN_Y + 74),
                     (uint16_t)(SR_WIN_W - 32));

        /* 快照行 */
        for (i = 0; i < g_snap_count && i < 10; i++) {
            sr_snapshot_t *s = &g_snaps[i];
            uint16_t ry = (uint16_t)(SR_WIN_Y + 90 + i * 32);

            if (i == g_selected) {
                app_graphics_fill_rect((uint16_t)(SR_WIN_X + 12),
                                       ry, (uint16_t)(SR_WIN_W - 24), 28,
                                       SR_SEL_BG);
            }

            /* 文件名（去掉路径前缀） */
            {
                const char *fn = s->filename;
                const char *slash = strrchr(fn, '\\');
                if (slash) fn = slash + 1;
                app_graphics_draw_text((uint16_t)(SR_WIN_X + 20),
                                       (uint16_t)(ry + 6), fn, SR_TEXT);
            }

            /* 大小 */
            line[0] = '\0';
            fmt_u32(num, s->size_kb);
            append_str(line, num, sizeof(line));
            append_str(line, " KB", sizeof(line));
            app_graphics_draw_text((uint16_t)(SR_WIN_X + 200),
                                   (uint16_t)(ry + 6), line, SR_TEXT);

            /* 描述 */
            app_graphics_draw_text((uint16_t)(SR_WIN_X + 320),
                                   (uint16_t)(ry + 6), s->desc, SR_MUTED);
        }

        if (g_snap_count == 0) {
            app_graphics_draw_text((uint16_t)(SR_WIN_X + 200),
                                   (uint16_t)(SR_WIN_Y + 130),
                                   "(暂无快照，请点击\"创建快照\")",
                                   SR_MUTED);
        }

        /* 进度条 */
        if (g_busy) {
            osui_progress((osui_rect_t){
                              (uint16_t)(SR_WIN_X + 16),
                              (uint16_t)(SR_WIN_Y + SR_WIN_H - 150),
                              (uint16_t)(SR_WIN_W - 32), 20},
                          g_progress);
            line[0] = '\0';
            fmt_u32(num, g_progress);
            append_str(line, "%", sizeof(line));
            app_graphics_draw_text((uint16_t)(SR_WIN_X + SR_WIN_W - 60),
                                   (uint16_t)(SR_WIN_Y + SR_WIN_H - 148),
                                   line, SR_TEXT);
        }

        /* 操作按钮 */
        osui_button_state((osui_rect_t){
                              (uint16_t)(SR_WIN_X + 16),
                              (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
                              140, 32},
                          "创建快照", OSUI_BUTTON_PRIMARY,
                          g_busy ? OSUI_STATE_DISABLED : 0);
        osui_button_state((osui_rect_t){
                              (uint16_t)(SR_WIN_X + 170),
                              (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
                              140, 32},
                          "恢复快照", OSUI_BUTTON_DANGER,
                          g_busy ? OSUI_STATE_DISABLED : 0);
        osui_button_state((osui_rect_t){
                              (uint16_t)(SR_WIN_X + 324),
                              (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
                              140, 32},
                          "删除快照", OSUI_BUTTON_GHOST,
                          g_busy ? OSUI_STATE_DISABLED : 0);
        osui_button((osui_rect_t){
                        (uint16_t)(SR_WIN_X + 478),
                        (uint16_t)(SR_WIN_Y + SR_WIN_H - 100),
                        100, 32},
                    "刷新", OSUI_BUTTON_GHOST);

        /* 状态消息 */
        if (g_status_msg[0] != '\0') {
            uint32_t col = SR_ACCENT;
            if (g_busy == 2 || g_restore_confirm) col = SR_DANGER;
            if (g_progress == 100 && !g_busy) col = SR_GREEN;
            app_graphics_draw_text((uint16_t)(SR_WIN_X + 16),
                                   (uint16_t)(SR_WIN_Y + SR_WIN_H - 50),
                                   g_status_msg, col);
        }

        /* 统计 */
        line[0] = '\0';
        fmt_u32(num, g_snap_count);
        append_str(line, num, sizeof(line));
        append_str(line, " 个快照", sizeof(line));

        osui_statusbar((osui_rect_t){SR_WIN_X,
                                      (uint16_t)(SR_WIN_Y + SR_WIN_H - 28),
                                      SR_WIN_W, 28},
                       "sysrestore", line, OSUI_STATE_SUCCESS);
        osui_present();
        app_sleep_ticks(2);
    }
}
