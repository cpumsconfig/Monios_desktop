#include "common.h"
#include "user_mgmt.h"
#include "pcb.h"
#include "file.h"
#include "string.h"
#include "audit.h"
#include "kernel.h"
#include "registry.h"

static user_entry_t g_users[USER_MAX_COUNT];
static uint32_t     g_user_count;

/* ============================================================
 *  登录失败锁定状态（每用户一份，与 g_users 并行索引）。
 *  阈值 / 时长从注册表读取，缺省 5 次 / 900 秒。
 * ============================================================ */
typedef struct {
    uint32_t fail_count;
    uint64_t lock_until_tick;   /* 0 = 未锁定 */
} lock_state_t;

static lock_state_t g_lock[USER_MAX_COUNT];

static uint32_t lock_fail_max(void)
{
    const char *v = registry_get("Security/FailMax");
    if (v == NULL || v[0] == '\0') return 5U;
    uint32_t n = 0;
    while (*v >= '0' && *v <= '9') { n = n * 10 + (uint32_t)(*v - '0'); v++; }
    return n == 0 ? 5U : n;
}

static uint32_t lock_seconds(void)
{
    const char *v = registry_get("Security/LockSeconds");
    if (v == NULL || v[0] == '\0') return 900U;
    uint32_t n = 0;
    while (*v >= '0' && *v <= '9') { n = n * 10 + (uint32_t)(*v - '0'); v++; }
    return n == 0 ? 900U : n;
}

static int32_t lock_index_by_name(const char *name)
{
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, name) == 0) return (int32_t) i;
    }
    return -1;
}

bool user_is_locked(const char *name)
{
    int32_t idx = lock_index_by_name(name);
    uint64_t now;

    if (idx < 0) return false;
    if (g_lock[idx].lock_until_tick == 0) return false;
    now = timer_ticks();
    if (now >= g_lock[idx].lock_until_tick) {
        /* 锁定窗口已过，自动释放 */
        g_lock[idx].lock_until_tick = 0;
        g_lock[idx].fail_count = 0;
        return false;
    }
    return true;
}

uint32_t user_failed_count(const char *name)
{
    int32_t idx = lock_index_by_name(name);
    if (idx < 0) return 0;
    return g_lock[idx].fail_count;
}

bool user_admin_unlock(const char *name)
{
    int32_t idx = lock_index_by_name(name);
    if (idx < 0) return false;
    g_lock[idx].lock_until_tick = 0;
    g_lock[idx].fail_count = 0;
    audit_log(AUDIT_LOGIN_UNLOCK, user_session_name(), name, true);
    return true;
}

static uint32_t user_atoi(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint32_t)(*s - '0'); s++; }
    return v;
}

/*
 * User / group management.
 *
 * The user database is stored at /Monios/System/Config/pwd.txt in a
 * passwd-like format:  uid:gid:username:password:home:shell
 * Root (uid=0) always exists.
 */

#define USER_DB_PATH  "/Monios/System/Config/passwd"

static bool         g_initialised;
static uint32_t     g_session_uid = 0;
static uint32_t     g_session_gid = 0;

static const char *skip_field(const char *p, char *dst, uint32_t dst_size)
{
    uint32_t i = 0;
    if (dst != NULL && dst_size > 0) dst[0] = '\0';
    while (*p != '\0' && *p != ':' && *p != '\n' && *p != '\r') {
        if (dst != NULL && i + 1 < dst_size) {
            dst[i++] = *p;
        }
        p++;
    }
    if (dst != NULL && dst_size > 0) dst[i] = '\0';
    if (*p == ':') p++;
    return p;
}

