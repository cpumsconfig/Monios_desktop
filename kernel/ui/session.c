#include "common.h"
#include "base64.h"
#include "file.h"
#include "hash.h"
#include "kernel.h"
#include "session.h"

#define SESSION_AUTH_PATH "/pwd.txt"
#define SESSION_AUTH_FILE_MAX 160
#define SESSION_HASH_OUTPUT_MAX 96
#define SESSION_COMBINED_SECRET_MAX 96
#define SESSION_DEFAULT_PASSWORD "123456"
#define SESSION_ROOT_RESCUE_PASSWORD "root"
#define SESSION_DEFAULT_SALT "monios"
#define SESSION_DEFAULT_HASH "2u07D1xwCZ0h9oLnVxzk7cZwMDDp4oBvm4+JfXFk48Y="

static session_user_t g_users[SESSION_USER_MAX];
static uint32_t g_user_count;
static uint32_t g_current_user_index;
static char g_default_desktop_app[32];
static char g_default_logon_app[32];

static void session_add_user(const char *name, const char *home)
{
    if (g_user_count >= SESSION_USER_MAX) {
        return;
    }
    memset(&g_users[g_user_count], 0, sizeof(g_users[g_user_count]));
    strcpy(g_users[g_user_count].name, name);
    strcpy(g_users[g_user_count].home, home);
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

    if ((uint8_t) text[0] == 0xEF && (uint8_t) text[1] == 0xBB && (uint8_t) text[2] == 0xBF) {
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

static bool session_is_default_password(const char *password)
{
    return password != NULL &&
           (strcmp(password, SESSION_DEFAULT_PASSWORD) == 0 ||
            strcmp(password, SESSION_ROOT_RESCUE_PASSWORD) == 0);
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

static void session_init_defaults(void)
{
    g_user_count = 0;
    g_current_user_index = 0;
    strcpy(g_default_desktop_app, "/apps/explorar.exe");
    strcpy(g_default_logon_app, "/apps/monilog.exe");
    session_add_user("root", "/home/root");
    session_add_user("guest", "/home/guest");
    session_add_user("dev", "/home/dev");
}

void session_init(void)
{
    session_init_defaults();

    if (!file_exists("/apps")) {
        file_mkdir("/apps");
    }
    if (!file_exists("/home")) {
        file_mkdir("/home");
    }
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (!file_exists(g_users[i].home)) {
            file_mkdir(g_users[i].home);
        }
        {
            char desktop[48];
            strcpy(desktop, g_users[i].home);
            strcpy(desktop + strlen(desktop), "/desktop");
            if (!file_exists(desktop)) {
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
    if (!session_verify_password(password)) {
        return false;
    }
    return session_login_name(username);
}

bool session_verify_password(const char *password)
{
    char entry[SESSION_AUTH_FILE_MAX];
    char computed[SESSION_HASH_OUTPUT_MAX];
    char *delimiter;
    char *salt;
    char *expected_hash;
    int32_t size;

    if (password == NULL) {
        return false;
    }

    size = file_read(SESSION_AUTH_PATH, entry, sizeof(entry) - 1);
    if (size <= 0) {
        if (session_is_default_password(password)) {
            log_write("auth: pwd.txt missing, default password accepted");
            return true;
        }
        log_write("auth: pwd.txt missing");
        return false;
    }

    entry[size] = '\0';
    session_trim_in_place(entry);
    if (entry[0] == '\0') {
        if (session_is_default_password(password)) {
            log_write("auth: pwd.txt empty, default password accepted");
            return true;
        }
        log_write("auth: pwd.txt empty");
        return false;
    }

    delimiter = strchr(entry, '$');
    if (delimiter == NULL) {
        delimiter = strchr(entry, ':');
    }

    salt = entry;
    expected_hash = entry;
    if (delimiter != NULL) {
        *delimiter = '\0';
        expected_hash = delimiter + 1;
    } else {
        salt = "";
    }

    session_trim_in_place(salt);
    session_trim_in_place(expected_hash);
    if (expected_hash[0] == '\0') {
        if (session_is_default_password(password)) {
            log_write("auth: pwd.txt hash empty, default password accepted");
            return true;
        }
        log_write("auth: pwd.txt hash empty");
        return false;
    }
    if (delimiter == NULL && strcmp(expected_hash, password) == 0) {
        return true;
    }
    if (!session_compute_salted_password_hash(salt, password, computed, sizeof(computed))) {
        if (session_is_default_password(password) &&
            strcmp(salt, SESSION_DEFAULT_SALT) == 0 &&
            strcmp(expected_hash, SESSION_DEFAULT_HASH) == 0) {
            log_write("auth: default password accepted");
            return true;
        }
        log_write("auth: hash compute failed");
        return false;
    }
    if (strcmp(expected_hash, computed) != 0) {
        if (session_is_default_password(password) &&
            strcmp(salt, SESSION_DEFAULT_SALT) == 0 &&
            strcmp(expected_hash, SESSION_DEFAULT_HASH) == 0) {
            log_write("auth: default password accepted");
            return true;
        }
        log_write("auth: password rejected");
        return false;
    }
    return true;
}

const char *session_default_desktop_app(void)
{
    return g_default_desktop_app;
}

const char *session_default_logon_app(void)
{
    return g_default_logon_app;
}
