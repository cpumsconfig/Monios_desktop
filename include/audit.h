#ifndef _AUDIT_H_
#define _AUDIT_H_

#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  Monios 审计子系统
 *
 *  - 记录安全相关事件到 /Monios/System/Logs/audit.log。
 *  - 环形缓冲批量写入，避免每次事件都同步落盘影响性能。
 *  - 日志文件超过 1MB 时滚动为 audit.log.1，重新开始。
 *  - 仅管理员可查看 / 清空。
 * ============================================================ */

typedef enum {
    AUDIT_LOGIN_SUCCESS = 0,
    AUDIT_LOGIN_FAIL,
    AUDIT_LOGIN_LOCKED,
    AUDIT_LOGIN_UNLOCK,
    AUDIT_FILE_ACCESS,
    AUDIT_FILE_DENIED,
    AUDIT_FILE_DELETE,
    AUDIT_ELEVATION,
    AUDIT_FIREWALL_CHANGE,
    AUDIT_DRIVER_LOAD,
    AUDIT_DRIVER_UNLOAD,
    AUDIT_USER_ADD,
    AUDIT_USER_DEL,
    AUDIT_PASSWD_CHANGE,
    AUDIT_OTHER
} audit_event_t;

void audit_init(void);

/* 记录一条审计事件。detail 可为 NULL。result=true 表示成功，false 表示拒绝/失败。 */
void audit_log(audit_event_t event, const char *user, const char *detail, bool result);

/* 把内存中的暂存缓冲刷到磁盘。 */
void audit_flush(void);

/* 清空审计日志（截断为 0 字节）。 */
bool audit_clear(void);

/* 把全部日志内容（含暂存）拷到 buffer，返回字节数。 */
uint32_t audit_dump(char *buffer, uint32_t buffer_size);

/* 事件类型转字符串（用于日志行）。 */
const char *audit_event_name(audit_event_t event);

#endif /* _AUDIT_H_ */
