#include "common.h"
#include "base64.h"
#include "file.h"
#include "hash.h"
#include "kernel.h"
#include "path.h"
#include "session.h"
#include "ui.h"

#define SESSION_AUTH_PATH UI_AUTH_PATH
#define SESSION_AUTH_FILE_MAX 512
#define SESSION_HASH_OUTPUT_MAX 96
#define SESSION_COMBINED_SECRET_MAX 96
#define SESSION_DEFAULT_SALT "monios"
#define SESSION_DEFAULT_HASH "2u07D1xwCZ0h9oLnVxzk7cZwMDDp4oBvm4+JfXFk48Y="
#define SESSION_MAX_FAILED_ATTEMPTS 5U
#define SESSION_LOCKOUT_SECONDS 10U

static session_user_t g_users[SESSION_USER_MAX];
static uint32_t g_user_count;
static uint32_t g_current_user_index;
static char g_default_desktop_app[32];
static char g_default_logon_app[32];
static uint32_t g_auth_failed_attempts;
static bool g_auth_locked;
static uint64_t g_auth_lock_until_tick;

static bool session_verify_password_for_user(const char *username, const char *password);

static bool session_copy_string(char *dst, uint32_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0 || src == NULL) {
        return false;
    }
    return strlcpy(dst, src, dst_size) < dst_size;
}

static void session_add_user(const char *name, const char *home)
{
    if (g_user_count >= SESSION_USER_MAX || name == NULL || home == NULL) {
        return;
    }
    memset(&g_users[g_user_count], 0, sizeof(g_users[g_user_count]));
    if (!session_copy_string(g_users[g_user_count].name, sizeof(g_users[g_user_count].name), name) ||
        !session_copy_string(g_users[g_user_count].home, sizeof(g_users[g_user_count].home), home)) {
        return;
    }
    g_user_count++;
}

static int32_t session_find_user_index(const char *name)
{
    if (name == NULL) {
        return -1;
    }
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].name, name) == 0) {
            return (int32_t) i;
        }
    }
    return -1;
}

static void session_trim_in_place(char *text)
{
    uint32_t start = 0;
    uint32_t end;
    uint32_t length;

    if (text == NULL) {
        return;
    }

    if (strlen(text) >= 3U &&
        (uint8_t) text[0] == 0xEF &&
        (uint8_t) text[1] == 0xBB &&
        (uint8_t) text[2] == 0xBF) {
        start = 3;
    }
    while (text[start] == ' ' || text[start] == '\r' || text[start] == '\n' || text[start] == '\t') {
        start++;
    }

    length = (uint32_t) strlen(text + start);
    if (start > 0 && length > 0) {
        for (uint32_t i = 0; i <= length; i++) {
            text[i] = text[start + i];
        }
    } else if (start > 0) {
        text[0] = '\0';
        return;
    }

    end = (uint32_t) strlen(text);
    while (end > 0) {
        char ch = text[end - 1];
        if (ch != ' ' && ch != '\r' && ch != '\n' && ch != '\t') {
            break;
        }
        end--;
    }
    text[end] = '\0';
}

static bool session_compute_salted_password_hash(const char *salt, const char *password, char *output, uint32_t output_size)
{
    char combined[SESSION_COMBINED_SECRET_MAX];
    uint8_t digest[HASH_SHA256_DIGEST_SIZE];
    uint32_t salt_len = (uint32_t) strlen(salt);
    uint32_t password_len = (uint32_t) strlen(password);

    if (salt_len + password_len + 1 > sizeof(combined)) {
        return false;
    }

    memcpy(combined, salt, salt_len);
    memcpy(combined + salt_len, password, password_len);
    combined[salt_len + password_len] = '\0';
    hash_sha256((const uint8_t *) combined, salt_len + password_len, digest);
    return base64_encode(digest, sizeof(digest), output, output_size) >= 0;
}

static bool session_constant_time_equal(const char *left, const char *right)
{
    uint32_t left_len;
    uint32_t right_len;
    uint32_t limit;
    uint8_t difference = 0;

    if (left == NULL || right == NULL) {
        return false;
    }
    left_len = (uint32_t) strlen(left);
    right_len = (uint32_t) strlen(right);
    limit = left_len > right_len ? left_len : right_len;
    if (limit >= SESSION_HASH_OUTPUT_MAX) {
        return false;
    }
    difference = (uint8_t) (left_len ^ right_len);
    for (uint32_t i = 0; i < limit; i++) {
        uint8_t left_byte = i < left_len ? (uint8_t) left[i] : 0;
        uint8_t right_byte = i < right_len ? (uint8_t) right[i] : 0;

        difference |= (uint8_t) (left_byte ^ right_byte);
    }
    return difference == 0;
}

static void session_clear_auth_lock(void)
{
    g_auth_failed_attempts = 0;
    g_auth_locked = false;
    g_auth_lock_until_tick = 0;
}

static bool session_auth_lock_active(bool emit_log)
{
    if (!g_auth_locked) {
        return false;
    }
    if (g_auth_lock_until_tick == 0 || timer_ticks() >= g_auth_lock_until_tick) {
        session_clear_auth_lock();
        if (emit_log) {
            log_write("auth: lock expired");
        }
        return false;
    }
    if (emit_log) {
        log_write("auth: locked; retry later");
    }
    return true;
}

static void session_note_auth_failure(void)
{
    uint32_t hz;

    if (++g_auth_failed_attempts < SESSION_MAX_FAILED_ATTEMPTS) {
        return;
    }
    hz = timer_hz();
    g_auth_locked = true;
    g_auth_lock_until_tick = timer_ticks() + (uint64_t) (hz == 0 ? 1U : hz) * SESSION_LOCKOUT_SECONDS;
    log_write("auth: locked");
}

