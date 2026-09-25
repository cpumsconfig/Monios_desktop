/* ============================================================
 *  Monios 审计子系统实现
 *  kernel/security/audit.c
 *
 *  - 事件先写入 4KB 暂存缓冲，满 4KB 或调用 audit_flush() 时落盘。
 *  - 落盘策略：读出旧日志 -> 拼接新行 -> 整文件写回。
 *    日志超过 AUDIT_MAX_FILE (1MB) 时滚动为 audit.log.1。
 *  - 时间戳采用启动后秒数（tick/timer_hz），不依赖 RTC。
 * ============================================================ */

#include "audit.h"
#include "common.h"
#include "file.h"
#include "kernel.h"
#include "string.h"

#define AUDIT_LOG_DIR   "/Monios/System/Logs"
#define AUDIT_LOG_PATH  "/Monios/System/Logs/audit.log"
#define AUDIT_OLD_PATH  "/Monios/System/Logs/audit.log.1"
#define AUDIT_STAGE_CAP 4096U
#define AUDIT_MAX_FILE  (1024U * 1024U)

/* 1MB 整文件缓冲（BSS，仅在落盘时实际触碰）。 */
static uint8_t  g_file_buf[AUDIT_MAX_FILE];
static char     g_stage[AUDIT_STAGE_CAP];
static uint32_t g_stage_len;
static bool     g_inited;

static const char *const g_event_names[] = {
    "LOGIN_OK",
    "LOGIN_FAIL",
    "LOGIN_LOCK",
    "LOGIN_UNLOCK",
    "FILE_OPEN",
    "FILE_DENY",
    "FILE_DEL",
    "ELEVATE",
    "FIREWALL",
    "DRV_LOAD",
    "DRV_UNLOAD",
    "USER_ADD",
    "USER_DEL",
    "PASSWD",
    "OTHER"
};

const char *audit_event_name(audit_event_t event)
{
    uint32_t i = (uint32_t) event;
    if (i >= (sizeof(g_event_names) / sizeof(g_event_names[0]))) {
        return "OTHER";
    }
    return g_event_names[i];
}

static void ensure_log_dir(void)
{
    if (!file_exists(AUDIT_LOG_DIR)) {
        file_mkdir(AUDIT_LOG_DIR);
    }
}

static uint32_t audit_uptime_seconds(void)
{
    uint32_t hz = timer_hz();
    if (hz == 0) {
        return 0;
    }
    return (uint32_t) (timer_ticks() / hz);
}

static void append_uint(char *buf, uint32_t *off, uint32_t cap, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0;
    uint32_t o = *off;
    if (v == 0) {
        if (o + 1 < cap) buf[o++] = '0';
    } else {
        while (v > 0 && n < sizeof(tmp)) {
            tmp[n++] = (char) ('0' + (v % 10));
            v /= 10;
        }
        while (n > 0 && o < cap) {
            buf[o++] = tmp[--n];
        }
    }
    buf[o] = '\0';
    *off = o;
}

static void append_str_safe(char *buf, uint32_t *off, uint32_t cap, const char *s)
{
    uint32_t o = *off;
    if (s == NULL) s = "";
    while (*s != '\0' && o + 1 < cap) {
        char c = *s++;
        /* 防止日志注入：把换行/回车替换为空格 */
        if (c == '\n' || c == '\r') c = ' ';
        buf[o++] = c;
    }
    buf[o] = '\0';
    *off = o;
}

void audit_init(void)
{
    g_stage_len = 0;
    g_stage[0] = '\0';
    ensure_log_dir();
    g_inited = true;
}

