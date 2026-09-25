/* ============================================================
 * firewall.c - Monios 图形化防火墙配置工具
 *
 * 通过 SYS_FIREWALL_CTL (62) 与内核防火墙交互：
 *   rbx=0  set_enabled(rcx)
 *   rbx=1  is_enabled()
 *   rbx=2  add_rule(rcx=fw_rule_arg_t*)
 *   rbx=3  remove_rule(rcx=id)
 *   rbx=4  list_rules(rcx=buf, rdx=cap)
 *   rbx=5  save_cfg()
 *
 * UI:
 *   - 顶部标题栏 + 启用/停用开关
 *   - 规则列表（点击行选中）
 *   - 底部按钮：添加拒绝规则 / 删除选中 / 保存配置 / 退出
 * ============================================================ */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "windows_dll.h"
#include "monios_dll.h"
#include "string.h"
#include "syscall.h"

/* 与内核 include/firewall.h 镜像。 */
#define FW_PROTO_ANY   0
#define FW_PROTO_ICMP  1
#define FW_PROTO_TCP   6
#define FW_PROTO_UDP   17
#define FW_DIR_IN      0
#define FW_DIR_OUT     1
#define FW_ACTION_ALLOW 0
#define FW_ACTION_DENY   1

typedef struct {
    uint8_t  direction;
    uint8_t  action;
    uint8_t  protocol;
    uint8_t  pad;
    uint8_t  src_ip[4];
    uint8_t  src_mask[4];
    uint8_t  dst_ip[4];
    uint8_t  dst_mask[4];
    uint16_t src_port;
    uint16_t dst_port;
} fw_rule_arg_t;

#define FW_WIN_X   60
#define FW_WIN_Y   40
#define FW_WIN_W   920
#define FW_WIN_H   640

#define FW_CANVAS  0x00EAF0F6
#define FW_PANEL   0x00FFFFFF
#define FW_TEXT    0x001C2930
#define FW_MUTED   0x005C6A70
#define FW_ACCENT  0x0000717F
#define FW_DANGER  0x00C0392B
#define FW_OK      0x002F8E5D

static char   g_listbuf[1024];
static uint32_t g_list_len;
static int    g_enabled;
static int    g_selected = -1;
static char   g_status[64];

static void fw_refresh(void)
{
    g_enabled = (int) syscall1(SYS_FIREWALL_CTL, 1);
    g_list_len = (uint32_t) syscall3(SYS_FIREWALL_CTL, 4,
                                     (uint64_t) g_listbuf, sizeof(g_listbuf));
    if ((int32_t) g_list_len < 0) g_list_len = 0;
    if (g_list_len >= sizeof(g_listbuf)) g_list_len = sizeof(g_listbuf) - 1;
    g_listbuf[g_list_len] = '\0';
}

static int in_rect(int32_t mx, int32_t my, osui_rect_t r)
{
    return mx >= r.x && mx < (int32_t)(r.x + r.width) &&
           my >= r.y && my < (int32_t)(r.y + r.height);
}

