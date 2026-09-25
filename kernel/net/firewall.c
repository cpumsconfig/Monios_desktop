/* ============================================================
 *  Monios 防火墙子系统实现
 *  kernel/net/firewall.c
 *
 *  设计要点：
 *   - 规则表静态分配（FW_MAX_RULES 条），首条命中优先。
 *   - 未命中规则时使用默认策略（默认允许）。
 *   - firewall_init() 通过 net_register_firewall_hooks() 把判定函数
 *     挂到 net.c 的收/发路径；net.c 在钩子为 NULL 时直接放行。
 *   - 持久化走 lib/file.c 的 file_read/file_write（Windows 盘符路径）。
 * ============================================================ */

#include "common.h"
#include "file.h"
#include "firewall.h"
#include "net.h"
#include "path.h"
#include "string.h"

static fw_rule_t g_rules[FW_MAX_RULES];
static bool g_enabled = false;            /* 默认停用 = 宽容模式 */
static bool g_default_inbound = true;     /* 默认放行入站 */
static bool g_default_outbound = true;   /* 默认放行出站 */
static uint32_t g_dropped_in = 0;
static uint32_t g_dropped_out = 0;

/* ------------------------------------------------------------
 *  地址/端口匹配
 * ------------------------------------------------------------ */

static bool fw_ip_match(const uint8_t *pkt_ip, const uint8_t *rule_ip,
                        const uint8_t *mask)
{
    uint32_t i;

    /* mask 全 0 => 任意地址 */
    if (mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0) {
        return true;
    }
    for (i = 0; i < 4; i++) {
        if ((pkt_ip[i] & mask[i]) != (rule_ip[i] & mask[i])) {
            return false;
        }
    }
    return true;
}

static bool fw_port_match(uint16_t pkt_port, uint16_t rule_port)
{
    /* rule_port == 0 => 任意端口 */
    if (rule_port == 0) {
        return true;
    }
    return pkt_port == rule_port;
}