void audit_log(audit_event_t event, const char *user, const char *detail, bool result)
{
    char line[256];
    uint32_t off = 0;
    uint32_t line_len;

    if (!g_inited) {
        audit_init();
    }

    /* 格式：[uptime_secs] EVENT user=<user> result=<ok|deny> detail=<text> */
    line[0] = '\0';
    if (off + 2 < sizeof(line)) line[off++] = '[';
    append_uint(line, &off, sizeof(line), audit_uptime_seconds());
    if (off + 2 < sizeof(line)) line[off++] = ']';
    if (off + 2 < sizeof(line)) line[off++] = ' ';
    append_str_safe(line, &off, sizeof(line), audit_event_name(event));
    if (off + 2 < sizeof(line)) line[off++] = ' ';
    append_str_safe(line, &off, sizeof(line), "user=");
    append_str_safe(line, &off, sizeof(line), (user != NULL && user[0] != '\0') ? user : "-");
    if (off + 2 < sizeof(line)) line[off++] = ' ';
    append_str_safe(line, &off, sizeof(line), result ? "result=ok" : "result=deny");
    if (detail != NULL && detail[0] != '\0') {
        if (off + 2 < sizeof(line)) line[off++] = ' ';
        append_str_safe(line, &off, sizeof(line), "detail=");
        append_str_safe(line, &off, sizeof(line), detail);
    }
    line[off++] = '\n';
    line[off] = '\0';
    line_len = off;

    /* 若单条就超过暂存容量，直接落盘（不会经常发生）。 */
    if (line_len >= AUDIT_STAGE_CAP) {
        /* 极端情况：截断到 cap-1 */
        line_len = AUDIT_STAGE_CAP - 1;
        line[line_len - 1] = '\n';
        line[line_len] = '\0';
    }

    if (g_stage_len + line_len + 1 >= AUDIT_STAGE_CAP) {
        audit_flush();
    }
    memcpy(g_stage + g_stage_len, line, line_len);
    g_stage_len += line_len;
    g_stage[g_stage_len] = '\0';
}

void audit_flush(void)
{
    int32_t old_size;
    uint32_t new_size;

    if (g_stage_len == 0) {
        return;
    }
    ensure_log_dir();

    old_size = file_size(AUDIT_LOG_PATH);
    if (old_size < 0) {
        old_size = 0;
    }

    /* 超过上限：滚动。直接把旧文件覆盖为 .1（保留最近一份）。 */
    if ((uint32_t) old_size + g_stage_len > AUDIT_MAX_FILE) {
        if (old_size > 0) {
            int32_t rd = file_read(AUDIT_LOG_PATH, g_file_buf, AUDIT_MAX_FILE);
            if (rd > 0) {
                file_write(AUDIT_OLD_PATH, g_file_buf, (uint32_t) rd);
            }
        }
        file_write(AUDIT_LOG_PATH, g_stage, g_stage_len);
        g_stage_len = 0;
        g_stage[0] = '\0';
        return;
    }

    if (old_size > 0) {
        int32_t rd = file_read(AUDIT_LOG_PATH, g_file_buf, AUDIT_MAX_FILE);
        if (rd < 0) rd = 0;
        new_size = (uint32_t) rd + g_stage_len;
        if (new_size > AUDIT_MAX_FILE) {
            /* 不应该发生，安全截断 */
            new_size = AUDIT_MAX_FILE;
        }
        memcpy(g_file_buf + rd, g_stage, g_stage_len);
        file_write(AUDIT_LOG_PATH, g_file_buf, new_size);
    } else {
        file_write(AUDIT_LOG_PATH, g_stage, g_stage_len);
    }
    g_stage_len = 0;
    g_stage[0] = '\0';
}

bool audit_clear(void)
{
    g_stage_len = 0;
    g_stage[0] = '\0';
    ensure_log_dir();
    file_write(AUDIT_LOG_PATH, "", 0);
    return true;
}

uint32_t audit_dump(char *buffer, uint32_t buffer_size)
{
    int32_t rd;
    uint32_t total;

    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    rd = file_size(AUDIT_LOG_PATH);
    if (rd <= 0) {
        /* 只有暂存内容 */
        total = g_stage_len;
        if (total >= buffer_size) total = buffer_size - 1;
        memcpy(buffer, g_stage, total);
        buffer[total] = '\0';
        return total;
    }
    if ((uint32_t) rd >= buffer_size) {
        /* 只返回尾部 N 行（简化：返回最后 buffer_size-1 字节） */
        uint32_t want = buffer_size - 1;
        int32_t off = rd - (int32_t) want;
        if (off < 0) off = 0;
        int32_t got = file_read_at(AUDIT_LOG_PATH, (uint32_t) off, buffer, want);
        if (got < 0) got = 0;
        /* 暂存追加在后面 */
        uint32_t room = buffer_size - (uint32_t) got;
        uint32_t add = g_stage_len < room ? g_stage_len : room;
        memcpy(buffer + got, g_stage, add);
        buffer[got + add] = '\0';
        return (uint32_t) (got + add);
    }
    rd = file_read(AUDIT_LOG_PATH, buffer, buffer_size - 1);
    if (rd < 0) rd = 0;
    total = (uint32_t) rd;
    if (total < buffer_size) {
        uint32_t room = buffer_size - total - 1;
        uint32_t add = g_stage_len < room ? g_stage_len : room;
        memcpy(buffer + total, g_stage, add);
        total += add;
    }
    buffer[total] = '\0';
    return total;
}