/* 数一下列表里非空规则行数（每行 '#id in|out ...'） */
static int fw_count_rules(void)
{
    int n = 0;
    const char *p = g_listbuf;
    while (*p != '\0') {
        if (*p == '#') n++;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return n;
}

/* 取出第 i 条规则的整行文本到 out */
static void fw_rule_line(int i, char *out, uint32_t cap)
{
    const char *p = g_listbuf;
    int idx = -1;
    out[0] = '\0';
    while (*p != '\0') {
        if (*p == '#') idx++;
        if (idx == i) {
            uint32_t o = 0;
            while (*p != '\0' && *p != '\n' && o + 1 < cap) out[o++] = *p++;
            out[o] = '\0';
            return;
        }
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
}

int main(void)
{
    app_mouse_snapshot_t mouse;
    uint8_t prev = 0;
    osui_rect_t btn_toggle = { FW_WIN_X + 16, 90, 180, 34 };
    osui_rect_t btn_add    = { FW_WIN_X + 16, FW_WIN_Y + FW_WIN_H - 50, 160, 34 };
    osui_rect_t btn_del    = { FW_WIN_X + 190, FW_WIN_Y + FW_WIN_H - 50, 160, 34 };
    osui_rect_t btn_save   = { FW_WIN_X + 364, FW_WIN_Y + FW_WIN_H - 50, 160, 34 };
    osui_rect_t btn_quit   = { FW_WIN_X + 528, FW_WIN_Y + FW_WIN_H - 50, 100, 34 };
    int preset = 0;

    app_enter_graphics_mode();
    fw_refresh();
    g_status[0] = '\0';

    for (;;) {
        app_get_mouse(&mouse);
        if (mouse.buttons & 2u) return 0;

        if ((mouse.buttons & 1u) && !(prev & 1u)) {
            int32_t mx = mouse.x_pixels;
            int32_t my = mouse.y_pixels;

            if (in_rect(mx, my, btn_toggle)) {
                g_enabled = !g_enabled;
                syscall2(SYS_FIREWALL_CTL, 0, (uint64_t) g_enabled);
                g_status[0] = '\0';
                syscall1(SYS_FIREWALL_CTL, 5);  /* auto-save */
            } else if (in_rect(mx, my, btn_add)) {
                /* 循环预设规则模板 */
                fw_rule_arg_t r;
                memset(&r, 0, sizeof(r));
                r.direction = FW_DIR_IN;
                r.action = FW_ACTION_DENY;
                r.protocol = FW_PROTO_TCP;
                /* preset 0: deny TCP 22, 1: deny TCP 445, 2: deny UDP 137,
                 * 3: allow TCP 80, 4: deny ICMP */
                if (preset == 0)      { r.protocol = FW_PROTO_TCP; r.dst_port = 22;  }
                else if (preset == 1) { r.protocol = FW_PROTO_TCP; r.dst_port = 445; }
                else if (preset == 2) { r.protocol = FW_PROTO_UDP; r.dst_port = 137; }
                else if (preset == 3) { r.protocol = FW_PROTO_TCP; r.action = FW_ACTION_ALLOW; r.dst_port = 80; }
                else                  { r.protocol = FW_PROTO_ICMP; r.dst_port = 0; }
                preset = (preset + 1) % 5;
                long id = (long) syscall2(SYS_FIREWALL_CTL, 2, (uint64_t) &r);
                if (id >= 0) {
                    syscall1(SYS_FIREWALL_CTL, 5);
                    g_status[0] = 'o'; g_status[1]='k'; g_status[2]='\0';
                } else {
                    g_status[0]='f'; g_status[1]='u'; g_status[2]='l'; g_status[3]='l'; g_status[4]='\0';
                }
                fw_refresh();
            } else if (in_rect(mx, my, btn_del)) {
                if (g_selected >= 0) {
                    /* 列表里第 selected 条对应的真实 id 从行首 '#<num>' 解析 */
                    char line[128];
                    fw_rule_line(g_selected, line, sizeof(line));
                    if (line[0] == '#') {
                        int id = 0;
                        const char *p = line + 1;
                        while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
                        syscall2(SYS_FIREWALL_CTL, 3, (uint64_t) (uint32_t) id);
                        syscall1(SYS_FIREWALL_CTL, 5);
                    }
                    g_selected = -1;
                    fw_refresh();
                }
            } else if (in_rect(mx, my, btn_save)) {
                syscall1(SYS_FIREWALL_CTL, 5);
                g_status[0]='s'; g_status[1]='a'; g_status[2]='v'; g_status[3]='e'; g_status[4]='d'; g_status[5]='\0';
            } else if (in_rect(mx, my, btn_quit)) {
                return 0;
            } else {
                /* 点击规则行选择 */
                int y0 = 140;
                int rh = 24;
                int n = fw_count_rules();
                int i;
                for (i = 0; i < n && i < 12; i++) {
                    osui_rect_t row = { FW_WIN_X + 16, (uint16_t)(y0 + i * rh),
                                        FW_WIN_W - 32, (uint16_t)(rh - 2) };
                    if (in_rect(mx, my, row)) { g_selected = i; break; }
                }
            }
        }
        prev = mouse.buttons;

        /* ---- 绘制 ---- */
        osui_canvas(FW_CANVAS);
        osui_panel((osui_rect_t){FW_WIN_X, FW_WIN_Y, FW_WIN_W, FW_WIN_H});
        osui_titlebar((osui_rect_t){FW_WIN_X, FW_WIN_Y, FW_WIN_W, 36},
                      "防火墙配置", true);

        /* 状态行 */
        osui_label(FW_WIN_X + 16, FW_WIN_Y + 50,
                   g_enabled ? "状态: 已启用" : "状态: 已停用",
                   g_enabled ? false : true);

        osui_button(btn_toggle,
                    g_enabled ? "停用防火墙" : "启用防火墙",
                    g_enabled ? OSUI_BUTTON_DANGER : OSUI_BUTTON_PRIMARY);

        /* 规则列表 */
        {
            char line[128];
            int n = fw_count_rules();
            int i;
            osui_label(FW_WIN_X + 16, 118, "规则列表（点击选中）", false);
            for (i = 0; i < n && i < 12; i++) {
                uint16_t ry = (uint16_t)(140 + i * 24);
                fw_rule_line(i, line, sizeof(line));
                uint32_t state = (i == g_selected) ? OSUI_STATE_SELECTED : 0;
                osui_list_item((osui_rect_t){FW_WIN_X + 16, ry,
                                             (uint16_t)(FW_WIN_W - 32), 22},
                               line, "", state);
            }
            if (n == 0) {
                osui_label(FW_WIN_X + 16, 150, "(无规则 - 默认放行所有流量)", true);
            }
        }

        /* 底部按钮 */
        osui_button(btn_add,  "添加规则", OSUI_BUTTON_PRIMARY);
        osui_button(btn_del,  "删除选中", OSUI_BUTTON_DANGER);
        osui_button(btn_save, "保存配置", 0);
        osui_button(btn_quit, "退出", OSUI_BUTTON_GHOST);

        if (g_status[0] != '\0') {
            osui_label(FW_WIN_X + 16, FW_WIN_Y + FW_WIN_H - 20, g_status, true);
        }
        osui_present();
    }
}
