#ifndef _USER_MGMT_H_
#define _USER_MGMT_H_

#include "stdbool.h"
#include "stdint.h"

#define USER_NAME_MAX  32
#define USER_PASS_MAX  64
#define USER_HOME_MAX  128
#define USER_SHELL_MAX  64
#define USER_MAX_COUNT  32

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char     username[USER_NAME_MAX];
    char     password[USER_PASS_MAX];   /* plaintext for simplicity (Monios) */
    char     home[USER_HOME_MAX];
    char     shell[USER_SHELL_MAX];
} user_entry_t;

/* User-management syscall handlers. */
uint32_t sys_getuid(void);
uint32_t sys_geteuid(void);
uint32_t sys_getgid(void);
uint32_t sys_getegid(void);
int32_t  sys_setuid(uint32_t uid);
int32_t  sys_setgid(uint32_t gid);

/* Database helpers. */
void     user_mgmt_init(void);
bool     user_lookup_by_name(const char *name, user_entry_t *out);
bool     user_lookup_by_uid(uint32_t uid, user_entry_t *out);
bool     user_authenticate(const char *name, const char *password);
int32_t  user_login(const char *name, const char *password);
void     user_logout(void);
const char *user_current_name(void);

/* su: switch current process to the given user (requires root or correct password). */
int32_t  user_su(const char *target_name, const char *password);

/* Login-session user (for kernel-mode contexts like the shell that do not
 * have a process PCB).  New processes inherit these credentials. */
void     user_set_session(uint32_t uid, uint32_t gid);
uint32_t user_session_uid(void);
uint32_t user_session_gid(void);
const char *user_session_name(void);

/* ============================================================
 *  登录失败锁定 (per-account)
 *
 *  - user_authenticate() 在判定密码前先检查是否处于锁定窗口；
 *    判定失败时递增失败计数，达到阈值后锁定。
 *  - 阈值 / 锁定时长可通过注册表配置：
 *      Security/FailMax      (默认 5)
 *      Security/LockSeconds   (默认 900 = 15 分钟)
 * ============================================================ */

/* 该用户当前是否被锁定（锁定期间密码正确也拒绝登录）。 */
bool     user_is_locked(const char *name);

/* 管理员手动解锁；返回 true 表示解锁成功（或本来就未锁定）。 */
bool     user_admin_unlock(const char *name);

/* 查询某用户的连续失败次数（用于 shell 状态显示）。 */
uint32_t user_failed_count(const char *name);

#endif /* _USER_MGMT_H_ */
