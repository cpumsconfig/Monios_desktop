#ifndef _FIREWALL_H_
#define _FIREWALL_H_

#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  Monios 防火墙子系统 (security group)
 *
 *  - 规则表：按协议(TCP/UDP/ICMP)、源/目的 IP、源/目的端口过滤
 *  - 入站/出站双向过滤
 *  - 默认策略：允许 / 拒绝（默认允许，宽容模式）
 *  - 规则持久化：保存到 C:\Monios\System\Config\firewall.rules
 *
 *  集成方式：firewall_init() 会把 firewall_check_inbound/outbound
 *  注册到 net.c 的收发路径钩子上。未注册前（或防火墙未启用时）
 *  net.c 侧钩子为 NULL，所有报文默认放行，不影响现有行为。
 * ============================================================ */

#define FW_PROTO_ANY   0
#define FW_PROTO_ICMP  1
#define FW_PROTO_TCP   6
#define FW_PROTO_UDP   17

#define FW_DIR_IN      0
#define FW_DIR_OUT     1

#define FW_ACTION_ALLOW 0
#define FW_ACTION_DENY   1

#define FW_MAX_RULES   32

typedef struct {
    bool     used;
    uint8_t  direction;   /* FW_DIR_IN / FW_DIR_OUT */
    uint8_t  action;      /* FW_ACTION_ALLOW / FW_ACTION_DENY */
    uint8_t  protocol;    /* FW_PROTO_* */
    uint8_t  src_ip[4];
    uint8_t  src_mask[4]; /* 0.0.0.0 = 任意源 */
    uint8_t  dst_ip[4];
    uint8_t  dst_mask[4]; /* 0.0.0.0 = 任意目的 */
    uint16_t src_port;     /* 0 = 任意端口 */
    uint16_t dst_port;     /* 0 = 任意端口 */
} fw_rule_t;

/* 初始化：清空规则表，默认策略=允许，并向 net.c 注册收发钩子。 */
void firewall_init(void);

/* 启用/停用防火墙。停用时所有报文放行（宽容模式）。 */
void firewall_set_enabled(bool enabled);
bool firewall_enabled(void);

/* 设置默认策略：true=允许，false=拒绝。 */
void firewall_set_default(bool inbound_allow, bool outbound_allow);

/* 添加一条规则，返回规则槽位 id (>=0)，失败返回 -1。 */
int32_t firewall_add_rule(uint8_t direction, uint8_t action, uint8_t protocol,
                          const uint8_t src_ip[4], const uint8_t src_mask[4],
                          const uint8_t dst_ip[4], const uint8_t dst_mask[4],
                          uint16_t src_port, uint16_t dst_port);

/* 按 id 删除规则。 */
bool firewall_remove_rule(int32_t id);

/* 列出规则到文本缓冲区（每行一条），返回写入字符数。 */
uint32_t firewall_list_rules(char *buffer, uint32_t buffer_size);

/* 核心判定：返回 true 表示放行，false 表示拦截。
 * 由 net.c 在收/发路径调用；也可被 syscall/测试直接调用。 */
bool firewall_check_inbound(uint8_t protocol,
                            const uint8_t src_ip[4], const uint8_t dst_ip[4],
                            uint16_t src_port, uint16_t dst_port);
bool firewall_check_outbound(uint8_t protocol,
                             const uint8_t src_ip[4], const uint8_t dst_ip[4],
                             uint16_t src_port, uint16_t dst_port);

/* 持久化：把规则表写到 path（Windows 盘符风格，如
 * C:\Monios\System\Config\firewall.rules）。 */
bool firewall_save_rules(const char *path);
bool firewall_load_rules(const char *path);

/* 统计：被拦截的入站/出站报文计数。 */
uint32_t firewall_dropped_inbound(void);
uint32_t firewall_dropped_outbound(void);

/* 用户态 ioctl 参数结构（与 user/apps/firewall.c 镜像一致）。
 * 方向 / 动作 / 协议使用 FW_DIR_x / FW_ACTION_x / FW_PROTO_x 常量。 */
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

#endif /* _FIREWALL_H_ */
