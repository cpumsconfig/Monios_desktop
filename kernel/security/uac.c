/* ============================================================
 *  Monios 用户权限隔离 / UAC 子系统实现
 *  kernel/security/uac.c
 *
 *  设计要点：
 *   - uac_init() 通过 file_uac_set_hook() 把 uac_can_access 注册到
 *     lib/file.c；file.c 在钩子为 NULL 时全部放行。
 *   - 未登录用户（g_user 为空）时 uac_can_access 一律放行，
 *     保证默认宽容模式、回归测试不受影响。
 *   - 管理员放行一切；普通用户只能写自己的用户目录。
 * ============================================================ */

#include "common.h"
#include "file.h"
#include "graphics.h"
#include "string.h"
#include "uac.h"

static char g_user[64] = {0};
static bool g_is_admin = false;

/* 用户目录前缀（盘符任意，按前缀匹配） */
static const char USERS_PREFIX[] = ":\\Monios\\Users\\";
static const char SYSTEM_PREFIX[] = ":\\Monios\\System\\";

static char to_lower_char(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char) (c - 'A' + 'a');
    }
    return c;
}

/* 比较路径是否以 "X:\Monios\Users\" 开头，X 任意盘符。
 * 匹配成功返回 Users 段后第一个字符的指针；失败返回 NULL。 */
static const char *path_after_users_prefix(const char *path)
{
    uint32_t i;

    if (path == NULL) {
        return NULL;
    }
    /* 期望: <drive>:\Monios\Users\... */
    if (path[1] != ':' || path[2] != '\\') {
        return NULL;
    }
    for (i = 0; USERS_PREFIX[i] != '\0'; i++) {
        char expect = USERS_PREFIX[i];
        char actual = path[3 + i];

        if (expect == '\\') {
            if (actual != '\\' && actual != '/') {
                return NULL;
            }
            continue;
        }
        if (to_lower_char(actual) != to_lower_char(expect)) {
            return NULL;
        }
    }
    return path + 3 + i;
}

static bool path_has_system_prefix(const char *path)
{
    uint32_t i;

    if (path == NULL || path[1] != ':' || path[2] != '\\') {
        return false;
    }
    for (i = 0; SYSTEM_PREFIX[i] != '\0'; i++) {
        char expect = SYSTEM_PREFIX[i];
        char actual = path[3 + i];

        if (expect == '\\') {
            if (actual != '\\' && actual != '/') {
                return false;
            }
            continue;
        }
        if (to_lower_char(actual) != to_lower_char(expect)) {
            return false;
        }
    }
    return true;
}

/* 提取路径中用户目录段（Users 后第一个 \ 之前的名字） */
static void extract_owner(const char *after_users, char *owner, uint32_t owner_size)
{
    uint32_t i = 0;

    if (after_users == NULL || owner == NULL || owner_size == 0) {
        return;
    }
    while (after_users[i] != '\0' &&
           after_users[i] != '\\' && after_users[i] != '/' &&
           i + 1 < owner_size) {
        owner[i] = to_lower_char(after_users[i]);
        i++;
    }
    owner[i] = '\0';
}

bool uac_can_access(const char *winpath, bool write)
{
    char owner[64];
    const char *after;
    char user_lower[64];
    uint32_t i;

    /* 未登录：宽容放行 */
    if (g_user[0] == '\0') {
        return true;
    }
    /* 管理员放行一切 */
    if (g_is_admin) {
        return true;
    }

    /* 系统目录：普通用户可读，不可写 */
    if (write && path_has_system_prefix(winpath)) {
        return false;
    }

    /* 用户目录隔离 */
    after = path_after_users_prefix(winpath);
    if (after != NULL) {
        extract_owner(after, owner, sizeof(owner));
        for (i = 0; g_user[i] != '\0' && i < sizeof(user_lower); i++) {
            user_lower[i] = to_lower_char(g_user[i]);
        }
        user_lower[i] = '\0';

        if (owner[0] != '\0' && strcmp(owner, user_lower) == 0) {
            return true;  /* 自己的目录 */
        }
        /* 别人的用户目录：拒绝 */
        return false;
    }

    /* 其他路径（C:\Monios\Programs 等）默认放行 */
    return true;
}

void uac_set_user(const char *username, bool is_admin)
{
    g_is_admin = is_admin;
    if (username == NULL || username[0] == '\0') {
        g_user[0] = '\0';
        return;
    }
    strlcpy(g_user, username, sizeof(g_user));
}

const char *uac_user(void)
{
    return g_user;
}

bool uac_is_admin(void)
{
    return g_is_admin;
}

bool uac_request_elevation(const char *program_path, const char *reason,
                           uint32_t privilege_level)
{
    bool ok;

    /* 已具备管理员权限无需提权 */
    if (g_is_admin) {
        return true;
    }
    ok = graphics_request_uac_elevation(program_path, reason, privilege_level);
    if (ok) {
        g_is_admin = true;
    }
    return ok;
}

void uac_init(void)
{
    g_user[0] = '\0';
    g_is_admin = false;
    /* 注册文件访问钩子到 file.c。未注册前 file.c 全部放行。 */
    file_uac_set_hook(uac_can_access);
}