static void user_load_database(void)
{
    static uint8_t buf[8192];
    int32_t size;
    const char *p;
    const char *line_start;

    g_user_count = 0;
    size = file_read(USER_DB_PATH, buf, sizeof(buf) - 1);
    if (size <= 0) {
        /* Fall back to default users: root, monios, guest. */
        g_users[0].uid = 0; g_users[0].gid = 0;
        strlcpy(g_users[0].username, "root", sizeof(g_users[0].username));
        /* Never fail open for privileged/local accounts when the database is
         * missing or unreadable.  "!" is an invalid interactive password and
         * keeps recovery/setup responsible for provisioning credentials. */
        strlcpy(g_users[0].password, "!", sizeof(g_users[0].password));
        strlcpy(g_users[0].home, "/Monios/Users/root", sizeof(g_users[0].home));
        strlcpy(g_users[0].shell, "/bin/sh", sizeof(g_users[0].shell));
        g_users[1].uid = 1000; g_users[1].gid = 1000;
        strlcpy(g_users[1].username, "monios", sizeof(g_users[1].username));
        strlcpy(g_users[1].password, "!", sizeof(g_users[1].password));
        strlcpy(g_users[1].home, "/Monios/Users/monios", sizeof(g_users[1].home));
        strlcpy(g_users[1].shell, "/bin/sh", sizeof(g_users[1].shell));
        g_users[2].uid = 1001; g_users[2].gid = 1001;
        strlcpy(g_users[2].username, "guest", sizeof(g_users[2].username));
        strlcpy(g_users[2].password, "", sizeof(g_users[2].password));
        strlcpy(g_users[2].home, "/Monios/Users/guest", sizeof(g_users[2].home));
        strlcpy(g_users[2].shell, "/bin/sh", sizeof(g_users[2].shell));
        g_user_count = 3;
        return;
    }
    buf[size] = '\0';
    p = (const char *) buf;
    while (*p != '\0' && g_user_count < USER_MAX_COUNT) {
        char uid_str[16], gid_str[16];
        user_entry_t *e = &g_users[g_user_count];

        line_start = p;
        p = skip_field(p, uid_str, sizeof(uid_str));
        p = skip_field(p, gid_str, sizeof(gid_str));
        p = skip_field(p, e->username, sizeof(e->username));
        p = skip_field(p, e->password, sizeof(e->password));
        p = skip_field(p, e->home, sizeof(e->home));
        p = skip_field(p, e->shell, sizeof(e->shell));
        e->uid = user_atoi(uid_str);
        e->gid = user_atoi(gid_str);
        if (e->username[0] != '\0') {
            g_user_count++;
        }
        /* Advance to next line. */
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
        if (p == line_start) break;  /* safety */
    }
    if (g_user_count == 0) {
        g_users[0].uid = 0; g_users[0].gid = 0;
        strlcpy(g_users[0].username, "root", sizeof(g_users[0].username));
        strlcpy(g_users[0].password, "!", sizeof(g_users[0].password));
        g_users[1].uid = 1000; g_users[1].gid = 1000;
        strlcpy(g_users[1].username, "monios", sizeof(g_users[1].username));
        strlcpy(g_users[1].password, "!", sizeof(g_users[1].password));
        g_users[2].uid = 1001; g_users[2].gid = 1001;
        strlcpy(g_users[2].username, "guest", sizeof(g_users[2].username));
        strlcpy(g_users[2].password, "", sizeof(g_users[2].password));
        g_user_count = 3;
    }
}

void user_mgmt_init(void)
{
    user_load_database();
    g_initialised = true;
}

bool user_lookup_by_name(const char *name, user_entry_t *out)
{
    if (!g_initialised) user_mgmt_init();
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, name) == 0) {
            if (out != NULL) *out = g_users[i];
            return true;
        }
    }
    return false;
}

bool user_lookup_by_uid(uint32_t uid, user_entry_t *out)
{
    if (!g_initialised) user_mgmt_init();
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].uid == uid) {
            if (out != NULL) *out = g_users[i];
            return true;
        }
    }
    return false;
}

bool user_authenticate(const char *name, const char *password)
{
    user_entry_t e;
    int32_t idx;

    if (name == NULL || password == NULL || name[0] == '\0' ||
        !user_lookup_by_name(name, &e)) {
        audit_log(AUDIT_LOGIN_FAIL, name, "no such user", false);
        return false;
    }

    /* 锁定检查：锁定期间即使密码正确也拒绝。 */
    if (user_is_locked(name)) {
        audit_log(AUDIT_LOGIN_LOCKED, name, "account locked", false);
        return false;
    }

    if (e.password[0] == '!' || e.password[0] == '*') {
        audit_log(AUDIT_LOGIN_FAIL, name, "account disabled", false);
        return false;
    }
    if (e.password[0] == '\0') {
        /* 空密码 = 无认证，直接放行（清空失败计数） */
        idx = lock_index_by_name(name);
        if (idx >= 0) { g_lock[idx].fail_count = 0; g_lock[idx].lock_until_tick = 0; }
        audit_log(AUDIT_LOGIN_SUCCESS, name, "empty password", true);
        return true;
    }
    if (strcmp(e.password, password) != 0) {
        /* 失败：递增计数，达到阈值则锁定 */
        idx = lock_index_by_name(name);
        if (idx >= 0) {
            uint32_t hz = timer_hz();
            g_lock[idx].fail_count++;
            if (g_lock[idx].fail_count >= lock_fail_max()) {
                g_lock[idx].lock_until_tick = timer_ticks() +
                    (uint64_t)(hz == 0 ? 1U : hz) * (uint64_t) lock_seconds();
                audit_log(AUDIT_LOGIN_LOCKED, name,
                          "max failed attempts reached", false);
            } else {
                audit_log(AUDIT_LOGIN_FAIL, name, "bad password", false);
            }
        } else {
            audit_log(AUDIT_LOGIN_FAIL, name, "bad password", false);
        }
        return false;
    }

    /* 成功：清零失败计数 */
    idx = lock_index_by_name(name);
    if (idx >= 0) {
        g_lock[idx].fail_count = 0;
        g_lock[idx].lock_until_tick = 0;
    }
    audit_log(AUDIT_LOGIN_SUCCESS, name, NULL, true);
    return true;
}

