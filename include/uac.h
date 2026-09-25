#ifndef _UAC_H_
#define _UAC_H_

#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  Monios 用户权限隔离 / UAC 子系统 (security group)
 *
 *  - 用户会话：当前登录用户名 + 普通用户/管理员级别。
 *  - 文件权限：通过 file_uac_set_hook() 挂到 file.c 的
 *    open/read/write/delete/mkdir/rmdir/list 路径。
 *  - 用户目录隔离：C:\Monios\Users\<name>\ 仅该用户（或管理员）可写。
 *  - UAC 提权：需要管理员权限时调用 graphics_request_uac_elevation()。
 *
 *  未登录任何用户（g_user 为空）时全部放行，保证默认宽容模式，
 *  不影响 119 项 VFS 回归测试。
 * ============================================================ */

#define UAC_PRIV_STANDARD  0
#define UAC_PRIV_ADMIN     1

/* 初始化：注册文件访问钩子到 file.c。 */
void uac_init(void);

/* 设置当前登录用户。username 为 NULL 或空串表示未登录（全放行）。 */
void uac_set_user(const char *username, bool is_admin);

/* 当前用户查询。 */
const char *uac_user(void);
bool uac_is_admin(void);

/* 权限判定：true=允许访问，false=拒绝。file.c 读/写路径调用。 */
bool uac_can_access(const char *winpath, bool write);

/* 请求提权：弹出 UAC 确认框，返回用户是否同意。 */
bool uac_request_elevation(const char *program_path, const char *reason,
                           uint32_t privilege_level);

#endif /* _UAC_H_ */