static void session_init_defaults(void)
{
    g_user_count = 0;
    g_current_user_index = 0;
    session_clear_auth_lock();
    (void) session_copy_string(g_default_desktop_app, sizeof(g_default_desktop_app), UI_EXPLORER_PATH);
    (void) session_copy_string(g_default_logon_app, sizeof(g_default_logon_app), UI_LOGON_PATH);
    session_add_user("root", UI_ROOT_HOME);
    session_add_user("guest", UI_GUEST_HOME);
    session_add_user("dev", UI_DEV_HOME);
}

void session_init(void)
{
    session_init_defaults();

    ui_ensure_system_layout();
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (!file_exists(g_users[i].home)) {
            file_mkdir(g_users[i].home);
        }
        {
            char desktop[48];
            uint64_t home_len = strlcpy(desktop, g_users[i].home, sizeof(desktop));
            uint64_t suffix_len = strlen(PATH_SEPARATOR_STR "Desktop");

            if (home_len < sizeof(desktop) &&
                home_len + suffix_len + 1 <= sizeof(desktop)) {
                strcpy(desktop + home_len, PATH_SEPARATOR_STR "Desktop");
            } else {
                desktop[0] = '\0';
            }
            if (desktop[0] != '\0' && !file_exists(desktop)) {
                file_mkdir(desktop);
            }
        }
    }
}

uint32_t session_user_count(void)
{
    return g_user_count;
}

const session_user_t *session_user_at(uint32_t index)
{
    if (index >= g_user_count) {
        return NULL;
    }
    return &g_users[index];
}

const session_user_t *session_current_user(void)
{
    if (g_current_user_index >= g_user_count) {
        return NULL;
    }
    return &g_users[g_current_user_index];
}

bool session_login(uint32_t index)
{
    if (index >= g_user_count) {
        return false;
    }
    g_current_user_index = index;
    return true;
}

bool session_login_name(const char *name)
{
    int32_t index = session_find_user_index(name);

    if (index < 0) {
        return false;
    }
    g_current_user_index = (uint32_t) index;
    return true;
}

bool session_validate_credentials(const char *username, const char *password)
{
    if (username == NULL || password == NULL || username[0] == '\0') {
        return false;
    }
    if (session_find_user_index(username) < 0) {
        return false;
    }
    if (!session_verify_password_for_user(username, password)) {
        return false;
    }
    return session_login_name(username);
}

bool session_verify_password(const char *password)
{
    const session_user_t *user = session_current_user();

    return session_verify_password_for_user(user != NULL ? user->name : "root", password);
}

static bool session_verify_password_for_user(const char *username, const char *password)
{
    char entry[SESSION_AUTH_FILE_MAX];
    char line[SESSION_AUTH_FILE_MAX];
    char *credential;
    char computed[SESSION_HASH_OUTPUT_MAX];
    char *delimiter;
    char *salt;
    char *expected_hash;
    int32_t size;
    uint32_t cursor = 0;
    bool candidate_seen = false;

    if (username == NULL || password == NULL || username[0] == '\0') {
        return false;
    }
    if (session_auth_lock_active(true)) {
        return false;
    }

    size = file_read(SESSION_AUTH_PATH, entry, sizeof(entry) - 1);
    if (size <= 0) {
        log_write("auth: pwd.txt missing");
        return false;
    }

    entry[size] = '\0';
    while (cursor < (uint32_t) size) {
        uint32_t line_length = 0;

        while (cursor + line_length < (uint32_t) size &&
               entry[cursor + line_length] != '\n') {
            line_length++;
        }
        if (line_length >= sizeof(line)) {
            while (cursor + line_length < (uint32_t) size &&
                   entry[cursor + line_length] != '\n') {
                line_length++;
            }
            cursor = cursor + line_length < (uint32_t) size ?
                     cursor + line_length + 1U :
                     (uint32_t) size;
            continue;
        }
        memcpy(line, entry + cursor, line_length);
        line[line_length] = '\0';
        session_trim_in_place(line);
        if (cursor + line_length < (uint32_t) size &&
            entry[cursor + line_length] == '\n') {
            cursor += line_length + 1;
        } else {
            cursor = (uint32_t) size;
        }
        if (line[0] == '\0') {
            continue;
        }

        credential = line;
        delimiter = strchr(line, '|');
        if (delimiter != NULL) {
            *delimiter = '\0';
            session_trim_in_place(line);
            if (strcmp(line, username) != 0) {
                continue;
            }
            credential = delimiter + 1;
            session_trim_in_place(credential);
        }
        candidate_seen = true;
        delimiter = strchr(credential, '$');
        if (delimiter == NULL) {
            delimiter = strchr(credential, ':');
        }
        if (delimiter == NULL) {
            continue;
        }
        salt = credential;
        expected_hash = delimiter + 1;
        *delimiter = '\0';
        session_trim_in_place(salt);
        session_trim_in_place(expected_hash);
        if (salt[0] == '\0' || expected_hash[0] == '\0' ||
            !session_compute_salted_password_hash(salt, password, computed, sizeof(computed))) {
            continue;
        }
        if (session_constant_time_equal(expected_hash, computed)) {
            session_clear_auth_lock();
            return true;
        }
    }
    if (!candidate_seen) {
        log_write("auth: pwd.txt empty");
        return false;
    }
    session_note_auth_failure();
    log_write("auth: password rejected");
    return false;
}

bool session_auth_locked(void)
{
    return session_auth_lock_active(false);
}

const char *session_default_desktop_app(void)
{
    return g_default_desktop_app;
}

const char *session_default_logon_app(void)
{
    return g_default_logon_app;
}