static bool fw_rule_applies(const fw_rule_t *r, uint8_t protocol,
                            const uint8_t src_ip[4], const uint8_t dst_ip[4],
                            uint16_t src_port, uint16_t dst_port)
{
    if (r->protocol != FW_PROTO_ANY && r->protocol != protocol) {
        return false;
    }
    if (!fw_ip_match(src_ip, r->src_ip, r->src_mask)) {
        return false;
    }
    if (!fw_ip_match(dst_ip, r->dst_ip, r->dst_mask)) {
        return false;
    }
    if (!fw_port_match(src_port, r->src_port)) {
        return false;
    }
    if (!fw_port_match(dst_port, r->dst_port)) {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------
 *  核心判定
 * ------------------------------------------------------------ */

static bool fw_evaluate(uint8_t direction, uint8_t protocol,
                        const uint8_t src_ip[4], const uint8_t dst_ip[4],
                        uint16_t src_port, uint16_t dst_port)
{
    uint32_t i;
    bool default_allow = (direction == FW_DIR_IN) ? g_default_inbound
                                                  : g_default_outbound;

    /* 未启用 => 全部放行 */
    if (!g_enabled) {
        return true;
    }

    for (i = 0; i < FW_MAX_RULES; i++) {
        const fw_rule_t *r = &g_rules[i];

        if (!r->used || r->direction != direction) {
            continue;
        }
        if (!fw_rule_applies(r, protocol, src_ip, dst_ip, src_port, dst_port)) {
            continue;
        }
        /* 首条命中即按其动作决策 */
        return r->action == FW_ACTION_ALLOW;
    }
    return default_allow;
}

bool firewall_check_inbound(uint8_t protocol,
                            const uint8_t src_ip[4], const uint8_t dst_ip[4],
                            uint16_t src_port, uint16_t dst_port)
{
    bool allow = fw_evaluate(FW_DIR_IN, protocol, src_ip, dst_ip,
                             src_port, dst_port);
    if (!allow) {
        g_dropped_in++;
    }
    return allow;
}

bool firewall_check_outbound(uint8_t protocol,
                             const uint8_t src_ip[4], const uint8_t dst_ip[4],
                             uint16_t src_port, uint16_t dst_port)
{
    bool allow = fw_evaluate(FW_DIR_OUT, protocol, src_ip, dst_ip,
                             src_port, dst_port);
    if (!allow) {
        g_dropped_out++;
    }
    return allow;
}

/* ------------------------------------------------------------
 *  规则管理
 * ------------------------------------------------------------ */

void firewall_set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool firewall_enabled(void)
{
    return g_enabled;
}

void firewall_set_default(bool inbound_allow, bool outbound_allow)
{
    g_default_inbound = inbound_allow;
    g_default_outbound = outbound_allow;
}

int32_t firewall_add_rule(uint8_t direction, uint8_t action, uint8_t protocol,
                          const uint8_t src_ip[4], const uint8_t src_mask[4],
                          const uint8_t dst_ip[4], const uint8_t dst_mask[4],
                          uint16_t src_port, uint16_t dst_port)
{
    uint32_t i;
    fw_rule_t *r;

    for (i = 0; i < FW_MAX_RULES; i++) {
        if (!g_rules[i].used) {
            break;
        }
    }
    if (i >= FW_MAX_RULES) {
        return -1;
    }

    r = &g_rules[i];
    memset(r, 0, sizeof(*r));
    r->used = true;
    r->direction = direction;
    r->action = action;
    r->protocol = protocol;
    if (src_ip != NULL) {
        memcpy(r->src_ip, src_ip, 4);
    }
    if (src_mask != NULL) {
        memcpy(r->src_mask, src_mask, 4);
    }
    if (dst_ip != NULL) {
        memcpy(r->dst_ip, dst_ip, 4);
    }
    if (dst_mask != NULL) {
        memcpy(r->dst_mask, dst_mask, 4);
    }
    r->src_port = src_port;
    r->dst_port = dst_port;
    return (int32_t) i;
}

bool firewall_remove_rule(int32_t id)
{
    if (id < 0 || id >= FW_MAX_RULES) {
        return false;
    }
    if (!g_rules[id].used) {
        return false;
    }
    g_rules[id].used = false;
    return true;
}

static void fw_append_line(char *buffer, uint32_t buffer_size, uint32_t *off,
                           const char *text)
{
    uint32_t l = (uint32_t) strlen(text);

    if (*off + l + 1 >= buffer_size) {
        return;
    }
    memcpy(buffer + *off, text, l);
    *off += l;
    buffer[*off] = '\n';
    (*off)++;
    buffer[*off] = '\0';
}

static void fw_format_ip(char out[16], const uint8_t ip[4])
{
    /* 极简数字格式化，避免依赖 ip_to_text 在内核 net 模块的可见性 */
    static const char digits[] = "0123456789";
    uint32_t i;
    uint32_t o = 0;

    for (i = 0; i < 4; i++) {
        uint8_t v = ip[i];
        char tmp[4];
        uint32_t t = 0;

        if (v == 0) {
            tmp[t++] = '0';
        } else {
            while (v > 0 && t < sizeof(tmp)) {
                tmp[t++] = digits[v % 10];
                v /= 10;
            }
        }
        while (t > 0 && o < 15) {
            out[o++] = tmp[--t];
        }
        if (i < 3 && o < 15) {
            out[o++] = '.';
        }
    }
    out[o] = '\0';
}

uint32_t firewall_list_rules(char *buffer, uint32_t buffer_size)
{
    uint32_t i;
    uint32_t off = 0;
    char hdr[64];

    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';

    /* 默认策略行 */
    hdr[0] = '\0';
    /* 用极简拼接构造 "default in=allow out=deny" */
    {
        char tmp[32];
        const char *din = g_default_inbound ? "allow" : "deny";
        const char *dout = g_default_outbound ? "allow" : "deny";
        strcpy(hdr, "default in=");
        strcpy(hdr + strlen(hdr), din);
        strcpy(hdr + strlen(hdr), " out=");
        strcpy(hdr + strlen(hdr), dout);
        tmp[0] = '\0';
        (void) tmp;
    }
    fw_append_line(buffer, buffer_size, &off, hdr);

    for (i = 0; i < FW_MAX_RULES; i++) {
        const fw_rule_t *r = &g_rules[i];
        char line[128];
        char sip[16];
        char smask[16];
        char dip[16];
        char dmask[16];
        const char *dir = (r->direction == FW_DIR_IN) ? "in " : "out";
        const char *act = (r->action == FW_ACTION_ALLOW) ? "allow" : "deny";
        const char *proto;

        if (!r->used) {
            continue;
        }
        switch (r->protocol) {
            case FW_PROTO_TCP:  proto = "tcp";  break;
            case FW_PROTO_UDP:  proto = "udp";  break;
            case FW_PROTO_ICMP: proto = "icmp"; break;
            default:            proto = "any";  break;
        }
        fw_format_ip(sip, r->src_ip);
        fw_format_ip(smask, r->src_mask);
        fw_format_ip(dip, r->dst_ip);
        fw_format_ip(dmask, r->dst_mask);

        line[0] = '\0';
        strcpy(line, "#");
        /* id */
        {
            char idstr[8];
            uint32_t id = i;
            uint32_t t = 0;
            if (id == 0) {
                idstr[t++] = '0';
            }
            while (id > 0 && t < sizeof(idstr) - 1) {
                idstr[t++] = (char) ('0' + (id % 10));
                id /= 10;
            }
            while (t > 0) {
                /* 反向写入 */
                line[strlen(line)] = idstr[--t];
            }
        }
        strcpy(line + strlen(line), " ");
        strcpy(line + strlen(line), dir);
        strcpy(line + strlen(line), " ");
        strcpy(line + strlen(line), act);
        strcpy(line + strlen(line), " ");
        strcpy(line + strlen(line), proto);
        strcpy(line + strlen(line), " ");
        strcpy(line + strlen(line), sip);
        strcpy(line + strlen(line), "/");
        strcpy(line + strlen(line), smask);
        strcpy(line + strlen(line), " :");
        /* src port */
        if (r->src_port == 0) {
            strcpy(line + strlen(line), "*");
        } else {
            char p[8];
            uint32_t pn = r->src_port;
            uint32_t t = 0;
            if (pn == 0) { p[t++] = '0'; }
            while (pn > 0 && t < sizeof(p) - 1) { p[t++] = (char)('0' + pn % 10); pn /= 10; }
            while (t > 0) { line[strlen(line)] = p[--t]; }
        }
        strcpy(line + strlen(line), " -> ");
        strcpy(line + strlen(line), dip);
        strcpy(line + strlen(line), "/");
        strcpy(line + strlen(line), dmask);
        strcpy(line + strlen(line), " :");
        if (r->dst_port == 0) {
            strcpy(line + strlen(line), "*");
        } else {
            char p[8];
            uint32_t pn = r->dst_port;
            uint32_t t = 0;
            if (pn == 0) { p[t++] = '0'; }
            while (pn > 0 && t < sizeof(p) - 1) { p[t++] = (char)('0' + pn % 10); pn /= 10; }
            while (t > 0) { line[strlen(line)] = p[--t]; }
        }
        fw_append_line(buffer, buffer_size, &off, line);
    }
    return off;
}

/* ------------------------------------------------------------
 *  持久化（文本格式，每行一条规则；二进制字段按 IP 文本序列化）
 *  格式与 firewall_list_rules 输出一致，便于人工查看。
 * ------------------------------------------------------------ */

bool firewall_save_rules(const char *path)
{
    char buffer[1024];

    if (path == NULL) {
        return false;
    }
    firewall_list_rules(buffer, sizeof(buffer));
    if (file_write(path, buffer, (uint32_t) strlen(buffer)) < 0) {
        return false;
    }
    return true;
}

bool firewall_load_rules(const char *path)
{
    /* 基础框架：解析留给后续 shell/配置层；此处仅校验文件存在性，
     * 规则表由 firewall_add_rule() 动态配置。保留加载接口以便持久化闭环。 */
    if (path == NULL) {
        return false;
    }
    return file_exists(path);
}

uint32_t firewall_dropped_inbound(void)
{
    return g_dropped_in;
}

uint32_t firewall_dropped_outbound(void)
{
    return g_dropped_out;
}

/* ------------------------------------------------------------
 *  初始化 + 向 net.c 注册收发钩子
 * ------------------------------------------------------------ */

void firewall_init(void)
{
    uint32_t i;

    for (i = 0; i < FW_MAX_RULES; i++) {
        memset(&g_rules[i], 0, sizeof(g_rules[i]));
    }
    g_enabled = false;        /* 默认宽容：不启用即不拦截 */
    g_default_inbound = true;
    g_default_outbound = true;
    g_dropped_in = 0;
    g_dropped_out = 0;

    /* 把判定函数挂到 net.c。net_register_firewall_hooks 定义在 net.c，
     * 在钩子为 NULL 时 net.c 直接放行，因此未链接本文件也安全。 */
    net_register_firewall_hooks(firewall_check_inbound, firewall_check_outbound);
}