const char *user_current_name(void)
{
    pcb_t *cur = pcb_get_current();
    static char name[USER_NAME_MAX];
    user_entry_t e;

    if (cur != NULL && user_lookup_by_uid(cur->euid, &e)) {
        strlcpy(name, e.username, sizeof(name));
        return name;
    }
    strlcpy(name, "root", sizeof(name));
    return name;
}

uint32_t sys_getuid(void)
{
    pcb_t *cur = pcb_get_current();
    return (cur != NULL) ? cur->uid : 0;
}

uint32_t sys_geteuid(void)
{
    pcb_t *cur = pcb_get_current();
    return (cur != NULL) ? cur->euid : 0;
}

uint32_t sys_getgid(void)
{
    pcb_t *cur = pcb_get_current();
    return (cur != NULL) ? cur->gid : 0;
}

uint32_t sys_getegid(void)
{
    pcb_t *cur = pcb_get_current();
    return (cur != NULL) ? cur->egid : 0;
}

int32_t sys_setuid(uint32_t uid)
{
    pcb_t *cur = pcb_get_current();
    if (cur == NULL) return -1;
    /* Root can set any uid; non-root can only set to its own uid. */
    if (cur->euid != 0 && uid != cur->uid && uid != cur->euid) {
        return -1;
    }
    if (cur->euid == 0) {
        cur->uid = uid;
    }
    cur->euid = uid;
    return 0;
}

int32_t sys_setgid(uint32_t gid)
{
    pcb_t *cur = pcb_get_current();
    if (cur == NULL) return -1;
    if (cur->egid != 0 && gid != cur->gid && gid != cur->egid) {
        return -1;
    }
    if (cur->egid == 0) {
        cur->gid = gid;
    }
    cur->egid = gid;
    return 0;
}

int32_t user_login(const char *name, const char *password)
{
    user_entry_t e;
    pcb_t *cur;
    if (!user_authenticate(name, password)) return -1;
    if (!user_lookup_by_name(name, &e)) return -1;
    cur = pcb_get_current();
    if (cur != NULL) {
        pcb_set_credentials((int32_t) cur->pid, e.uid, e.gid, e.uid, e.gid);
    }
    return 0;
}

void user_logout(void)
{
    pcb_t *cur = pcb_get_current();
    if (cur != NULL) {
        pcb_set_credentials((int32_t) cur->pid, 0, 0, 0, 0);
    }
}

int32_t user_su(const char *target_name, const char *password)
{
    user_entry_t e;
    pcb_t *cur;
    if (!user_lookup_by_name(target_name, &e)) return -1;
    cur = pcb_get_current();
    if (cur == NULL) {
        /* Kernel-mode context (e.g. shell): use session identity. */
        if (g_session_uid != 0 && !user_authenticate(target_name, password)) {
            return -1;
        }
        g_session_uid = e.uid;
        g_session_gid = e.gid;
        return 0;
    }
    /* Root can su without password; others need the target's password. */
    if (cur->euid != 0 && !user_authenticate(target_name, password)) {
        return -1;
    }
    pcb_set_credentials((int32_t) cur->pid,
                        cur->uid, cur->gid,
                        e.uid, e.gid);
    return 0;
}

void user_set_session(uint32_t uid, uint32_t gid)
{
    g_session_uid = uid;
    g_session_gid = gid;
}

uint32_t user_session_uid(void)
{
    return g_session_uid;
}

uint32_t user_session_gid(void)
{
    return g_session_gid;
}

const char *user_session_name(void)
{
    static char name[USER_NAME_MAX];
    user_entry_t e;
    if (user_lookup_by_uid(g_session_uid, &e)) {
        strlcpy(name, e.username, sizeof(name));
        return name;
    }
    strlcpy(name, "root", sizeof(name));
    return name;
}
