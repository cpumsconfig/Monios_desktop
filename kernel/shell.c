#include "ahci.h"
#include "aac.h"
#include "base64.h"
#include "bios.h"
#include "bitmap.h"
#include "bluetooth.h"
#include "browser.h"
#include "bsod.h"
#include "buddy.h"
#include "common.h"
#include "console.h"
#include "cpu.h"
#include "device.h"
#include "dns.h"
#include "eevdf.h"
#include "extfs.h"
#include "exec.h"
#include "file.h"
#include "frame.h"
#include "fs_cache.h"
#include "futex.h"
#include "gop.h"
#include "gpu.h"
#include "graphics.h"
#include "gui.h"
#include "hash.h"
#include "hda.h"
#include "heap.h"
#include "hid.h"
#include "http.h"
#include "iic.h"
#include "ide.h"
#include "ipc.h"
#include "ipv4.h"
#include "ipv6.h"
#include "lazyalloc.h"
#include "kernel.h"
#include "lwip.h"
#include "memory.h"
#include "muqss.h"
#include "net.h"
#include "ntfs.h"
#include "nvme.h"
#include "path.h"
#include "pcb.h"
#include "pcnet.h"
#include "pool.h"
#include "power.h"
#include "prsys.h"
#include "rtc.h"
#include "scheduler.h"
#include "schedopt.h"
#include "session.h"
#include "shell.h"
#include "signal.h"
#include "smp.h"
#include "socket.h"
#include "storage_ext.h"
#include "tcb.h"
#include "string.h"
#include "terminal.h"
#include "tls.h"
#include "usb_ext.h"
#include "vma.h"
#include "vmext.h"
#include "wifi.h"
#include "xhci.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX  8
#define SHELL_HISTORY_MAX 16
#define SHELL_ENV_MAX 32
#define SHELL_ENV_LEN 128
#define SHELL_STREAM_MAX 2048
#define SHELL_PIPE_MAX 4
#define SHELL_GLOB_MAX 64
#define SHELL_GLOB_PATH_MAX 256
#define SHELL_COMPLETE_MAX 64
#define SHELL_COMPLETE_PATH_MAX 256
#define SHELL_GREP_LINE_MAX 512
#define SHELL_DEFAULT_LINES 10

typedef enum {
    SHELL_STATE_COMMAND = 0,
    SHELL_STATE_SU_PASSWORD,
    SHELL_STATE_SUDO_PASSWORD
} shell_state_t;

static shell_privilege_t g_shell_privilege;
static shell_state_t g_shell_state;
static char g_line[SHELL_LINE_MAX];
static uint32_t g_line_len;
static char g_password[SHELL_LINE_MAX];
static uint32_t g_password_len;
static char g_pending_sudo[SHELL_LINE_MAX];
static char g_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static uint32_t g_history_count;
static int32_t g_history_index;
static uint32_t g_cursor_pos;
static uint32_t g_last_drawn_len;
static char g_draft[SHELL_LINE_MAX];
static char g_shell_cwd[PATH_MAX_LEN];
static char g_env[SHELL_ENV_MAX][SHELL_ENV_LEN];
static uint32_t g_env_count;
static char *g_capture_buffer;
static uint32_t g_capture_size;
static uint32_t g_capture_len;
static const char *g_pipe_input;

/* Tab completion state */
static char g_complete_candidates[SHELL_COMPLETE_MAX][SHELL_COMPLETE_PATH_MAX];
static uint32_t g_complete_count;
static uint32_t g_complete_index;
static char g_complete_base[SHELL_LINE_MAX];
static uint32_t g_complete_base_len;
static bool g_complete_in_progress;

#define SHELL_AUTH_PATH             "/pwd.txt"
#define SHELL_AUTH_FILE_MAX         256
#define SHELL_HASH_TEXT_MAX         256
#define SHELL_HASH_OUTPUT_MAX       128
#define SHELL_BASE64_OUTPUT_MAX     384
#define SHELL_COMBINED_SECRET_MAX   256
#define SHELL_PRINT_BUFFER_MAX      64
#define SHELL_DEFAULT_DRIVE         "C:"

static void shell_print_line(const char *text);
static void shell_run_command(char *line, bool elevated_once);
static void shell_redraw_input_line(void);
static void shell_print_prompt(void);

static void shell_capture_append(const char *text)
{
    uint32_t len;

    if (g_capture_buffer == NULL || g_capture_size == 0 || text == NULL) {
        return;
    }
    len = (uint32_t) strlen(text);
    if (g_capture_len + len >= g_capture_size) {
        len = g_capture_size - g_capture_len - 1;
    }
    if (len > 0) {
        memcpy(g_capture_buffer + g_capture_len, text, len);
        g_capture_len += len;
        g_capture_buffer[g_capture_len] = '\0';
    }
}

bool shell_output_capture_active(void)
{
    return g_capture_buffer != NULL;
}

void shell_output_capture_write(const char *buffer, uint32_t size)
{
    if (g_capture_buffer == NULL || buffer == NULL) {
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        char tmp[2];

        tmp[0] = buffer[i];
        tmp[1] = '\0';
        shell_capture_append(tmp);
        if (buffer[i] == '\n') {
            terminal_note_output_line();
        }
    }
}

/* ==================== GLOB EXPANSION ==================== */

static bool shell_glob_match(const char *pattern, const char *name)
{
    const char *p = pattern;
    const char *n = name;

    /* skip leading asterisk */
    while (*p == '*') p++;

    /* If pattern ends after stars, match everything */
    if (*p == '\0') {
        return true;
    }

    /* match character by character, stars match any segment */
    while (*n != '\0') {
        if (*p == '*') {
            if (shell_glob_match(p, n + 1)) {
                return true;
            }
            n++;
        } else if (*p == *n) {
            p++;
            n++;
        } else {
            return false;
        }
    }

    while (*p == '*') p++;

    return *p == '\0';
}

static void shell_glob_expand_in_dir(
    const char *dir,
    const char *pattern,
    char results[SHELL_GLOB_MAX][SHELL_GLOB_PATH_MAX],
    uint32_t *result_count
)
{
    char search_path[PATH_MAX_LEN];
    char dir_buffer[2048];
    uint32_t start = 0;
    uint32_t i;

    if (*result_count >= SHELL_GLOB_MAX) return;

    if (dir == NULL || dir[0] == '\0') {
        strcpy(search_path, "/");
    } else {
        strcpy(search_path, dir);
    }

    if (!file_list_dir(search_path, dir_buffer, sizeof(dir_buffer))) {
        return;
    }

    for (i = 0; dir_buffer[i] != '\0'; i++) {
        if (dir_buffer[i] == '\n') {
            char entry_name[64];
            uint32_t len = i - start;

            if (len > 0 && len < sizeof(entry_name)) {
                memcpy(entry_name, dir_buffer + start, len);
                entry_name[len] = '\0';

                if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
                    start = i + 1;
                    continue;
                }

                if (shell_glob_match(pattern, entry_name)) {
                    char full_path[SHELL_GLOB_PATH_MAX];
                    uint32_t dlen = (uint32_t) strlen(search_path);

                    strcpy(full_path, search_path);
                    if (dlen > 1) {
                        full_path[dlen++] = '/';
                        full_path[dlen] = '\0';
                    }
                    if (dlen + strlen(entry_name) < sizeof(full_path)) {
                        strcpy(full_path + dlen, entry_name);
                        if (*result_count < SHELL_GLOB_MAX) {
                            strcpy(results[*result_count], full_path);
                            (*result_count)++;
                        }
                    }
                }
            }
            start = i + 1;
        }
    }
}

static bool shell_glob_expand(const char *input, char output[SHELL_LINE_MAX], uint32_t *out_len)
{
    char results[SHELL_GLOB_MAX][SHELL_GLOB_PATH_MAX];
    uint32_t result_count = 0;
    const char *star = NULL;
    const char *p = input;

    *out_len = 0;
    output[0] = '\0';

    /* find first glob char */
    while (*p != '\0') {
        if (*p == '*' || *p == '?') {
            star = p;
            break;
        }
        p++;
    }

    if (star == NULL) {
        uint32_t len = (uint32_t) strlen(input);
        if (len >= SHELL_LINE_MAX) len = SHELL_LINE_MAX - 1;
        memcpy(output, input, len);
        output[len] = '\0';
        *out_len = len;
        return true;
    }

    const char *pat_start = star;
    while (pat_start > input && pat_start[-1] != '/' && pat_start[-1] != ':') {
        pat_start--;
    }

    char dir_part[PATH_MAX_LEN];
    char pattern_part[64];
    uint32_t dlen = (uint32_t)(pat_start - input);
    uint32_t plen = (uint32_t)(star - pat_start);

    if (dlen > 0) {
        memcpy(dir_part, input, dlen);
        dir_part[dlen] = '\0';
    } else {
        strcpy(dir_part, "/");
    }

    memcpy(pattern_part, pat_start, plen);
    pattern_part[plen] = '\0';

    shell_glob_expand_in_dir(g_shell_cwd, pattern_part, results, &result_count);

    if (result_count == 0 && dlen == 0) {
        shell_glob_expand_in_dir("/", pattern_part, results, &result_count);
        shell_glob_expand_in_dir("/home", pattern_part, results, &result_count);
        shell_glob_expand_in_dir("/apps", pattern_part, results, &result_count);
    }

    if (result_count == 0) {
        uint32_t len = (uint32_t) strlen(input);
        if (len >= SHELL_LINE_MAX) len = SHELL_LINE_MAX - 1;
        memcpy(output, input, len);
        output[len] = '\0';
        *out_len = len;
        return false;
    }

    char prefix[SHELL_LINE_MAX];
    uint32_t prefix_len = (uint32_t)(pat_start - input);
    if (prefix_len > 0) {
        memcpy(prefix, input, prefix_len);
        prefix[prefix_len] = '\0';
    } else {
        prefix[0] = '\0';
    }

    uint32_t pos = 0;
    uint32_t first_len = (uint32_t) strlen(results[0]);
    if (first_len >= SHELL_LINE_MAX) first_len = SHELL_LINE_MAX - 1;
    memcpy(output, results[0], first_len);
    output[first_len] = '\0';
    *out_len = first_len;

    if (result_count > 1) {
        pos = 0;
        for (uint32_t r = 0; r < result_count && r < SHELL_GLOB_MAX; r++) {
            uint32_t rl = (uint32_t) strlen(results[r]);
            if (pos + rl + 1 >= SHELL_LINE_MAX) break;
            memcpy(output + pos, results[r], rl);
            pos += rl;
            output[pos++] = '\n';
        }
        output[pos] = '\0';
        *out_len = pos;
    }

    return result_count > 0;
}

/* ==================== TAB COMPLETION ==================== */

static void shell_complete_reset(void)
{
    g_complete_count = 0;
    g_complete_index = 0;
    g_complete_base_len = 0;
    g_complete_in_progress = false;
    g_complete_base[0] = '\0';
}

static void shell_complete_add(const char *path)
{
    if (g_complete_count >= SHELL_COMPLETE_MAX) return;
    strcpy(g_complete_candidates[g_complete_count++], path);
}

static void shell_complete_scan_dir(const char *dir, const char *prefix, bool dirs_only)
{
    char buffer[2048];
    uint32_t start = 0;
    uint32_t plen = (uint32_t) strlen(prefix);

    if (!file_list_dir(dir, buffer, sizeof(buffer))) return;

    for (uint32_t i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            char name[64];
            uint32_t len = i - start;
            if (len > 0 && len < sizeof(name)) {
                memcpy(name, buffer + start, len);
                name[len] = '\0';

                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                    start = i + 1;
                    continue;
                }

                if (plen == 0 || (len >= plen && memcmp(name, prefix, plen) == 0)) {
                    char full[SHELL_COMPLETE_PATH_MAX];
                    uint32_t dlen = (uint32_t) strlen(dir);

                    strcpy(full, dir);
                    if (dlen > 1) {
                        full[dlen++] = '/';
                        full[dlen] = '\0';
                    }
                    if (dlen + strlen(name) < sizeof(full)) {
                        strcpy(full + dlen, name);
                        if (!dirs_only || file_is_dir(full)) {
                            shell_complete_add(full);
                        }
                    }
                }
            }
            start = i + 1;
        }
    }
}

static void shell_complete_command(const char *prefix, uint32_t prefix_len)
{
    static const char *commands[] = {
        "help", "whoami", "users", "login", "pwd", "cd", "ls", "cat", "echo", "wc",
        "upper", "lower", "mkdir", "touch", "write", "rm", "rmdir",
        "run", "hash", "base64", "env", "set", "unset", "sudo", "su",
        "exit", "shutdown", "net", "dhcp", "dns", "ipv4", "ipv6",
        "cpu", "fpu", "cpuid", "tcb", "ide", "ahci", "nvme", "hda",
        "aac", "pcnet", "lwip", "xhci", "usbext", "hid", "ntfs", "extfs", "fscache",
        "tls", "ssl", "http", "https", "wifi", "bluetooth", "storagex", "gpu", "gui",
        "browser", "power", "vmext", "schedopt",
        "iic", "bios", "gop", "rtc", "heap", "frame", "vma",
        "lazyalloc", "bitmap", "buddy", "eevdf", "futex", "ipc",
        "muqss", "pcb", "pool", "prsys", "scheduler", "signal",
        "socket", "udp", "ping", "dev",
        "wm", "term", "smp", "taskmgr", "clear", "which", "grep",
        "head", "tail", NULL
    };

    for (uint32_t i = 0; commands[i] != NULL; i++) {
        uint32_t clen = (uint32_t) strlen(commands[i]);
        if (prefix_len == 0 || (clen >= prefix_len && memcmp(commands[i], prefix, prefix_len) == 0)) {
            strcpy(g_complete_candidates[g_complete_count++], commands[i]);
            if (g_complete_count >= SHELL_COMPLETE_MAX) break;
        }
    }
}

static void shell_complete_filename(const char *prefix, uint32_t prefix_len)
{
    char dir[PATH_MAX_LEN];
    char file_prefix[64];

    strcpy(dir, g_shell_cwd);
    file_prefix[0] = '\0';

    if (prefix_len > 0) {
        const char *last_slash = NULL;
        for (uint32_t i = 0; i < prefix_len; i++) {
            if (prefix[i] == '/') last_slash = &prefix[i + 1];
        }

        if (last_slash != NULL) {
            uint32_t dlen = (uint32_t)(last_slash - prefix);
            if (dlen >= sizeof(dir)) dlen = sizeof(dir) - 1;
            memcpy(dir, prefix, dlen);
            dir[dlen] = '\0';

            uint32_t flen = prefix_len - dlen - 1;
            if (flen >= sizeof(file_prefix)) flen = sizeof(file_prefix) - 1;
            memcpy(file_prefix, last_slash + 1, flen);
            file_prefix[flen] = '\0';
        } else {
            file_prefix[0] = '\0';
            uint32_t i = 0;
            while (i < prefix_len && i < sizeof(file_prefix) - 1) {
                file_prefix[i] = prefix[i];
                i++;
            }
            file_prefix[i] = '\0';
        }
    }

    shell_complete_scan_dir(dir, file_prefix, false);
    shell_complete_scan_dir("/apps", file_prefix, false);
    shell_complete_scan_dir("/home", file_prefix, false);
}

static void shell_do_tab_complete(void)
{
    shell_complete_reset();

    uint32_t cursor = g_cursor_pos;
    uint32_t token_start = 0;

    while (cursor > 0 && g_line[cursor - 1] == ' ') {
        cursor--;
    }
    token_start = cursor;
    while (token_start > 0 && g_line[token_start - 1] != ' ') {
        token_start--;
    }

    uint32_t token_len = cursor - token_start;
    char token[SHELL_LINE_MAX];
    if (token_len > 0 && token_len < sizeof(token)) {
        memcpy(token, g_line + token_start, token_len);
        token[token_len] = '\0';
    } else {
        token[0] = '\0';
    }

    uint32_t spaces_before = 0;
    for (uint32_t i = 0; i < token_start; i++) {
        if (g_line[i] == ' ') spaces_before++;
    }

    if (spaces_before == 0 && token_len > 0) {
        shell_complete_command(token, token_len);
    } else {
        shell_complete_filename(token, token_len);
    }

    if (g_complete_count == 0) {
        return;
    }

    if (g_complete_count == 1) {
        const char *match = g_complete_candidates[0];
        uint32_t match_len = (uint32_t) strlen(match);

        if (match_len > 0 && match[match_len - 1] == '/') {
            match_len--;
        }

        bool is_dir = file_is_dir(match);

        if (token_len + match_len + (is_dir ? 2 : 1) >= sizeof(g_line)) {
            return;
        }

        uint32_t i = token_start;
        for (uint32_t j = 0; j < match_len; j++) {
            g_line[i++] = match[j];
        }
        if (is_dir) {
            g_line[i++] = '/';
        }
        g_line[i] = '\0';
        g_line_len = i;
        g_cursor_pos = i;
        shell_redraw_input_line();
    } else {
        shell_print_prompt();
        for (uint32_t i = 0; i < g_complete_count; i++) {
            terminal_note_output_line();
            console_write(g_complete_candidates[i]);
            console_write("\r\n");
        }
        shell_print_prompt();
        console_write(g_line);
        g_last_drawn_len = g_line_len;
        if (graphics_active()) {
            graphics_notify_process_output();
        }
    }
}

/* ==================== GLOB EXPAND ALL ARGV ==================== */

static uint32_t shell_expand_globs_in_argv(uint32_t argc, char *argv[SHELL_ARG_MAX])
{
    char expanded[SHELL_ARG_MAX][SHELL_LINE_MAX];
    char results[SHELL_GLOB_MAX][SHELL_GLOB_PATH_MAX];
    uint32_t new_argc = 0;
    uint32_t result_count = 0;
    bool has_glob = false;

    for (uint32_t i = 0; i < argc; i++) {
        uint32_t out_len = 0;
        has_glob = false;

        for (const char *p = argv[i]; *p != '\0'; p++) {
            if (*p == '*' || *p == '?') {
                has_glob = true;
                break;
            }
        }

        if (!has_glob) {
            strcpy(expanded[new_argc++], argv[i]);
            continue;
        }

        shell_glob_expand(argv[i], expanded[i], &out_len);

        if (out_len == 0) {
            continue;
        }

        result_count = 0;
        uint32_t pos = 0;
        for (uint32_t j = 0; j < out_len && j < SHELL_LINE_MAX; j++) {
            if (expanded[i][j] == '\n' || j == out_len - 1) {
                uint32_t len = (j == out_len - 1) ? j - pos + 1 : j - pos;
                if (len > 0 && len < SHELL_GLOB_PATH_MAX && result_count < SHELL_GLOB_MAX) {
                    memcpy(results[result_count], expanded[i] + pos, len);
                    results[result_count][len] = '\0';
                    result_count++;
                }
                pos = j + 1;
            }
        }

        if (result_count == 0) {
            strcpy(expanded[new_argc++], argv[i]);
            continue;
        }

        for (uint32_t r = 0; r < result_count && new_argc < SHELL_ARG_MAX - 1; r++) {
            strcpy(expanded[new_argc++], results[r]);
        }
    }

    for (uint32_t i = 0; i < new_argc && i < SHELL_ARG_MAX; i++) {
        strcpy(argv[i], expanded[i]);
    }

    return new_argc;
}

static void shell_env_set_pair(const char *pair)
{
    if (pair == NULL) return;
    char tmp[SHELL_ENV_LEN];
    char *eq = strchr(pair, '=');
    if (eq == NULL) {
        return;
    }
    /* normalize key=value */
    uint32_t len = (uint32_t) strlen(pair);
    if (len >= SHELL_ENV_LEN) return;
    strcpy(tmp, pair);

    /* search existing */
    for (uint32_t i = 0; i < g_env_count; i++) {
        /* compare key only */
        char *k1 = strchr(g_env[i], '=');
        char *k2 = strchr(tmp, '=');
        if (k1 != NULL && k2 != NULL) {
            uint32_t keylen1 = (uint32_t)(k1 - g_env[i]);
            uint32_t keylen2 = (uint32_t)(k2 - tmp);
            if (keylen1 == keylen2 && keylen1 > 0) {
                if (memcmp(g_env[i], tmp, keylen1) == 0) {
                    strcpy(g_env[i], tmp);
                    return;
                }
            }
        }
    }

    if (g_env_count < SHELL_ENV_MAX) {
        strcpy(g_env[g_env_count++], tmp);
    }
}

static void shell_env_unset_key(const char *key)
{
    if (key == NULL) return;
    for (uint32_t i = 0; i < g_env_count; i++) {
        char *eq = strchr(g_env[i], '=');
        if (eq == NULL) continue;
        uint32_t keylen = (uint32_t)(eq - g_env[i]);
        if ((uint32_t) strlen(key) == keylen && keylen > 0 && memcmp(g_env[i], key, keylen) == 0) {
            /* remove by shifting */
            for (uint32_t j = i + 1; j < g_env_count; j++) {
                strcpy(g_env[j - 1], g_env[j]);
            }
            g_env_count--;
            return;
        }
    }
}

static void shell_write_prompt_prefix(void)
{
    if (g_shell_privilege == SHELL_PRIV_R0) {
        console_write("[R0] ");
    } else {
        console_write("[R3] ");
    }
    console_write(SHELL_DEFAULT_DRIVE);
    console_write(g_shell_cwd);
    console_write(" $ ");
}

static void shell_print_cwd(void)
{
    char text[PATH_MAX_LEN + 3];

    strcpy(text, SHELL_DEFAULT_DRIVE);
    strcpy(text + 2, g_shell_cwd);
    shell_print_line(text);
}

static void shell_redraw_input_line(void)
{
    console_write("\r");
    shell_write_prompt_prefix();
    console_write(g_line);
    for (uint32_t i = g_line_len; i <= g_last_drawn_len; i++) {
        console_write(" ");
    }
    console_write("\r");
    shell_write_prompt_prefix();
    for (uint32_t i = 0; i < g_cursor_pos; i++) {
        console_write_char(g_line[i]);
    }
    g_last_drawn_len = g_line_len;
    if (graphics_active()) {
        graphics_notify_process_output();
    }
}

static void shell_history_push(const char *line)
{
    if (line[0] == '\0') {
        return;
    }

    if (g_history_count < SHELL_HISTORY_MAX) {
        strcpy(g_history[g_history_count++], line);
    } else {
        for (uint32_t i = 1; i < SHELL_HISTORY_MAX; i++) {
            strcpy(g_history[i - 1], g_history[i]);
        }
        strcpy(g_history[SHELL_HISTORY_MAX - 1], line);
    }
    g_history_index = (int32_t) g_history_count;
}

static void shell_history_load(int32_t index)
{
    if (index < 0 || index >= (int32_t) g_history_count) {
        return;
    }
    strcpy(g_line, g_history[index]);
    g_line_len = (uint32_t) strlen(g_line);
    g_cursor_pos = g_line_len;
    shell_redraw_input_line();
}

static void shell_delete_char(void)
{
    if (g_cursor_pos >= g_line_len) {
        return;
    }
    for (uint32_t i = g_cursor_pos; i < g_line_len; i++) {
        g_line[i] = g_line[i + 1];
    }
    g_line_len--;
    shell_redraw_input_line();
}

static void shell_kill_to_start(void)
{
    if (g_cursor_pos == 0) {
        return;
    }
    for (uint32_t i = 0; i + g_cursor_pos <= g_line_len; i++) {
        g_line[i] = g_line[i + g_cursor_pos];
    }
    g_line_len -= g_cursor_pos;
    g_cursor_pos = 0;
    shell_redraw_input_line();
}

static void shell_kill_to_end(void)
{
    if (g_cursor_pos >= g_line_len) {
        return;
    }
    g_line[g_cursor_pos] = '\0';
    g_line_len = g_cursor_pos;
    shell_redraw_input_line();
}

static void shell_print_prompt(void)
{
    console_write("\r\n");
    shell_write_prompt_prefix();
    if (graphics_active()) {
        graphics_notify_process_output();
    }
}

static void shell_print_line(const char *text)
{
    terminal_note_output_line();
    if (g_capture_buffer != NULL) {
        shell_capture_append(text);
        shell_capture_append("\n");
        return;
    }
    if (graphics_active()) {
        console_write(text);
        console_write("\r\n");
        graphics_notify_process_output();
        return;
    }
    console_write(text);
    console_write("\r\n");
}

static bool shell_is_whitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static bool shell_is_printable_text(const uint8_t *buffer, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\r' || buffer[i] == '\t') {
            continue;
        }
        if (buffer[i] < 32 || buffer[i] > 126) {
            return false;
        }
    }
    return true;
}

static void shell_trim_in_place(char *text)
{
    uint32_t len = (uint32_t) strlen(text);
    uint32_t start = 0;
    uint32_t end = len;

    while (start < len && shell_is_whitespace(text[start])) {
        start++;
    }
    while (end > start && shell_is_whitespace(text[end - 1])) {
        end--;
    }

    if (start > 0) {
        uint32_t out_len = end - start;
        for (uint32_t i = 0; i < out_len; i++) {
            text[i] = text[start + i];
        }
        text[out_len] = '\0';
        return;
    }

    text[end] = '\0';
}

static bool shell_join_args(uint32_t argc, char *argv[SHELL_ARG_MAX], uint32_t start_index, char *output, uint32_t output_size)
{
    uint32_t pos = 0;

    if (start_index >= argc || output_size == 0) {
        return false;
    }

    output[0] = '\0';
    for (uint32_t i = start_index; i < argc; i++) {
        uint32_t j = 0;

        if (i > start_index) {
            if (pos + 1 >= output_size) {
                return false;
            }
            output[pos++] = ' ';
        }
        while (argv[i][j] != '\0') {
            if (pos + 1 >= output_size) {
                return false;
            }
            output[pos++] = argv[i][j++];
        }
    }
    output[pos] = '\0';
    return true;
}

static bool shell_parse_u32_arg(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    uint32_t base = 10;
    uint32_t index = 0;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    }
    for (; text[index] != '\0'; index++) {
        char ch = text[index];

        if (base == 10) {
            if (ch < '0' || ch > '9') {
                return false;
            }
            value = value * 10u + (uint32_t) (ch - '0');
        } else {
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value |= (uint32_t) (ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= (uint32_t) (ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value |= (uint32_t) (ch - 'A' + 10);
            } else {
                return false;
            }
        }
    }
    *out = value;
    return true;
}

static bool shell_parse_u16_arg(const char *text, uint16_t *out)
{
    uint32_t value;

    if (!shell_parse_u32_arg(text, &value) || value > 65535) {
        return false;
    }
    *out = (uint16_t) value;
    return true;
}

static bool shell_parse_u64_arg(const char *text, uint64_t *out)
{
    uint64_t value = 0;
    uint32_t base = 10;
    uint32_t index = 0;

    if (text == NULL || text[0] == '\0' || out == NULL) {
        return false;
    }
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        index = 2;
    }
    for (; text[index] != '\0'; index++) {
        char ch = text[index];

        if (base == 10) {
            if (ch < '0' || ch > '9') {
                return false;
            }
            value = value * 10u + (uint64_t) (ch - '0');
        } else {
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value |= (uint64_t) (ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= (uint64_t) (ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value |= (uint64_t) (ch - 'A' + 10);
            } else {
                return false;
            }
        }
    }
    *out = value;
    return true;
}

static bool shell_compute_salted_password_hash(const char *salt, const char *password, char *output, uint32_t output_size)
{
    char combined[SHELL_COMBINED_SECRET_MAX];
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

static bool shell_authenticate_password(const char *password)
{
    char entry[SHELL_AUTH_FILE_MAX];
    char computed[SHELL_HASH_OUTPUT_MAX];
    char *delimiter;
    char *salt;
    char *expected_hash;
    int32_t size = file_read(SHELL_AUTH_PATH, entry, sizeof(entry) - 1);

    if (size <= 0) {
        return false;
    }

    entry[size] = '\0';
    shell_trim_in_place(entry);
    if (entry[0] == '\0') {
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

    shell_trim_in_place(salt);
    shell_trim_in_place(expected_hash);
    if (expected_hash[0] == '\0') {
        return false;
    }

    if (!shell_compute_salted_password_hash(salt, password, computed, sizeof(computed))) {
        return false;
    }
    return strcmp(expected_hash, computed) == 0;
}

static bool shell_resolve_path(const char *input, char output[PATH_MAX_LEN])
{
    return path_resolve(g_shell_cwd, input, output, PATH_MAX_LEN);
}

static bool shell_has_suffix(const char *text, const char *suffix)
{
    uint32_t text_len = (uint32_t) strlen(text);
    uint32_t suffix_len = (uint32_t) strlen(suffix);

    if (suffix_len > text_len) {
        return false;
    }
    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

static bool shell_rm_option_is_recursive_force(const char *option)
{
    if (option == NULL || option[0] != '-') {
        return false;
    }
    return strcmp(option, "-rf") == 0 ||
           strcmp(option, "-fr") == 0 ||
           strcmp(option, "-r") == 0 ||
           strcmp(option, "-f") == 0;
}

static bool shell_rm_path_is_root_wildcard(const char *path)
{
    return path != NULL && (strcmp(path, "/*") == 0 || strcmp(path, "/.") == 0);
}

static void shell_append_path(char *out, uint32_t out_size, const char *base, const char *name)
{
    uint32_t len;

    out[0] = '\0';
    if (base == NULL || name == NULL || out_size == 0) {
        return;
    }

    strcpy(out, base);
    len = (uint32_t) strlen(out);
    if (len > 1 && len + 1 < out_size) {
        out[len++] = '/';
        out[len] = '\0';
    }
    if (len == 1 && out[0] == '/') {
        out[1] = '\0';
    }
    if (len + strlen(name) + 1 < out_size) {
        strcpy(out + len, name);
    }
}

static bool shell_remove_recursive(const char *path)
{
    char list[2048];
    uint32_t start = 0;
    bool ok = true;

    if (path == NULL) {
        return false;
    }
    if (!file_is_dir(path)) {
        return file_delete(path);
    }
    if (!file_list_dir(path, list, sizeof(list))) {
        return false;
    }

    for (uint32_t i = 0;; i++) {
        if (list[i] == '\n' || list[i] == '\0') {
            char name[64];
            char child[PATH_MAX_LEN];
            uint32_t len = i - start;

            if (len > 0 && len < sizeof(name)) {
                memcpy(name, list + start, len);
                name[len] = '\0';
                if (name[len - 1] == '/') {
                    name[len - 1] = '\0';
                }
                shell_append_path(child, sizeof(child), path, name);
                if (!shell_remove_recursive(child)) {
                    ok = false;
                }
            }
            if (list[i] == '\0') {
                break;
            }
            start = i + 1;
        }
    }

    if (strcmp(path, "/") != 0 && !file_rmdir(path)) {
        ok = false;
    }
    return ok;
}

static void shell_check_boot_files_or_panic(void)
{
    if (!file_exists("/kernel.bin") && !file_exists("/loader.bin")) {
        bsod_panic("BOOT FILES DELETED", "kernel.bin and loader.bin were removed");
    }
}

static void shell_print_exit_code(int32_t code)
{
    char buffer[SHELL_PRINT_BUFFER_MAX];
    uint32_t index = 0;
    uint32_t start = 0;
    uint32_t value;

    strcpy(buffer, "exit code: ");
    index = (uint32_t) strlen(buffer);

    if (code < 0) {
        buffer[index++] = '-';
        value = (uint32_t) (-code);
    } else {
        value = (uint32_t) code;
    }

    if (value == 0) {
        buffer[index++] = '0';
    } else {
        start = index;
        while (value > 0 && index < sizeof(buffer) - 1) {
            buffer[index++] = (char) ('0' + (value % 10));
            value /= 10;
        }
        for (uint32_t i = 0; i < (index - start) / 2; i++) {
            char tmp = buffer[start + i];
            buffer[start + i] = buffer[index - 1 - i];
            buffer[index - 1 - i] = tmp;
        }
    }

    buffer[index] = '\0';
    shell_print_line(buffer);
}

static void shell_change_directory(const char *input)
{
    char resolved[PATH_MAX_LEN];
    const char *target = input == NULL ? "/" : input;

    if (!path_resolve(g_shell_cwd, target, resolved, sizeof(resolved))) {
        shell_print_line("invalid path");
        return;
    }
    if (!file_is_dir(resolved)) {
        shell_print_line("not a directory");
        return;
    }

    strcpy(g_shell_cwd, resolved);
}

static void shell_expand_argument(const char *in, char *out, uint32_t out_size)
{
    uint32_t oi = 0;
    const char *p = in;

    while (*p != '\0' && oi + 1 < out_size) {
        if (*p == '$') {
            /* support $KEY or ${KEY} */
            const char *start = p + 1;
            const char *end = start;
            char key[SHELL_ENV_LEN];
            uint32_t klen = 0;

            if (*start == '{') {
                start++;
                end = start;
                while (*end != '\0' && *end != '}') end++;
                klen = (uint32_t)(end - start);
                if (*end == '}') p = end + 1; else p = end;
            } else {
                end = start;
                while (*end != '\0' && ((uint8_t)*end >= 'A' && (uint8_t)*end <= 'z') || ((uint8_t)*end == '_') || ((uint8_t)*end >= '0' && (uint8_t)*end <= '9')) end++;
                klen = (uint32_t)(end - start);
                p = end;
            }
            if (klen == 0 || klen >= SHELL_ENV_LEN) {
                continue;
            }
            memcpy(key, start, klen);
            key[klen] = '\0';

            /* lookup */
            bool found = false;
            for (uint32_t i = 0; i < g_env_count; i++) {
                char *eq = strchr(g_env[i], '=');
                if (eq == NULL) continue;
                uint32_t keylen = (uint32_t)(eq - g_env[i]);
                if (keylen == klen && memcmp(g_env[i], key, klen) == 0) {
                    const char *val = eq + 1;
                    while (*val != '\0' && oi + 1 < out_size) {
                        out[oi++] = *val++;
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                /* nothing: skip */
            }
            continue;
        }
        out[oi++] = *p++;
    }
    out[oi] = '\0';
}

static void shell_run_exec_program(const char *path, uint32_t argc, char *argv[SHELL_ARG_MAX])
{
    char resolved[PATH_MAX_LEN];
    char fallback[PATH_MAX_LEN];
    char *program_argv[SHELL_ARG_MAX];
    int32_t exit_code = 0;

    if (!shell_resolve_path(path, resolved)) {
        shell_print_line("invalid program path");
        return;
    }
    if (!file_exists(resolved) || file_is_dir(resolved)) {
        uint32_t len;

        len = (uint32_t) strlen(path);
        if (len + 5 < sizeof(fallback)) {
            strcpy(fallback, path);
            strcpy(fallback + len, ".elf");
            if (shell_resolve_path(fallback, resolved) && file_exists(resolved) && !file_is_dir(resolved)) {
                path = fallback;
            } else {
                shell_print_line("program not found");
                return;
            }
        } else {
            shell_print_line("program not found");
            return;
        }
    }

    program_argv[0] = resolved;
    /* expand args (support $KEY, ${KEY}, embedded) */
    static char expanded[SHELL_ARG_MAX][SHELL_ENV_LEN];
    for (uint32_t i = 1; i < argc; i++) {
        shell_expand_argument(argv[i], expanded[i], sizeof(expanded[i]));
        program_argv[i] = expanded[i];
    }

    /* build env pointer list */
    char *env_ptrs[SHELL_ENV_MAX];
    for (uint32_t i = 0; i < g_env_count && i < SHELL_ENV_MAX; i++) {
        env_ptrs[i] = g_env[i];
    }

    if (!exec_run(resolved, argc, program_argv, g_shell_cwd, env_ptrs, g_env_count, &exit_code)) {
        shell_print_line("exec failed");
        return;
    }
    if (exit_code != 0) {
        shell_print_exit_code(exit_code);
    }
}

static void shell_run_task_manager(void)
{
    char *argv[] = { "taskmgr.elf" };

    shell_run_exec_program("/taskmgr.elf", 1, argv);
}

static void shell_handle_hash_command(uint32_t argc, char *argv[SHELL_ARG_MAX])
{
    char text[SHELL_HASH_TEXT_MAX];
    char output[SHELL_BASE64_OUTPUT_MAX];

    if (argc >= 2 && strcmp(argv[1], "-s") == 0) {
        uint32_t salt_len;
        uint32_t hash_len;

        if (argc < 4) {
            shell_print_line("usage: hash -s <salt> <text>");
            return;
        }
        if (!shell_join_args(argc, argv, 3, text, sizeof(text))) {
            shell_print_line("usage: hash -s <salt> <text>");
            return;
        }
        if (!shell_compute_salted_password_hash(argv[2], text, output, sizeof(output))) {
            shell_print_line("hash failed");
            return;
        }
        salt_len = (uint32_t) strlen(argv[2]);
        hash_len = (uint32_t) strlen(output);
        if (salt_len + hash_len + 2 > sizeof(text)) {
            shell_print_line("hash output too long");
            return;
        }
        strcpy(text, argv[2]);
        text[salt_len] = '$';
        text[salt_len + 1] = '\0';
        strcpy(text + salt_len + 1, output);
        shell_print_line(text);
        return;
    }

    if (!shell_join_args(argc, argv, 1, text, sizeof(text))) {
        shell_print_line("usage: hash <text>");
        return;
    }

    hash_sha256_hex((const uint8_t *) text, (uint32_t) strlen(text), output);
    shell_print_line(output);
}

static void shell_handle_base64_command(uint32_t argc, char *argv[SHELL_ARG_MAX])
{
    char text[SHELL_HASH_TEXT_MAX];
    char encoded[SHELL_BASE64_OUTPUT_MAX];
    uint8_t decoded[SHELL_HASH_TEXT_MAX];
    int32_t decoded_size;

    if (argc < 3) {
        shell_print_line("usage: base64 <enc|dec> <text>");
        return;
    }

    if (strcmp(argv[1], "enc") == 0 || strcmp(argv[1], "encode") == 0) {
        if (!shell_join_args(argc, argv, 2, text, sizeof(text))) {
            shell_print_line("usage: base64 <enc|dec> <text>");
            return;
        }
        if (base64_encode((const uint8_t *) text, (uint32_t) strlen(text), encoded, sizeof(encoded)) < 0) {
            shell_print_line("base64 encode failed");
            return;
        }
        shell_print_line(encoded);
        return;
    }

    if (strcmp(argv[1], "dec") == 0 || strcmp(argv[1], "decode") == 0) {
        if (!shell_join_args(argc, argv, 2, text, sizeof(text))) {
            shell_print_line("usage: base64 <enc|dec> <text>");
            return;
        }
        decoded_size = base64_decode(text, decoded, sizeof(decoded) - 1);
        if (decoded_size < 0) {
            shell_print_line("invalid base64");
            return;
        }
        if (!shell_is_printable_text(decoded, (uint32_t) decoded_size)) {
            shell_print_line("decoded data is not printable text");
            return;
        }
        decoded[decoded_size] = '\0';
        shell_print_line((const char *) decoded);
        return;
    }

    shell_print_line("usage: base64 <enc|dec> <text>");
}

static void shell_insert_char(char ch)
{
    if (g_line_len + 1 >= sizeof(g_line)) {
        return;
    }
    for (uint32_t i = g_line_len; i > g_cursor_pos; i--) {
        g_line[i] = g_line[i - 1];
    }
    g_line[g_cursor_pos++] = ch;
    g_line[++g_line_len] = '\0';
    shell_redraw_input_line();
}

static void shell_backspace_char(void)
{
    if (g_cursor_pos == 0 || g_line_len == 0) {
        return;
    }
    for (uint32_t i = g_cursor_pos - 1; i < g_line_len; i++) {
        g_line[i] = g_line[i + 1];
    }
    g_cursor_pos--;
    g_line_len--;
    shell_redraw_input_line();
}

static bool shell_is_root_only(const char *cmd)
{
    return strcmp(cmd, "mkdir") == 0 ||
           strcmp(cmd, "touch") == 0 ||
           strcmp(cmd, "write") == 0 ||
           strcmp(cmd, "rm") == 0 ||
           strcmp(cmd, "rmdir") == 0;
}

static uint32_t shell_split_args(char *line, char *argv[SHELL_ARG_MAX])
{
    uint32_t argc = 0;
    char *p = line;

    while (*p != '\0' && argc < SHELL_ARG_MAX) {
        /* skip leading spaces */
        while (*p == ' ') p++;
        if (*p == '\0') break;

        /* start of argument */
        argv[argc++] = p;

        char *read = p;
        char *write = p;
        bool quoted = false;

        while (*read != '\0') {
            if (*read == '"') {
                quoted = !quoted;
                read++;
                continue;
            }
            if (!quoted && *read == ' ') {
                /* consume the space and stop argument */
                read++;
                break;
            }
            *write++ = *read++;
        }
        *write = '\0';
        p = read;
    }
    return argc;
}

static bool shell_is_device_path(const char *path)
{
    return path != NULL &&
           ((path[0] == '\\' && path[1] == '\\' && path[2] == '.' && path[3] == '\\') ||
            (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/'));
}

static void shell_capture_begin(char *buffer, uint32_t size)
{
    g_capture_buffer = buffer;
    g_capture_size = size;
    g_capture_len = 0;
    if (buffer != NULL && size > 0) {
        buffer[0] = '\0';
    }
}

static void shell_capture_end(void)
{
    g_capture_buffer = NULL;
    g_capture_size = 0;
    g_capture_len = 0;
}

static bool shell_write_output_target(const char *path, const char *text, bool append)
{
    char resolved[PATH_MAX_LEN];
    uint32_t text_len = (uint32_t) strlen(text);

    if (shell_is_device_path(path)) {
        return device_write(path, text, text_len) >= 0;
    }
    if (!shell_resolve_path(path, resolved)) {
        return false;
    }
    if (append && file_exists(resolved) && !file_is_dir(resolved)) {
        int32_t old_size = file_size(resolved);
        char combined[SHELL_STREAM_MAX];

        if (old_size < 0 || (uint32_t) old_size + text_len >= sizeof(combined)) {
            return false;
        }
        memset(combined, 0, sizeof(combined));
        if (old_size > 0 && file_read(resolved, combined, (uint32_t) old_size) != old_size) {
            return false;
        }
        memcpy(combined + old_size, text, text_len);
        return file_write(resolved, combined, (uint32_t) old_size + text_len) >= 0;
    }
    return file_write(resolved, text, text_len) >= 0;
}

static bool shell_read_input_source(const char *path, char *buffer, uint32_t size)
{
    char resolved[PATH_MAX_LEN];
    int32_t read;

    if (buffer == NULL || size == 0) {
        return false;
    }
    buffer[0] = '\0';
    if (shell_is_device_path(path)) {
        read = device_read(path, buffer, size - 1);
    } else {
        if (!shell_resolve_path(path, resolved)) {
            return false;
        }
        read = file_read(resolved, buffer, size - 1);
    }
    if (read < 0) {
        return false;
    }
    buffer[read] = '\0';
    return true;
}

static void shell_dump_dir(const char *path)
{
    char buffer[2048];
    uint32_t start = 0;
    uint32_t i;

    if (!file_list_dir(path, buffer, sizeof(buffer))) {
        shell_print_line("list failed");
        return;
    }

    for (i = 0; buffer[i] != '\0'; i++) {
        if (buffer[i] == '\n') {
            char line[64];
            uint32_t j = 0;
            while (start < i && j < sizeof(line) - 1) {
                line[j++] = buffer[start++];
            }
            line[j] = '\0';
            if (j > 0) {
                shell_print_line(line);
            }
            start = i + 1;
        }
    }
}

static void shell_cat_file(const char *path)
{
    char buffer[1024];
    int32_t size;

    if (shell_is_device_path(path)) {
        size = device_read(path, buffer, sizeof(buffer) - 1);
    } else {
        size = file_read(path, buffer, sizeof(buffer) - 1);
    }

    if (size < 0) {
        shell_print_line("read failed");
        return;
    }
    buffer[size] = '\0';
    shell_print_line(buffer);
}

static void shell_touch_file(const char *path)
{
    if (file_write(path, "", 0) < 0) {
        shell_print_line("touch failed");
    } else {
        shell_print_line("file created");
    }
}

static void shell_write_file(const char *path, const char *text)
{
    int32_t written;

    if (shell_is_device_path(path)) {
        written = device_write(path, text, (uint32_t) strlen(text));
    } else {
        written = file_write(path, text, (uint32_t) strlen(text));
    }
    if (written < 0) {
        shell_print_line("write failed");
    } else {
        shell_print_line("write ok");
    }
}

static void shell_print_u32_prefixed(const char *prefix, uint32_t value)
{
    char buffer[64];
    uint32_t index = 0;
    uint32_t start;

    strcpy(buffer, prefix);
    index = (uint32_t) strlen(buffer);
    if (value == 0) {
        buffer[index++] = '0';
    } else {
        start = index;
        while (value > 0 && index + 1 < sizeof(buffer)) {
            buffer[index++] = (char) ('0' + (value % 10));
            value /= 10;
        }
        for (uint32_t i = 0; i < (index - start) / 2; i++) {
            char tmp = buffer[start + i];
            buffer[start + i] = buffer[index - 1 - i];
            buffer[index - 1 - i] = tmp;
        }
    }
    buffer[index] = '\0';
    shell_print_line(buffer);
}

static void shell_print_i32_prefixed(const char *prefix, int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        char buffer[64];

        strcpy(buffer, prefix);
        if (strlen(buffer) + 2 < sizeof(buffer)) {
            strcpy(buffer + strlen(buffer), "-");
            prefix = buffer;
        }
        magnitude = (uint32_t) (-(value + 1)) + 1u;
    } else {
        magnitude = (uint32_t) value;
    }
    shell_print_u32_prefixed(prefix, magnitude);
}

static void shell_print_hex_u32_prefixed(const char *prefix, uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[64];
    uint32_t index = 0;

    strcpy(buffer, prefix);
    index = (uint32_t) strlen(buffer);
    buffer[index++] = '0';
    buffer[index++] = 'x';
    for (uint32_t i = 0; i < 8 && index + 1 < sizeof(buffer); i++) {
        buffer[index++] = hex[(value >> ((7 - i) * 4)) & 0xF];
    }
    buffer[index] = '\0';
    shell_print_line(buffer);
}

static void shell_print_u64_prefixed(const char *prefix, uint64_t value)
{
    char buffer[80];
    char temp[20];
    uint32_t index;
    uint32_t count = 0;

    strcpy(buffer, prefix);
    index = (uint32_t) strlen(buffer);
    if (value == 0) {
        buffer[index++] = '0';
    } else {
        while (value > 0 && count < sizeof(temp)) {
            temp[count++] = (char) ('0' + (value % 10));
            value /= 10;
        }
        while (count > 0 && index + 1 < sizeof(buffer)) {
            buffer[index++] = temp[--count];
        }
    }
    buffer[index] = '\0';
    shell_print_line(buffer);
}

static void shell_print_hex_u64_prefixed(const char *prefix, uint64_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[80];
    uint32_t index = 0;

    strcpy(buffer, prefix);
    index = (uint32_t) strlen(buffer);
    buffer[index++] = '0';
    buffer[index++] = 'x';
    for (uint32_t i = 0; i < 16 && index + 1 < sizeof(buffer); i++) {
        buffer[index++] = hex[(value >> ((15 - i) * 4)) & 0xF];
    }
    buffer[index] = '\0';
    shell_print_line(buffer);
}

static uint32_t shell_count_lines(const char *text)
{
    uint32_t lines = 0;
    bool saw_char = false;

    while (text != NULL && *text != '\0') {
        saw_char = true;
        if (*text == '\n') {
            lines++;
        }
        text++;
    }
    if (saw_char && text[-1] != '\n') {
        lines++;
    }
    return lines;
}

static uint32_t shell_count_words(const char *text)
{
    uint32_t words = 0;
    bool in_word = false;

    while (text != NULL && *text != '\0') {
        bool ws = shell_is_whitespace(*text);
        if (!ws && !in_word) {
            words++;
            in_word = true;
        } else if (ws) {
            in_word = false;
        }
        text++;
    }
    return words;
}

static void shell_print_stream_transformed(const char *text, bool upper)
{
    char buffer[SHELL_STREAM_MAX];
    uint32_t i = 0;

    while (text != NULL && text[i] != '\0' && i + 1 < sizeof(buffer)) {
        char ch = text[i];
        if (upper && ch >= 'a' && ch <= 'z') {
            ch = (char) (ch - 'a' + 'A');
        } else if (!upper && ch >= 'A' && ch <= 'Z') {
            ch = (char) (ch - 'A' + 'a');
        }
        buffer[i++] = ch;
    }
    buffer[i] = '\0';
    shell_print_line(buffer);
}

static bool shell_strip_redirection(char *segment,
                                    char *cleaned,
                                    uint32_t cleaned_size,
                                    char **input_path,
                                    char **output_path,
                                    bool *append)
{
    char *argv[SHELL_ARG_MAX];
    uint32_t argc = shell_split_args(segment, argv);
    uint32_t pos = 0;

    cleaned[0] = '\0';
    for (uint32_t i = 0; i < argc; i++) {
        if (strcmp(argv[i], "<") == 0 || strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
            bool is_input = strcmp(argv[i], "<") == 0;
            bool is_append = strcmp(argv[i], ">>") == 0;

            if (i + 1 >= argc) {
                return false;
            }
            if (is_input) {
                *input_path = argv[++i];
            } else {
                *output_path = argv[++i];
                *append = is_append;
            }
            continue;
        }
        if (pos != 0) {
            if (pos + 1 >= cleaned_size) {
                return false;
            }
            cleaned[pos++] = ' ';
        }
        for (uint32_t j = 0; argv[i][j] != '\0'; j++) {
            if (pos + 1 >= cleaned_size) {
                return false;
            }
            cleaned[pos++] = argv[i][j];
        }
        cleaned[pos] = '\0';
    }
    return true;
}

static bool shell_prepare_redirection_tokens(const char *input, char *output, uint32_t output_size)
{
    uint32_t out = 0;
    bool quoted = false;

    if (input == NULL || output == NULL || output_size == 0) {
        return false;
    }
    for (uint32_t i = 0; input[i] != '\0'; i++) {
        char ch = input[i];

        if (ch == '"') {
            quoted = !quoted;
        }
        if (!quoted && (ch == '<' || ch == '>')) {
            if (out + 4 >= output_size) {
                return false;
            }
            output[out++] = ' ';
            output[out++] = ch;
            if (ch == '>' && input[i + 1] == '>') {
                output[out++] = '>';
                i++;
            }
            output[out++] = ' ';
            continue;
        }
        if (out + 1 >= output_size) {
            return false;
        }
        output[out++] = ch;
    }
    output[out] = '\0';
    return true;
}

static void shell_execute_line(char *line, bool elevated_once)
{
    char segments[SHELL_PIPE_MAX][SHELL_LINE_MAX];
    char previous[SHELL_STREAM_MAX];
    char current[SHELL_STREAM_MAX];
    char input_buffer[SHELL_STREAM_MAX];
    uint32_t segment_count = 0;
    uint32_t start = 0;
    bool has_pipe = false;
    char *input_path = NULL;
    char *output_path = NULL;
    bool append = false;

    previous[0] = '\0';
    input_buffer[0] = '\0';

    for (uint32_t i = 0, quoted = 0;; i++) {
        if (line[i] == '"') {
            quoted = !quoted;
        }
        if ((!quoted && line[i] == '|') || line[i] == '\0') {
            uint32_t len = i - start;

            if (segment_count >= SHELL_PIPE_MAX || len >= SHELL_LINE_MAX) {
                shell_print_line("pipeline too long");
                return;
            }
            memcpy(segments[segment_count], line + start, len);
            segments[segment_count][len] = '\0';
            shell_trim_in_place(segments[segment_count]);
            segment_count++;
            if (line[i] == '\0') {
                break;
            }
            has_pipe = true;
            start = i + 1;
        }
    }

    if (segment_count == 1 && strchr(line, '<') == NULL && strchr(line, '>') == NULL) {
        shell_run_command(line, elevated_once);
        return;
    }

    for (uint32_t i = 0; i < segment_count; i++) {
        char segment_copy[SHELL_LINE_MAX];
        char redir_ready[SHELL_LINE_MAX];
        char cleaned[SHELL_LINE_MAX];
        bool capture;

        strcpy(segment_copy, segments[i]);
        if (!shell_prepare_redirection_tokens(segment_copy, redir_ready, sizeof(redir_ready))) {
            shell_print_line("redirection syntax error");
            return;
        }
        input_path = NULL;
        if (i + 1 == segment_count) {
            output_path = NULL;
            append = false;
        }
        if (!shell_strip_redirection(redir_ready, cleaned, sizeof(cleaned), &input_path, &output_path, &append)) {
            shell_print_line("redirection syntax error");
            return;
        }
        capture = has_pipe || i + 1 < segment_count || output_path != NULL;
        if (input_path != NULL) {
            if (!shell_read_input_source(input_path, input_buffer, sizeof(input_buffer))) {
                shell_print_line("input redirection failed");
                return;
            }
            g_pipe_input = input_buffer;
        } else {
            g_pipe_input = previous[0] == '\0' ? NULL : previous;
        }
        if (capture) {
            shell_capture_begin(current, sizeof(current));
        }
        shell_run_command(cleaned, elevated_once);
        if (capture) {
            shell_capture_end();
            strcpy(previous, current);
        }
    }
    g_pipe_input = NULL;
    if (output_path != NULL) {
        if (!shell_write_output_target(output_path, previous, append)) {
            shell_print_line("output redirection failed");
        }
    } else if (has_pipe) {
        console_write(previous);
        if (graphics_active()) {
            graphics_notify_process_output();
        }
    }
}

static void shell_run_command(char *line, bool elevated_once)
{
    char *argv[SHELL_ARG_MAX];
    char resolved_path[PATH_MAX_LEN];
    uint32_t argc;
    bool allow_root = (g_shell_privilege == SHELL_PRIV_R0) || elevated_once;

    argc = shell_split_args(line, argv);
    if (argc == 0) {
        return;
    }

    /* expand glob patterns in arguments */
    argc = shell_expand_globs_in_argv(argc, argv);

    if (strcmp(argv[0], "help") == 0) {
        shell_print_line("help whoami users login pwd cd ls cat echo wc upper lower mkdir touch write rm rmdir run hash base64 env set unset sudo su exit shutdown net dhcp dns ipv4 ipv6 tls ssl http https wifi bluetooth cpu fpu cpuid tcb ide ahci nvme storagex hda aac pcnet lwip xhci usbext hid ntfs extfs fscache iic bios gop rtc heap frame vma lazyalloc vmext bitmap buddy eevdf futex ipc muqss pcb pool prsys scheduler schedopt signal socket udp ping dev wm gui gpu browser power term smp taskmgr clear which grep head tail ver");
        return;
    }

    if (strcmp(argv[0], "ver") == 0) {
        char version_content[SHELL_STREAM_MAX];
        if (shell_read_input_source("C:/version.txt", version_content, sizeof(version_content))) {
            console_write(version_content);
        } else {
            shell_print_line("version.txt not found");
        }
        return;
    }

    if (strcmp(argv[0], "clear") == 0) {
        console_clear();
        shell_print_prompt();
        return;
    }

    if (strcmp(argv[0], "which") == 0) {
        if (argc < 2) {
            shell_print_line("usage: which <command>");
            return;
        }
        static const char *search_paths[] = {
            "/", "/home/root/desktop", "/home", "/apps", "/home/root", NULL
        };
        char found_path[PATH_MAX_LEN];
        bool found = false;
        char resolved[PATH_MAX_LEN];

        for (uint32_t si = 0; search_paths[si] != NULL && !found; si++) {
            char list_buf[2048];
            uint32_t start = 0;

            if (!file_list_dir(search_paths[si], list_buf, sizeof(list_buf))) {
                continue;
            }

            for (uint32_t i = 0; list_buf[i] != '\0' && !found; i++) {
                if (list_buf[i] == '\n') {
                    char name[64];
                    uint32_t len = i - start;
                    if (len > 0 && len < sizeof(name)) {
                        memcpy(name, list_buf + start, len);
                        name[len] = '\0';

                        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                            start = i + 1;
                            continue;
                        }

                        /* check if this file matches the command name */
                        if (strcmp(name, argv[1]) == 0) {
                            uint32_t dlen = (uint32_t) strlen(search_paths[si]);
                            strcpy(found_path, search_paths[si]);
                            if (dlen > 1) {
                                found_path[dlen++] = '/';
                                found_path[dlen] = '\0';
                            }
                            strcpy(found_path + dlen, name);
                            found = true;
                            break;
                        }

                        /* also check .elf suffix */
                        uint32_t name_len = (uint32_t) strlen(name);
                        if (name_len > 4 && strcmp(name + name_len - 4, ".elf") == 0) {
                            char base[64];
                            uint32_t blen = name_len - 4;
                            if (blen >= sizeof(base)) blen = sizeof(base) - 1;
                            memcpy(base, name, blen);
                            base[blen] = '\0';

                            if (strcmp(base, argv[1]) == 0) {
                                uint32_t dlen = (uint32_t) strlen(search_paths[si]);
                                strcpy(found_path, search_paths[si]);
                                if (dlen > 1) {
                                    found_path[dlen++] = '/';
                                    found_path[dlen] = '\0';
                                }
                                strcpy(found_path + dlen, name);
                                found = true;
                                break;
                            }
                        }
                    }
                    start = i + 1;
                }
            }
        }

        if (found) {
            shell_print_line(found_path);
        } else {
            shell_print_line("not found");
        }
        return;
    }

    if (strcmp(argv[0], "grep") == 0) {
        char pattern[SHELL_GREP_LINE_MAX];
        char file_buffer[SHELL_STREAM_MAX];
        const char *content = NULL;
        bool ignore_case = false;

        /* parse options */
        for (uint32_t i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-i") == 0 == 0 || strcmp(argv[i], "--ignore-case") == 0) {
                ignore_case = true;
            } else if (argv[i][0] != '-') {
                if (pattern[0] == '\0') {
                    uint32_t plen = (uint32_t) strlen(argv[i]);
                    if (plen > sizeof(pattern) - 1) plen = sizeof(pattern) - 1;
                    memcpy(pattern, argv[i], plen);
                    pattern[plen] = '\0';
                }
            }
        }

        if (pattern[0] == '\0') {
            shell_print_line("usage: grep [-i] <pattern> [file]");
            return;
        }

        /* read from file if provided */
        if (argc >= 2 && argv[argc - 1][0] != '-' && !file_is_dir(argv[argc - 1])) {
            if (!shell_read_input_source(argv[argc - 1], file_buffer, sizeof(file_buffer))) {
                shell_print_line("grep: file not found");
                return;
            }
            content = file_buffer;
        } else if (g_pipe_input != NULL) {
            content = g_pipe_input;
        } else {
            shell_print_line("usage: grep [-i] <pattern> [file]");
            return;
        }

        /* search line by line */
        char line_buf[SHELL_GREP_LINE_MAX];
        uint32_t li = 0;
        uint32_t pat_len = (uint32_t) strlen(pattern);
        uint32_t content_len = (uint32_t) strlen(content);

        for (uint32_t i = 0; i <= content_len; i++) {
            char ch = (i < content_len) ? content[i] : '\n';
            bool eof = (i >= content_len);

            if (ch == '\n' || eof) {
                line_buf[li] = '\0';

                if (li > 0) {
                    bool match = false;

                    if (!ignore_case) {
                        /* case-sensitive: simple memcmp scan */
                        for (uint32_t pos = 0; pos + pat_len <= li; pos++) {
                            if (memcmp(line_buf + pos, pattern, pat_len) == 0) {
                                match = true;
                                break;
                            }
                        }
                    } else {
                        /* case-insensitive */
                        for (uint32_t pos = 0; pos + pat_len <= li; pos++) {
                            bool ok = true;
                            for (uint32_t k = 0; k < pat_len; k++) {
                                char a = line_buf[pos + k];
                                char b = pattern[k];
                                if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
                                if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
                                if (a != b) { ok = false; break; }
                            }
                            if (ok) { match = true; break; }
                        }
                    }

                    if (match) {
                        shell_print_line(line_buf);
                    }
                }

                li = 0;
                if (eof) break;
                continue;
            }

            if (li < sizeof(line_buf) - 1) {
                line_buf[li++] = ch;
            }
        }
        return;
    }

    if (strcmp(argv[0], "head") == 0 || strcmp(argv[0], "tail") == 0) {
        char file_buffer[SHELL_STREAM_MAX];
        const char *text = NULL;
        uint32_t count = SHELL_DEFAULT_LINES;
        bool is_tail = strcmp(argv[0], "tail") == 0;

        /* parse -n count */
        for (uint32_t i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
                count = 0;
                for (uint32_t j = 0; argv[i + 1][j] != '\0'; j++) {
                    if (argv[i + 1][j] >= '0' && argv[i + 1][j] <= '9') {
                        count = count * 10 + (uint32_t)(argv[i + 1][j] - '0');
                    } else {
                        break;
                    }
                }
                i++;
            } else if (argv[i][0] != '-' && !file_is_dir(argv[i])) {
                if (!shell_read_input_source(argv[i], file_buffer, sizeof(file_buffer))) {
                    shell_print_line("head/tail: file not found");
                    return;
                }
                text = file_buffer;
            }
        }

        if (text == NULL) {
            if (g_pipe_input != NULL) {
                text = g_pipe_input;
            } else {
                shell_print_line("usage: head [-n <count>] [file]");
                shell_print_line("usage: tail [-n <count>] [file]");
                return;
            }
        }

        /* collect all lines first */
        char *lines[256];
        uint32_t line_count = 0;
        uint32_t li = 0;
        char line_buf[SHELL_GREP_LINE_MAX];

        for (uint32_t i = 0; text[i] != '\0' && line_count < 256; i++) {
            if (text[i] == '\n' || i == (uint32_t)strlen(text) - 1) {
                if (i == (uint32_t)strlen(text) - 1 && text[i] != '\n') {
                    if (li < sizeof(line_buf) - 1) line_buf[li++] = text[i];
                }
                line_buf[li] = '\0';

                lines[line_count] = (char *)((uint64_t)line_buf);
                line_count++;
                li = 0;
            } else {
                if (li < sizeof(line_buf) - 1) line_buf[li++] = text[i];
            }
        }

        /* print lines */
        if (!is_tail) {
            for (uint32_t i = 0; i < count && i < line_count; i++) {
                shell_print_line(lines[i]);
            }
        } else {
            uint32_t start = (line_count > count) ? line_count - count : 0;
            for (uint32_t i = start; i < line_count; i++) {
                shell_print_line(lines[i]);
            }
        }
        return;
    }

    if (strcmp(argv[0], "echo") == 0) {
        char text[SHELL_STREAM_MAX];

        if (argc >= 2) {
            if (!shell_join_args(argc, argv, 1, text, sizeof(text))) {
                shell_print_line("echo text too long");
                return;
            }
            shell_print_line(text);
        } else if (g_pipe_input != NULL) {
            shell_print_line(g_pipe_input);
        }
        return;
    }

    if (strcmp(argv[0], "wc") == 0) {
        const char *text = g_pipe_input;
        char file_buffer[SHELL_STREAM_MAX];

        if (argc >= 2) {
            if (!shell_read_input_source(argv[1], file_buffer, sizeof(file_buffer))) {
                shell_print_line("wc read failed");
                return;
            }
            text = file_buffer;
        }
        if (text == NULL) {
            shell_print_line("usage: wc [path]");
            return;
        }
        shell_print_u32_prefixed("lines: ", shell_count_lines(text));
        shell_print_u32_prefixed("words: ", shell_count_words(text));
        shell_print_u32_prefixed("bytes: ", (uint32_t) strlen(text));
        return;
    }

    if (strcmp(argv[0], "upper") == 0 || strcmp(argv[0], "lower") == 0) {
        char text[SHELL_STREAM_MAX];
        bool upper = strcmp(argv[0], "upper") == 0;

        if (argc >= 2) {
            if (!shell_join_args(argc, argv, 1, text, sizeof(text))) {
                shell_print_line("text too long");
                return;
            }
            shell_print_stream_transformed(text, upper);
        } else if (g_pipe_input != NULL) {
            shell_print_stream_transformed(g_pipe_input, upper);
        } else {
            shell_print_line(upper ? "usage: upper [text]" : "usage: lower [text]");
        }
        return;
    }

    if (strcmp(argv[0], "env") == 0) {
        for (uint32_t i = 0; i < g_env_count; i++) {
            shell_print_line(g_env[i]);
        }
        return;
    }

    if (strcmp(argv[0], "set") == 0) {
        if (argc < 2) {
            shell_print_line("usage: set KEY=VALUE");
            return;
        }
        /* cases:
           set KEY=VALUE
           set KEY VALUE
           set KEY = VALUE
        */
        /* find '=' token if it's a separate token */
        int eq_index = -1;
        for (uint32_t i = 1; i < argc; i++) {
            if (strcmp(argv[i], "=") == 0) {
                eq_index = (int)i;
                break;
            }
        }
        if (eq_index >= 0) {
            if (eq_index == 1 && argc >= 4) {
                /* set KEY = VALUE... */
                char pair[SHELL_ENV_LEN];
                uint32_t klen = (uint32_t) strlen(argv[1]);
                /* join the rest as value */
                char val[SHELL_ENV_LEN];
                if (!shell_join_args(argc, argv, (uint32_t) eq_index + 1, val, sizeof(val))) {
                    shell_print_line("set value too long");
                    return;
                }
                if (klen + 1 + strlen(val) < sizeof(pair)) {
                    strcpy(pair, argv[1]);
                    pair[klen] = '=';
                    strcpy(pair + klen + 1, val);
                    shell_env_set_pair(pair);
                    shell_print_line("ok");
                    return;
                }
            }
            shell_print_line("usage: set KEY=VALUE");
            return;
        }

        /* token contains '=' or simple form */
        if (strchr(argv[1], '=') != NULL) {
            shell_env_set_pair(argv[1]);
            shell_print_line("ok");
            return;
        }
        if (argc >= 3) {
            char pair[SHELL_ENV_LEN];
            uint32_t klen = (uint32_t) strlen(argv[1]);
            char val[SHELL_ENV_LEN];
            if (!shell_join_args(argc, argv, 2, val, sizeof(val))) {
                shell_print_line("set value too long");
                return;
            }
            if (klen + 1 + strlen(val) < sizeof(pair)) {
                strcpy(pair, argv[1]);
                pair[klen] = '=';
                strcpy(pair + klen + 1, val);
                shell_env_set_pair(pair);
                shell_print_line("ok");
                return;
            }
        }
        shell_print_line("usage: set KEY=VALUE");
        return;
    }

    if (strcmp(argv[0], "unset") == 0) {
        if (argc < 2) {
            shell_print_line("usage: unset KEY");
            return;
        }
        shell_env_unset_key(argv[1]);
        shell_print_line("ok");
        return;
    }

    if (strcmp(argv[0], "whoami") == 0) {
        const session_user_t *user = session_current_user();

        shell_print_line(user != NULL ? user->name : "unknown");
        shell_print_line(g_shell_privilege == SHELL_PRIV_R0 ? "ring: R0" : "ring: R3");
        return;
    }

    if (strcmp(argv[0], "users") == 0) {
        const session_user_t *current = session_current_user();

        for (uint32_t i = 0; i < session_user_count(); i++) {
            const session_user_t *user = session_user_at(i);
            char line[64];

            if (user != NULL) {
                line[0] = '\0';
                if (current != NULL && strcmp(current->name, user->name) == 0) {
                    strcpy(line, "* ");
                }
                strcat(line, user->name);
                shell_print_line(line);
                shell_print_line(user->home);
            }
        }
        shell_print_u32_prefixed("count: ", session_user_count());
        shell_print_line(current != NULL ? current->name : "unknown");
        return;
    }

    if (strcmp(argv[0], "login") == 0) {
        const session_user_t *user;
        char pair[SHELL_ENV_LEN];

        if (argc < 2) {
            shell_print_line("usage: login <user>");
            return;
        }
        if (!session_login_name(argv[1])) {
            shell_print_line("unknown user");
            return;
        }
        user = session_current_user();
        if (user == NULL) {
            shell_print_line("login failed");
            return;
        }
        strcpy(pair, "USER=");
        strcat(pair, user->name);
        shell_env_set_pair(pair);
        strcpy(pair, "HOME=");
        strcat(pair, user->home);
        shell_env_set_pair(pair);
        strcpy(pair, "PWD=");
        strcat(pair, user->home);
        shell_env_set_pair(pair);
        if (file_is_dir(user->home)) {
            strcpy(g_shell_cwd, user->home);
        }
        shell_print_line(user->name);
        return;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        shell_print_cwd();
        return;
    }

    if (strcmp(argv[0], "cd") == 0) {
        shell_change_directory(argc >= 2 ? argv[1] : "/");
        return;
    }

    if (strcmp(argv[0], "ls") == 0) {
        if (!path_resolve(g_shell_cwd, argc >= 2 ? argv[1] : ".", resolved_path, sizeof(resolved_path))) {
            shell_print_line("invalid path");
            return;
        }
        shell_dump_dir(resolved_path);
        return;
    }

    if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2) {
            if (g_pipe_input != NULL) {
                shell_print_line(g_pipe_input);
            } else {
                shell_print_line("usage: cat <path>");
            }
            return;
        }
        if (shell_is_device_path(argv[1])) {
            shell_cat_file(argv[1]);
            return;
        }
        if (!shell_resolve_path(argv[1], resolved_path)) {
            shell_print_line("invalid path");
            return;
        }
        shell_cat_file(resolved_path);
        return;
    }

    if (strcmp(argv[0], "exit") == 0) {
        g_shell_privilege = SHELL_PRIV_R3;
        shell_print_line("switched to R3");
        return;
    }

    if (strcmp(argv[0], "taskmgr") == 0) {
        shell_run_task_manager();
        return;
    }

    if (strcmp(argv[0], "shutdown") == 0) {
        if (argc >= 2 && strcmp(argv[1], "poweroff") == 0) {
            kernel_request_shutdown();
            shell_print_line("shutdown requested");
        } else if (argc >= 2 && strcmp(argv[1], "reboot") == 0) {
            kernel_request_reboot();
            shell_print_line("reboot requested");
        } else {
            shell_print_line("usage: shutdown [poweroff/reboot]");
        }
        return;
    }

    if (strcmp(argv[0], "ping") == 0) {
        if (argc < 2) {
            shell_print_line("usage: ping [ip/gateway/host]");
        } else if (net_ping(argv[1])) {
            shell_print_line(net_status());
        } else {
            shell_print_line(net_status());
        }
        return;
    }

    if (strcmp(argv[0], "net") == 0) {
        const net_info_t *info = net_info();

        shell_print_line(net_status());
        shell_print_line(info->mac_text);
        shell_print_line(info->ip_text);
        shell_print_line(info->gateway_text);
        shell_print_line(info->dns_text);
        shell_print_line(info->dhcp_configured ? "dhcp: ok" : "dhcp: static");
        return;
    }

    if (strcmp(argv[0], "dns") == 0) {
        uint8_t ip[4];
        char text[16];

        if (argc < 2) {
            shell_print_line("usage: dns <name>");
            return;
        }
        if (dns_resolve_ipv4(argv[1], ip)) {
            ipv4_to_text(ip, text);
            shell_print_line(text);
        } else {
            shell_print_line(dns_status());
        }
        return;
    }

    if (strcmp(argv[0], "ipv4") == 0) {
        uint8_t ip[4];
        char text[16];

        if (argc < 2) {
            const net_info_t *info = net_info();

            shell_print_line(info->ip_text);
            shell_print_line(info->gateway_text);
            shell_print_line(info->dns_text);
            return;
        }
        if (!ipv4_parse(argv[1], ip)) {
            shell_print_line("bad ipv4");
            return;
        }
        ipv4_to_text(ip, text);
        shell_print_line(text);
        if (ipv4_is_loopback(ip)) shell_print_line("loopback");
        if (ipv4_is_private(ip)) shell_print_line("private");
        if (ipv4_is_link_local(ip)) shell_print_line("link-local");
        if (ipv4_is_multicast(ip)) shell_print_line("multicast");
        return;
    }

    if (strcmp(argv[0], "ipv6") == 0) {
        uint8_t ip[16];
        char text[48];

        if (argc < 2) {
            const ipv6_info_t *info = ipv6_info();

            shell_print_line(ipv6_status());
            shell_print_line(info->link_local);
            shell_print_u32_prefixed("parsed: ", info->parsed_count);
            return;
        }
        if (!ipv6_parse(argv[1], ip)) {
            shell_print_line("bad ipv6");
            return;
        }
        ipv6_to_text(ip, text, sizeof(text));
        shell_print_line(text);
        if (ipv6_is_unspecified(ip)) shell_print_line("unspecified");
        if (ipv6_is_loopback(ip)) shell_print_line("loopback");
        if (ipv6_is_link_local(ip)) shell_print_line("link-local");
        if (ipv6_is_multicast(ip)) shell_print_line("multicast");
        if (ipv6_is_unique_local(ip)) shell_print_line("unique-local");
        if (ipv6_is_global_unicast(ip)) shell_print_line("global-unicast");
        if (argc >= 4 && strcmp(argv[2], "prefix") == 0) {
            uint8_t other[16];
            uint32_t prefix = 0;

            if (!ipv6_parse(argv[3], other) ||
                (argc >= 5 && !shell_parse_u32_arg(argv[4], &prefix)) ||
                prefix > 128) {
                shell_print_line("usage: ipv6 <addr> prefix <other> [bits]");
                return;
            }
            if (argc < 5) {
                prefix = 64;
            }
            shell_print_line(ipv6_prefix_match(ip, other, (uint8_t) prefix) ? "prefix: match" : "prefix: no");
        }
        return;
    }

    if (strcmp(argv[0], "tls") == 0 || strcmp(argv[0], "ssl") == 0) {
        const tls_info_t *info = tls_info();

        if (argc >= 2) {
            shell_print_line(tls_probe_server_name(argv[1]) ? tls_status() : tls_status());
            return;
        }
        shell_print_line(tls_status());
        shell_print_line(info->record_layer ? "record: yes" : "record: no");
        shell_print_line(info->x509_parser ? "x509: yes" : "x509: no");
        shell_print_line(info->crypto_backend_ready ? "crypto: yes" : "crypto: pending");
        shell_print_u32_prefixed("probes: ", info->probes);
        return;
    }

    if (strcmp(argv[0], "http") == 0 || strcmp(argv[0], "https") == 0) {
        const http_info_t *info = http_info();

        if (argc >= 3 && strcmp(argv[1], "get") == 0) {
            char request[512];

            if (http_build_get_request(argv[2], argc >= 4 ? argv[3] : "/", request, sizeof(request))) {
                shell_print_line(request);
            } else {
                shell_print_line(http_status());
            }
            return;
        }
        if (argc >= 2) {
            shell_print_line(http_probe_url(argv[1]) ? http_status() : http_status());
            return;
        }
        shell_print_line(http_status());
        shell_print_u32_prefixed("requests: ", info->requests_built);
        shell_print_u32_prefixed("https probes: ", info->https_probes);
        return;
    }

    if (strcmp(argv[0], "browser") == 0) {
        const browser_info_t *info = browser_info();

        if (argc >= 2) {
            shell_print_line(browser_open_url(argv[1]) ? browser_status() : browser_status());
            return;
        }
        shell_print_line(browser_status());
        shell_print_line(info->last_url);
        shell_print_u32_prefixed("pages: ", info->pages_requested);
        return;
    }

    if (strcmp(argv[0], "wifi") == 0) {
        const wifi_info_t *info = wifi_info();

        shell_print_line(wifi_status());
        if (info->present) {
            shell_print_hex_u32_prefixed("device: ", ((uint32_t) info->vendor_id << 16) | info->device_id);
            shell_print_u32_prefixed("bus: ", info->bus);
            shell_print_u32_prefixed("slot: ", info->slot);
            shell_print_u32_prefixed("irq: ", info->irq);
        }
        return;
    }

    if (strcmp(argv[0], "bluetooth") == 0) {
        const bluetooth_info_t *info = bluetooth_info();

        shell_print_line(bluetooth_status());
        shell_print_line(info->usb_transport_ready ? "transport: usb" : "transport: none");
        shell_print_u32_prefixed("controllers: ", info->controllers);
        return;
    }

    if (strcmp(argv[0], "cpu") == 0 || strcmp(argv[0], "fpu") == 0) {
        const cpu_info_t *info = cpu_current_info();

        shell_print_line(info->vendor);
        shell_print_line(info->brand[0] != '\0' ? info->brand : "unknown");
        shell_print_line(info->has_fpu ? "fpu: present" : "fpu: absent");
        shell_print_line(cpu_fpu_enabled() ? "fpu: enabled" : "fpu: disabled");
        shell_print_line(info->has_sse ? "sse: yes" : "sse: no");
        shell_print_line(info->has_sse2 ? "sse2: yes" : "sse2: no");
        shell_print_line(info->has_avx ? "avx: yes" : "avx: no");
        return;
    }

    if (strcmp(argv[0], "cpuid") == 0) {
        cpuid_regs_t regs;
        uint32_t leaf = 0;
        uint32_t subleaf = 0;

        if (argc >= 2) {
            if (!shell_parse_u32_arg(argv[1], &leaf)) {
                shell_print_line("usage: cpuid [leaf] [subleaf]");
                return;
            }
        }
        if (argc >= 3 && !shell_parse_u32_arg(argv[2], &subleaf)) {
            shell_print_line("usage: cpuid [leaf] [subleaf]");
            return;
        }
        cpu_cpuid_query(leaf, subleaf, &regs);
        shell_print_hex_u32_prefixed("eax: ", regs.eax);
        shell_print_hex_u32_prefixed("ebx: ", regs.ebx);
        shell_print_hex_u32_prefixed("ecx: ", regs.ecx);
        shell_print_hex_u32_prefixed("edx: ", regs.edx);
        return;
    }

    if (strcmp(argv[0], "tcb") == 0) {
        tcb_t tcb;

        for (uint32_t i = 0; i < tcb_capacity(); i++) {
            if (tcb_snapshot(i, &tcb) && tcb.used) {
                shell_print_u32_prefixed("id: ", tcb.id);
                shell_print_line(tcb.name != NULL ? tcb.name : "(unnamed)");
                shell_print_u32_prefixed("period: ", tcb.period_ticks);
                shell_print_u32_prefixed("runs: ", tcb.run_count);
            }
        }
        return;
    }

    if (strcmp(argv[0], "ide") == 0) {
        const ide_info_t *info = ide_info();

        shell_print_line(ide_status());
        if (info->present) {
            shell_print_line(info->model);
            shell_print_u32_prefixed("sectors: ", info->sectors);
        }
        return;
    }

    if (strcmp(argv[0], "ahci") == 0) {
        const ahci_info_t *info = ahci_info();

        shell_print_line(ahci_status());
        if (info->present) {
            shell_print_hex_u32_prefixed("abar: ", info->abar);
            shell_print_hex_u32_prefixed("cap: ", info->cap);
            shell_print_hex_u32_prefixed("ports: ", info->ports_implemented);
        }
        return;
    }

    if (strcmp(argv[0], "nvme") == 0) {
        const nvme_info_t *info = nvme_info();

        shell_print_line(nvme_status());
        if (info->present) {
            shell_print_hex_u32_prefixed("mmio: ", info->mmio_base);
            shell_print_hex_u32_prefixed("version: ", info->version);
            shell_print_hex_u32_prefixed("csts: ", info->csts);
            shell_print_u32_prefixed("queue entries: ", info->max_queue_entries);
            shell_print_u32_prefixed("doorbell stride: ", info->doorbell_stride);
        }
        return;
    }

    if (strcmp(argv[0], "storagex") == 0) {
        const storage_ext_info_t *info = storage_ext_info();

        shell_print_line(storage_ext_status());
        shell_print_u32_prefixed("ide: ", info->ide_controllers);
        shell_print_u32_prefixed("sata: ", info->sata_controllers);
        shell_print_u32_prefixed("nvme: ", info->nvme_controllers);
        shell_print_u32_prefixed("scsi: ", info->scsi_controllers);
        shell_print_u32_prefixed("raid: ", info->raid_controllers);
        shell_print_u32_prefixed("other: ", info->other_storage);
        return;
    }

    if (strcmp(argv[0], "hda") == 0) {
        const hda_info_t *info = hda_info();

        shell_print_line(hda_status());
        if (info->present) {
            shell_print_hex_u32_prefixed("mmio: ", info->mmio_base);
            shell_print_hex_u32_prefixed("gcap: ", info->global_cap);
        }
        return;
    }

    if (strcmp(argv[0], "aac") == 0) {
        aac_info_t info;
        char path[PATH_MAX_LEN];

        if (argc < 2) {
            const aac_info_t *last = aac_last_info();

            shell_print_line(aac_status());
            if (last->valid) {
                shell_print_line(last->codec);
                shell_print_u32_prefixed("sample rate: ", last->sample_rate);
                shell_print_u32_prefixed("channels: ", last->channels);
                shell_print_u32_prefixed("frame length: ", last->frame_length);
                shell_print_u32_prefixed("duration ms: ", last->duration_ms);
            }
            return;
        }
        if (!shell_resolve_path(argv[1], path)) {
            shell_print_line("invalid path");
            return;
        }
        if (!aac_probe_file(path, &info)) {
            shell_print_line(aac_status());
            return;
        }
        shell_print_line(aac_status());
        shell_print_line(info.codec);
        shell_print_u32_prefixed("sample rate: ", info.sample_rate);
        shell_print_u32_prefixed("channels: ", info.channels);
        shell_print_u32_prefixed("frame length: ", info.frame_length);
        shell_print_u32_prefixed("frames: ", info.estimated_frames);
        shell_print_u32_prefixed("duration ms: ", info.duration_ms);
        return;
    }

    if (strcmp(argv[0], "pcnet") == 0) {
        const pcnet_info_t *info = pcnet_info();

        shell_print_line(pcnet_status());
        if (info->present) {
            shell_print_line(info->mac_text);
            shell_print_hex_u32_prefixed("io: ", info->io_base);
        }
        return;
    }

    if (strcmp(argv[0], "lwip") == 0) {
        const lwip_info_t *info = lwip_info();
        uint16_t port;

        if (argc >= 5 && strcmp(argv[1], "send") == 0) {
            char payload[SHELL_STREAM_MAX];

            if (!shell_parse_u16_arg(argv[3], &port)) {
                shell_print_line("usage: lwip send <ip/name> <port> <text>");
                return;
            }
            if (!shell_join_args(argc, argv, 4, payload, sizeof(payload))) {
                shell_print_line("lwip payload too long");
                return;
            }
            shell_print_line(lwip_udp_send(argv[2], port, (const uint8_t *) payload, (uint16_t) strlen(payload)) ? lwip_status() : lwip_status());
            return;
        }
        shell_print_line(lwip_status());
        shell_print_line(info->driver);
        shell_print_line(info->netif_up ? "netif: up" : "netif: down");
        shell_print_line(info->ip);
        shell_print_line(info->gateway);
        shell_print_line(info->dns);
        shell_print_line(info->dhcp_configured ? "dhcp: yes" : "dhcp: no");
        shell_print_line(info->dns_configured ? "dns: yes" : "dns: no");
        shell_print_u32_prefixed("udp sockets: ", info->udp_sockets);
        shell_print_u32_prefixed("tcp pcbs: ", info->tcp_pcbs);
        shell_print_u32_prefixed("tx: ", info->tx_packets);
        shell_print_u32_prefixed("rx: ", info->rx_packets);
        return;
    }

    if (strcmp(argv[0], "xhci") == 0) {
        const xhci_info_t *info = xhci_info();

        shell_print_line(xhci_status());
        if (info->present) {
            shell_print_hex_u32_prefixed("mmio: ", info->mmio_base);
            shell_print_hex_u32_prefixed("hci version: ", info->hci_version);
            shell_print_u32_prefixed("cap length: ", info->cap_length);
            shell_print_u32_prefixed("max slots: ", info->max_slots);
            shell_print_u32_prefixed("max ports: ", info->max_ports);
        }
        return;
    }

    if (strcmp(argv[0], "usbext") == 0) {
        const usb_ext_info_t *info = usb_ext_info();

        shell_print_line(usb_ext_status());
        shell_print_line(info->legacy_ready ? "legacy: yes" : "legacy: no");
        shell_print_line(info->native_host_present ? "native: yes" : "native: no");
        shell_print_line(info->xhci_present ? "xhci: yes" : "xhci: no");
        shell_print_u32_prefixed("root ports: ", info->root_ports);
        shell_print_u32_prefixed("max slots: ", info->max_slots);
        return;
    }

    if (strcmp(argv[0], "hid") == 0) {
        const hid_info_t *info = hid_info();

        shell_print_line(hid_status());
        shell_print_line(info->legacy_keyboard ? "legacy keyboard: yes" : "legacy keyboard: no");
        shell_print_line(info->legacy_mouse ? "legacy mouse: yes" : "legacy mouse: no");
        shell_print_line(info->usb_legacy ? "usb legacy: yes" : "usb legacy: no");
        shell_print_line(info->xhci_present ? "xhci: yes" : "xhci: no");
        shell_print_u32_prefixed("key events: ", info->key_events_seen);
        shell_print_i32_prefixed("mouse x: ", info->mouse_x);
        shell_print_i32_prefixed("mouse y: ", info->mouse_y);
        shell_print_u32_prefixed("buttons: ", info->mouse_buttons);
        return;
    }

    if (strcmp(argv[0], "ntfs") == 0) {
        const ntfs_info_t *info = ntfs_info();

        shell_print_line(ntfs_status());
        if (info->present) {
            shell_print_u32_prefixed("volume lba: ", info->volume_lba);
            shell_print_u32_prefixed("bytes/sector: ", info->bytes_per_sector);
            shell_print_u32_prefixed("sectors/cluster: ", info->sectors_per_cluster);
            shell_print_u64_prefixed("total sectors: ", info->total_sectors);
            shell_print_u64_prefixed("mft lcn: ", info->mft_lcn);
            shell_print_u64_prefixed("mftmirr lcn: ", info->mftmirr_lcn);
            shell_print_u32_prefixed("cluster size: ", info->cluster_size);
            shell_print_u32_prefixed("mft record: ", info->mft_record_size);
            shell_print_u32_prefixed("index record: ", info->index_record_size);
            shell_print_u32_prefixed("mft0 lba: ", info->mft0_lba);
            shell_print_line(info->mft0_readable ? "mft0: readable" : "mft0: unreadable");
            shell_print_hex_u32_prefixed("serial low: ", info->serial_low);
            shell_print_hex_u32_prefixed("serial high: ", info->serial_high);
        }
        return;
    }

    if (strcmp(argv[0], "extfs") == 0) {
        const extfs_info_t *info = extfs_info();

        shell_print_line(extfs_status());
        if (info->present) {
            shell_print_u32_prefixed("volume lba: ", info->volume_lba);
            shell_print_u32_prefixed("block size: ", info->block_size);
            shell_print_u32_prefixed("blocks: ", info->blocks_count);
            shell_print_u32_prefixed("inodes: ", info->inodes_count);
            shell_print_u32_prefixed("inode size: ", info->inode_size);
            shell_print_hex_u32_prefixed("compat: ", info->feature_compat);
            shell_print_hex_u32_prefixed("incompat: ", info->feature_incompat);
            shell_print_hex_u32_prefixed("ro compat: ", info->feature_ro_compat);
        }
        return;
    }

    if (strcmp(argv[0], "fscache") == 0) {
        const fs_cache_info_t *info = fs_cache_info();

        if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
            fs_cache_invalidate_all();
            shell_print_line(fs_cache_status());
            return;
        }
        shell_print_line(fs_cache_status());
        shell_print_line(info->enabled ? "enabled: yes" : "enabled: no");
        shell_print_line(file_backend_name());
        shell_print_u32_prefixed("slots: ", info->slots);
        shell_print_u32_prefixed("block size: ", info->block_size);
        shell_print_u32_prefixed("hits: ", info->hits);
        shell_print_u32_prefixed("misses: ", info->misses);
        shell_print_u32_prefixed("fills: ", info->fills);
        shell_print_u32_prefixed("invalidations: ", info->invalidations);
        return;
    }

    if (strcmp(argv[0], "iic") == 0) {
        uint8_t addrs[32];
        uint32_t probe;

        if (argc >= 2 && strcmp(argv[1], "scan") == 0) {
            uint32_t count = iic_scan(addrs, sizeof(addrs));

            shell_print_line(iic_status());
            for (uint32_t i = 0; i < count; i++) {
                shell_print_hex_u32_prefixed("addr: ", addrs[i]);
            }
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "probe") == 0) {
            if (!shell_parse_u32_arg(argv[2], &probe) || probe > 0x7F) {
                shell_print_line("usage: iic probe <addr>");
                return;
            }
            shell_print_line(iic_probe((uint8_t) probe) ? iic_status() : iic_status());
            return;
        }
        shell_print_line(iic_status());
        shell_print_u32_prefixed("adapters: ", iic_info()->adapters);
        shell_print_u32_prefixed("found: ", iic_info()->found_count);
        return;
    }

    if (strcmp(argv[0], "bios") == 0) {
        const bios_info_t *info = bios_info();

        shell_print_line(bios_status());
        shell_print_hex_u32_prefixed("equipment: ", info->equipment_word);
        shell_print_u32_prefixed("conv kb: ", info->conventional_kb);
        shell_print_hex_u32_prefixed("ebda seg: ", info->ebda_segment);
        shell_print_u32_prefixed("com ports: ", info->com_ports);
        return;
    }

    if (strcmp(argv[0], "gop") == 0) {
        const gop_info_t *info = gop_info();

        shell_print_line(gop_status());
        shell_print_line(info->backend);
        shell_print_hex_u32_prefixed("fb: ", info->framebuffer);
        shell_print_u32_prefixed("width: ", info->width);
        shell_print_u32_prefixed("height: ", info->height);
        shell_print_u32_prefixed("pitch: ", info->pitch_bytes);
        return;
    }

    if (strcmp(argv[0], "rtc") == 0) {
        char text[20];

        shell_print_line(rtc_status());
        if (rtc_format_time(text, sizeof(text))) {
            shell_print_line(text);
        }
        return;
    }

    if (strcmp(argv[0], "heap") == 0) {
        const heap_info_t *info = heap_info();

        shell_print_line(heap_status());
        shell_print_hex_u64_prefixed("base: ", info->base);
        shell_print_u64_prefixed("size: ", info->size);
        shell_print_u64_prefixed("used: ", info->used);
        shell_print_u64_prefixed("free: ", info->free_bytes);
        shell_print_u64_prefixed("high water: ", info->high_water_used);
        shell_print_u32_prefixed("allocs: ", info->alloc_count);
        shell_print_u32_prefixed("frees: ", info->free_count);
        return;
    }

    if (strcmp(argv[0], "frame") == 0) {
        const frame_info_t *info = frame_info();
        uint32_t count = 1;

        if (argc >= 2 && strcmp(argv[1], "alloc") == 0) {
            uint64_t base;

            if (argc >= 3 && !shell_parse_u32_arg(argv[2], &count)) {
                shell_print_line("usage: frame alloc [count]");
                return;
            }
            base = frame_alloc(count);
            if (base == 0) {
                shell_print_line(frame_status());
            } else {
                shell_print_hex_u64_prefixed("frame: ", base);
            }
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "free") == 0) {
            uint64_t base;

            if (!shell_parse_u64_arg(argv[2], &base)) {
                shell_print_line("usage: frame free <base> [count]");
                return;
            }
            if (argc >= 4 && !shell_parse_u32_arg(argv[3], &count)) {
                shell_print_line("usage: frame free <base> [count]");
                return;
            }
            shell_print_line(frame_free(base, count) ? frame_status() : frame_status());
            return;
        }
        shell_print_line(frame_status());
        shell_print_u32_prefixed("used: ", info->used_frames);
        shell_print_u32_prefixed("reserved: ", info->reserved_frames);
        shell_print_u32_prefixed("total: ", info->total_frames);
        shell_print_hex_u64_prefixed("last base: ", info->last_base);
        return;
    }

    if (strcmp(argv[0], "vma") == 0) {
        vma_entry_t entry;

        shell_print_line(vma_status());
        for (uint32_t i = 0; i < vma_count(); i++) {
            if (vma_snapshot(i, &entry) && entry.used) {
                shell_print_u32_prefixed("id: ", entry.id);
                shell_print_line(entry.name);
                shell_print_hex_u64_prefixed("base: ", entry.base);
                shell_print_u64_prefixed("size: ", entry.size);
                shell_print_hex_u32_prefixed("flags: ", entry.flags);
            }
        }
        return;
    }

    if (strcmp(argv[0], "lazyalloc") == 0) {
        lazy_region_t region;
        uint64_t addr;

        if (argc >= 3 && strcmp(argv[1], "touch") == 0) {
            if (!shell_parse_u64_arg(argv[2], &addr)) {
                shell_print_line("usage: lazyalloc touch <addr>");
                return;
            }
            shell_print_line(lazyalloc_touch(addr) ? lazyalloc_status() : lazyalloc_status());
            return;
        }
        shell_print_line(lazyalloc_status());
        for (uint32_t i = 0; i < lazyalloc_count(); i++) {
            if (lazyalloc_snapshot(i, &region) && region.used) {
                shell_print_u32_prefixed("id: ", region.id);
                shell_print_hex_u64_prefixed("base: ", region.base);
                shell_print_u32_prefixed("pages: ", region.total_pages);
                shell_print_u32_prefixed("committed: ", region.committed_pages);
            }
        }
        return;
    }

    if (strcmp(argv[0], "vmext") == 0) {
        const vmext_info_t *info = vmext_info();

        shell_print_line(vmext_status());
        shell_print_line(info->paging_active ? "paging: active" : "paging: inactive");
        shell_print_u32_prefixed("vma regions: ", info->vma_regions);
        shell_print_u32_prefixed("lazy regions: ", info->lazy_regions);
        shell_print_hex_u64_prefixed("pml4: ", info->pml4_phys);
        return;
    }

    if (strcmp(argv[0], "bitmap") == 0) {
        const bitmap_stats_t *stats = bitmap_stats();

        shell_print_line(bitmap_status());
        shell_print_u32_prefixed("set ops: ", stats->set_ops);
        shell_print_u32_prefixed("clear ops: ", stats->clear_ops);
        shell_print_u32_prefixed("alloc ops: ", stats->alloc_ops);
        shell_print_u32_prefixed("free ops: ", stats->free_ops);
        return;
    }

    if (strcmp(argv[0], "buddy") == 0) {
        const buddy_info_t *info = buddy_info();
        uint32_t order = 0;

        if (argc >= 2 && strcmp(argv[1], "alloc") == 0) {
            uint64_t base;

            if (argc >= 3 && !shell_parse_u32_arg(argv[2], &order)) {
                shell_print_line("usage: buddy alloc [order]");
                return;
            }
            base = buddy_alloc(order);
            if (base == 0) {
                shell_print_line(buddy_status());
            } else {
                shell_print_hex_u64_prefixed("buddy: ", base);
            }
            return;
        }
        if (argc >= 4 && strcmp(argv[1], "free") == 0) {
            uint64_t base;

            if (!shell_parse_u64_arg(argv[2], &base) || !shell_parse_u32_arg(argv[3], &order)) {
                shell_print_line("usage: buddy free <base> <order>");
                return;
            }
            shell_print_line(buddy_free(base, order) ? buddy_status() : buddy_status());
            return;
        }
        shell_print_line(buddy_status());
        shell_print_u32_prefixed("max order: ", info->max_order);
        shell_print_u32_prefixed("allocs: ", info->alloc_count);
        shell_print_u32_prefixed("frees: ", info->free_count);
        shell_print_u32_prefixed("failures: ", info->failed_allocs);
        return;
    }

    if (strcmp(argv[0], "eevdf") == 0) {
        const eevdf_info_t *info = eevdf_info();

        shell_print_line(eevdf_status());
        shell_print_u32_prefixed("tracked: ", info->tracked_tasks);
        shell_print_u32_prefixed("dispatches: ", info->dispatches);
        shell_print_u64_prefixed("min deadline: ", info->min_deadline);
        shell_print_u64_prefixed("last deadline: ", info->last_deadline);
        return;
    }

    if (strcmp(argv[0], "muqss") == 0) {
        const muqss_info_t *info = muqss_info();

        shell_print_line(muqss_status());
        shell_print_u32_prefixed("tracked: ", info->tracked_tasks);
        shell_print_u32_prefixed("dispatches: ", info->dispatches);
        shell_print_u32_prefixed("active score: ", info->active_score);
        shell_print_u32_prefixed("last task: ", info->last_task);
        return;
    }

    if (strcmp(argv[0], "scheduler") == 0) {
        const scheduler_info_t *info = scheduler_info();

        if (argc >= 3 && strcmp(argv[1], "set") == 0) {
            scheduler_policy_t policy = SCHED_POLICY_RR;

            if (strcmp(argv[2], "rr") == 0) {
                policy = SCHED_POLICY_RR;
            } else if (strcmp(argv[2], "eevdf") == 0) {
                policy = SCHED_POLICY_EEVDF;
            } else if (strcmp(argv[2], "muqss") == 0) {
                policy = SCHED_POLICY_MUQSS;
            } else {
                shell_print_line("usage: scheduler set [rr|eevdf|muqss]");
                return;
            }
            shell_print_line(scheduler_set_policy(policy) ? scheduler_status() : "scheduler: set failed");
            return;
        }
        shell_print_line(scheduler_status());
        shell_print_line(scheduler_policy_name(info->policy));
        shell_print_u32_prefixed("tasks: ", info->task_count);
        shell_print_u32_prefixed("dispatches: ", info->dispatches);
        shell_print_u32_prefixed("last task: ", info->last_task);
        return;
    }

    if (strcmp(argv[0], "pcb") == 0) {
        pcb_t pcb;

        shell_print_line(pcb_status());
        shell_print_u32_prefixed("current pid: ", pcb_current_pid() < 0 ? 0u : (uint32_t) pcb_current_pid());
        for (uint32_t i = 0; i < pcb_capacity(); i++) {
            if (pcb_snapshot(i, &pcb) && pcb.used) {
                shell_print_u32_prefixed("pid: ", pcb.pid);
                shell_print_line(pcb.name);
                shell_print_line(pcb_state_name(pcb.state));
                shell_print_u32_prefixed("signals: ", pcb.pending_signals);
            }
        }
        return;
    }

    if (strcmp(argv[0], "pool") == 0) {
        const pool_stats_t *stats = pool_stats();

        shell_print_line(pool_status());
        shell_print_u32_prefixed("pools: ", stats->pools);
        shell_print_u32_prefixed("slots total: ", stats->slots_total);
        shell_print_u32_prefixed("slots used: ", stats->slots_used);
        shell_print_u32_prefixed("alloc ops: ", stats->alloc_ops);
        shell_print_u32_prefixed("free ops: ", stats->free_ops);
        return;
    }

    if (strcmp(argv[0], "prsys") == 0) {
        const prsys_info_t *info = prsys_info();

        shell_print_line(prsys_status());
        shell_print_u32_prefixed("processes: ", info->processes);
        shell_print_u32_prefixed("tasks: ", info->tasks);
        shell_print_u64_prefixed("uptime ticks: ", info->uptime_ticks);
        shell_print_u32_prefixed("policy id: ", info->scheduler_policy);
        return;
    }

    if (strcmp(argv[0], "futex") == 0) {
        uint64_t addr;
        uint32_t count = 1;

        if (argc >= 4 && strcmp(argv[1], "wait") == 0) {
            uint32_t expected = 0;
            uint32_t timeout = 0;

            if (!shell_parse_u64_arg(argv[2], &addr) || !shell_parse_u32_arg(argv[3], &expected)) {
                shell_print_line("usage: futex wait <addr> <expected> [timeout]");
                return;
            }
            if (argc >= 5 && !shell_parse_u32_arg(argv[4], &timeout)) {
                shell_print_line("usage: futex wait <addr> <expected> [timeout]");
                return;
            }
            shell_print_u32_prefixed("waiters: ", (uint32_t) futex_wait(addr, expected, timeout));
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "wake") == 0) {
            if (!shell_parse_u64_arg(argv[2], &addr)) {
                shell_print_line("usage: futex wake <addr> [count]");
                return;
            }
            if (argc >= 4 && !shell_parse_u32_arg(argv[3], &count)) {
                shell_print_line("usage: futex wake <addr> [count]");
                return;
            }
            shell_print_u32_prefixed("woken: ", futex_wake(addr, count));
            return;
        }
        shell_print_line(futex_status());
        shell_print_u32_prefixed("waiters: ", futex_info()->total_waiters);
        shell_print_u32_prefixed("slots: ", futex_info()->used_slots);
        return;
    }

    if (strcmp(argv[0], "ipc") == 0) {
        int32_t port_id;
        uint32_t port_value;
        char text[SHELL_STREAM_MAX];

        if (argc >= 3 && strcmp(argv[1], "create") == 0) {
            port_id = ipc_port_create(argv[2]);
            if (port_id < 0) {
                shell_print_line(ipc_status());
            } else {
                shell_print_u32_prefixed("port: ", (uint32_t) port_id);
            }
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "close") == 0) {
            if (!shell_parse_u32_arg(argv[2], &port_value)) {
                shell_print_line("usage: ipc close <port>");
                return;
            }
            shell_print_line(ipc_port_close((int32_t) port_value) ? ipc_status() : ipc_status());
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "broadcast") == 0) {
            if (!shell_join_args(argc, argv, 2, text, sizeof(text))) {
                shell_print_line("usage: ipc broadcast <text>");
                return;
            }
            shell_print_u32_prefixed("delivered: ", ipc_broadcast_text(text));
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "peek") == 0) {
            int32_t size;

            if (!shell_parse_u32_arg(argv[2], &port_value)) {
                shell_print_line("usage: ipc peek <port>");
                return;
            }
            size = ipc_peek_text((int32_t) port_value, text, sizeof(text));
            if (size > 0) {
                shell_print_line(text);
            } else {
                shell_print_line(ipc_status());
            }
            return;
        }
        if (argc >= 4 && strcmp(argv[1], "send") == 0) {
            if (!shell_parse_u32_arg(argv[2], &port_value) || !shell_join_args(argc, argv, 3, text, sizeof(text))) {
                shell_print_line("usage: ipc send <port> <text>");
                return;
            }
            port_id = (int32_t) port_value;
            shell_print_line(ipc_send_text(port_id, text) ? ipc_status() : ipc_status());
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "recv") == 0) {
            int32_t size;

            if (!shell_parse_u32_arg(argv[2], &port_value)) {
                shell_print_line("usage: ipc recv <port>");
                return;
            }
            port_id = (int32_t) port_value;
            size = ipc_recv_text(port_id, text, sizeof(text));
            if (size > 0) {
                shell_print_line(text);
            } else {
                shell_print_line(ipc_status());
            }
            return;
        }
        shell_print_line(ipc_status());
        shell_print_u32_prefixed("ports: ", ipc_info()->port_count);
        shell_print_u32_prefixed("sent: ", ipc_info()->sent_count);
        shell_print_u32_prefixed("recv: ", ipc_info()->recv_count);
        shell_print_u32_prefixed("dropped: ", ipc_info()->dropped_count);
        shell_print_u32_prefixed("capacity: ", ipc_info()->max_ports);
        shell_print_u32_prefixed("queue depth: ", ipc_info()->queue_depth);
        shell_print_u32_prefixed("message size: ", ipc_info()->message_size);
        return;
    }

    if (strcmp(argv[0], "signal") == 0) {
        uint32_t pid = 0;
        uint32_t signo = 0;

        if (argc >= 4 && strcmp(argv[1], "send") == 0) {
            if (!shell_parse_u32_arg(argv[2], &pid) || !shell_parse_u32_arg(argv[3], &signo)) {
                shell_print_line("usage: signal send <pid> <signo>");
                return;
            }
            shell_print_line(signal_send((int32_t) pid, (uint8_t) signo) ? signal_status() : signal_status());
            return;
        }
        if (argc >= 3 && strcmp(argv[1], "take") == 0) {
            if (!shell_parse_u32_arg(argv[2], &pid)) {
                shell_print_line("usage: signal take <pid>");
                return;
            }
            shell_print_hex_u32_prefixed("pending: ", signal_take_pending((int32_t) pid));
            return;
        }
        shell_print_line(signal_status());
        shell_print_u32_prefixed("sent: ", signal_info()->sent_count);
        shell_print_u32_prefixed("fetched: ", signal_info()->fetch_count);
        return;
    }

    if (strcmp(argv[0], "socket") == 0) {
        uint32_t handle_value;
        uint16_t port;

        if (argc < 2) {
            shell_print_line(socket_status());
            shell_print_u32_prefixed("open: ", socket_count());
            return;
        }
        if (strcmp(argv[1], "open") == 0) {
            int32_t handle;

            port = 0;
            if (argc >= 3 && !shell_parse_u16_arg(argv[2], &port)) {
                shell_print_line("bad port");
                return;
            }
            handle = socket_udp_open(port);
            if (handle > 0) {
                shell_print_u32_prefixed("socket: ", (uint32_t) handle);
            } else {
                shell_print_line(socket_status());
            }
            return;
        }
        if (strcmp(argv[1], "close") == 0) {
            if (argc < 3 || !shell_parse_u32_arg(argv[2], &handle_value)) {
                shell_print_line("usage: socket close <handle>");
                return;
            }
            shell_print_line(socket_close((int32_t) handle_value) ? socket_status() : socket_status());
            return;
        }
        if (strcmp(argv[1], "send") == 0) {
            char payload[SHELL_STREAM_MAX];

            if (argc < 6 ||
                !shell_parse_u32_arg(argv[2], &handle_value) ||
                !shell_parse_u16_arg(argv[4], &port)) {
                shell_print_line("usage: socket send <handle> <ip/name> <port> <text>");
                return;
            }
            if (!shell_join_args(argc, argv, 5, payload, sizeof(payload))) {
                shell_print_line("socket payload too long");
                return;
            }
            (void) socket_sendto_ipv4((int32_t) handle_value, argv[3], port, (const uint8_t *) payload, (uint16_t) strlen(payload));
            shell_print_line(socket_status());
            return;
        }
        if (strcmp(argv[1], "recv") == 0) {
            char src_ip[16];
            uint16_t src_port = 0;
            uint8_t payload[SOCKET_MAX_PAYLOAD + 1];
            int32_t size;

            if (argc < 3 || !shell_parse_u32_arg(argv[2], &handle_value)) {
                shell_print_line("usage: socket recv <handle>");
                return;
            }
            size = socket_recvfrom_ipv4((int32_t) handle_value, src_ip, &src_port, payload, SOCKET_MAX_PAYLOAD);
            if (size <= 0) {
                shell_print_line(socket_status());
                return;
            }
            payload[size] = '\0';
            shell_print_line(src_ip);
            shell_print_u32_prefixed("port: ", src_port);
            shell_print_line((const char *) payload);
            return;
        }
        shell_print_line("usage: socket [open [port]|send|recv|close]");
        return;
    }

    if (strcmp(argv[0], "udp") == 0) {
        char payload[SHELL_STREAM_MAX];
        uint32_t port = 0;

        if (argc < 4 || strcmp(argv[1], "send") != 0) {
            shell_print_line("usage: udp send <ip> <port> <text>");
            return;
        }
        for (uint32_t i = 0; argv[3][i] != '\0'; i++) {
            if (argv[3][i] < '0' || argv[3][i] > '9') {
                shell_print_line("bad port");
                return;
            }
            port = port * 10u + (uint32_t) (argv[3][i] - '0');
        }
        if (port == 0 || port > 65535) {
            shell_print_line("bad port");
            return;
        }
        if (!shell_join_args(argc, argv, 4, payload, sizeof(payload))) {
            shell_print_line("udp payload too long");
            return;
        }
        if (net_udp_send(argv[2], (uint16_t) port, (const uint8_t *) payload, (uint16_t) strlen(payload))) {
            shell_print_line(net_status());
        } else {
            shell_print_line(net_status());
        }
        return;
    }

    if (strcmp(argv[0], "dhcp") == 0) {
        if (net_dhcp_request()) {
            shell_print_line(net_status());
        } else {
            shell_print_line(net_status());
        }
        return;
    }

    if (strcmp(argv[0], "dev") == 0) {
        char buffer[SHELL_STREAM_MAX];

        if (argc < 2 || strcmp(argv[1], "list") == 0) {
            if (device_list(buffer, sizeof(buffer))) {
                shell_print_line(buffer);
            }
            return;
        }
        if (strcmp(argv[1], "read") == 0 && argc >= 3) {
            if (device_read(argv[2], buffer, sizeof(buffer) - 1) >= 0) {
                shell_print_line(buffer);
            } else {
                shell_print_line("device read failed");
            }
            return;
        }
        shell_print_line("usage: dev [list|read <\\\\.\\name>]");
        return;
    }

    if (strcmp(argv[0], "wm") == 0) {
        shell_print_u32_prefixed("windows: ", graphics_window_count());
        shell_print_u32_prefixed("focused: ", graphics_focused_window_index());
        return;
    }

    if (strcmp(argv[0], "gui") == 0) {
        const gui_info_t *info = gui_info();

        shell_print_line(gui_status());
        shell_print_line(info->app_framework_ready ? "framework: yes" : "framework: no");
        shell_print_u32_prefixed("widgets: ", info->widgets_registered);
        shell_print_u32_prefixed("windows: ", info->windows);
        shell_print_u32_prefixed("focused: ", info->focused);
        return;
    }

    if (strcmp(argv[0], "gpu") == 0) {
        const gpu_info_t *info = gpu_info();

        shell_print_line(gpu_status());
        shell_print_line(info->backend);
        shell_print_u32_prefixed("width: ", info->width);
        shell_print_u32_prefixed("height: ", info->height);
        shell_print_u32_prefixed("bpp: ", info->bpp);
        shell_print_u32_prefixed("pitch: ", info->pitch);
        shell_print_u32_prefixed("submits: ", info->submit_count);
        shell_print_u32_prefixed("presents: ", info->present_count);
        return;
    }

    if (strcmp(argv[0], "power") == 0) {
        const power_info_t *info = power_info();

        shell_print_line(power_status());
        shell_print_line(info->acpi_ready ? "acpi: yes" : "acpi: no");
        shell_print_line(info->power_button_ready ? "button: yes" : "button: no");
        shell_print_line(info->cpu_frequency_detected ? "cpu freq: detected" : "cpu freq: no");
        shell_print_line(info->device_power_ready ? "device pm: yes" : "device pm: no");
        shell_print_u32_prefixed("sci irq: ", info->sci_irq);
        return;
    }

    if (strcmp(argv[0], "term") == 0) {
        const terminal_info_t *term = terminal_info();

        shell_print_line(term->active ? "terminal: active" : "terminal: inactive");
        shell_print_line(term->focused ? "focus: yes" : "focus: no");
        shell_print_line(term->mode);
        shell_print_u32_prefixed("lines: ", term->lines_written);
        return;
    }

    if (strcmp(argv[0], "smp") == 0) {
        const smp_info_t *info = smp_info();

        shell_print_line(info->status);
        shell_print_u32_prefixed("logical processors: ", info->logical_processors);
        shell_print_u32_prefixed("online processors: ", info->online_processors);
        shell_print_line(info->bootstrap_only ? "mode: bootstrap processor only" : "mode: ap scheduler");
        return;
    }

    if (strcmp(argv[0], "schedopt") == 0) {
        const schedopt_info_t *info = schedopt_info();

        shell_print_line(schedopt_status());
        shell_print_line(info->policy);
        shell_print_u32_prefixed("logical processors: ", info->logical_processors);
        shell_print_u32_prefixed("online processors: ", info->online_processors);
        shell_print_u32_prefixed("dispatches: ", info->dispatches);
        shell_print_line(info->load_balancer_ready ? "load balancer: yes" : "load balancer: pending");
        return;
    }

    if (strcmp(argv[0], "su") == 0) {
        shell_print_line("password:");
        g_shell_state = SHELL_STATE_SU_PASSWORD;
        g_password_len = 0;
        g_password[0] = '\0';
        return;
    }

    if (strcmp(argv[0], "sudo") == 0) {
        if (argc < 2) {
            shell_print_line("usage: sudo <command>");
            return;
        }
        memset(g_pending_sudo, 0, sizeof(g_pending_sudo));
        if (!shell_join_args(argc, argv, 1, g_pending_sudo, sizeof(g_pending_sudo))) {
            shell_print_line("sudo command too long");
            return;
        }
        shell_print_line("password:");
        g_shell_state = SHELL_STATE_SUDO_PASSWORD;
        g_password_len = 0;
        g_password[0] = '\0';
        return;
    }

    if (strcmp(argv[0], "hash") == 0) {
        shell_handle_hash_command(argc, argv);
        return;
    }

    if (strcmp(argv[0], "base64") == 0) {
        shell_handle_base64_command(argc, argv);
        return;
    }

    if (strcmp(argv[0], "run") == 0) {
        if (argc < 2) {
            shell_print_line("usage: run <program.elf> [args]");
            return;
        }
        shell_run_exec_program(argv[1], argc - 1, &argv[1]);
        return;
    }

    if (strcmp(argv[0], "rm") == 0 &&
        argc >= 3 &&
        shell_rm_option_is_recursive_force(argv[1]) &&
        shell_rm_path_is_root_wildcard(argv[2])) {
        shell_print_line("rm: recursive root delete requested");
        shell_remove_recursive("/");
        shell_check_boot_files_or_panic();
        shell_print_line("rm -rf ok");
        return;
    }

    if (shell_is_root_only(argv[0]) && !allow_root) {
        shell_print_line("permission denied (need R0)");
        return;
    }

    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2) {
            shell_print_line("usage: mkdir <path>");
        } else if (shell_resolve_path(argv[1], resolved_path) && file_mkdir(resolved_path)) {
            shell_print_line("mkdir ok");
        } else {
            shell_print_line("mkdir failed");
        }
        return;
    }

    if (strcmp(argv[0], "touch") == 0) {
        if (argc < 2) {
            shell_print_line("usage: touch <path>");
        } else {
            if (!shell_resolve_path(argv[1], resolved_path)) {
                shell_print_line("invalid path");
                return;
            }
            shell_touch_file(resolved_path);
        }
        return;
    }

    if (strcmp(argv[0], "write") == 0) {
        char text[256];

        if (argc < 3) {
            if (argc >= 2 && g_pipe_input != NULL) {
                shell_write_file(argv[1], g_pipe_input);
            } else {
                shell_print_line("usage: write <path> <text>");
            }
            return;
        }
        memset(text, 0, sizeof(text));
        if (!shell_join_args(argc, argv, 2, text, sizeof(text))) {
            shell_print_line("write text too long");
            return;
        }
        if (shell_is_device_path(argv[1])) {
            shell_write_file(argv[1], text);
            return;
        }
        if (!shell_resolve_path(argv[1], resolved_path)) {
            shell_print_line("invalid path");
            return;
        }
        shell_write_file(resolved_path, text);
        return;
    }

    if (strcmp(argv[0], "rm") == 0) {
        if (argc < 2) {
            shell_print_line("usage: rm <path>");
        } else if (argc >= 3 && shell_rm_option_is_recursive_force(argv[1])) {
            if (!shell_resolve_path(argv[2], resolved_path)) {
                shell_print_line("invalid path");
            } else if (shell_remove_recursive(resolved_path)) {
                shell_check_boot_files_or_panic();
                shell_print_line("rm -rf ok");
            } else {
                shell_print_line("rm -rf failed");
            }
        } else if (shell_resolve_path(argv[1], resolved_path) && file_delete(resolved_path)) {
            shell_check_boot_files_or_panic();
            shell_print_line("rm ok");
        } else {
            shell_print_line("rm failed");
        }
        return;
    }

    if (strcmp(argv[0], "rmdir") == 0) {
        if (argc < 2) {
            shell_print_line("usage: rmdir <path>");
        } else if (shell_resolve_path(argv[1], resolved_path) && file_rmdir(resolved_path)) {
            shell_print_line("rmdir ok");
        } else {
            shell_print_line("rmdir failed");
        }
        return;
    }

    if (shell_has_suffix(argv[0], ".elf")) {
        shell_run_exec_program(argv[0], argc, argv);
        return;
    }

    shell_print_line("unknown command");
}

void shell_init(void)
{
    g_shell_privilege = SHELL_PRIV_R3;
    g_shell_state = SHELL_STATE_COMMAND;
    g_line_len = 0;
    g_cursor_pos = 0;
    g_password_len = 0;
    g_history_count = 0;
    g_history_index = 0;
    memset(g_line, 0, sizeof(g_line));
    memset(g_password, 0, sizeof(g_password));
    memset(g_pending_sudo, 0, sizeof(g_pending_sudo));
    memset(g_history, 0, sizeof(g_history));
    memset(g_draft, 0, sizeof(g_draft));
    strcpy(g_shell_cwd, "/");
    /* init environment */
    g_env_count = 0;
    shell_env_set_pair("HOME=/");
    shell_env_set_pair("PWD=/");
    shell_env_set_pair("USER=root");
    g_last_drawn_len = 0;
    shell_print_prompt();
}

shell_privilege_t shell_privilege(void)
{
    return g_shell_privilege;
}

const char *shell_current_cwd(void)
{
    return g_shell_cwd;
}

bool shell_exec_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    shell_run_exec_program(path, 1, (char *[]) { (char *) path });
    return true;
}

void shell_resume_text_mode(void)
{
    if (graphics_active()) {
        return;
    }
    g_last_drawn_len = 0;
    console_clear();

    if (g_shell_state == SHELL_STATE_COMMAND) {
        shell_redraw_input_line();
        return;
    }

    console_write("password:");
    for (uint32_t i = 0; i < g_password_len; i++) {
        console_write_char('*');
    }
}

void shell_handle_navigation_key(key_event_type_t type)
{
    if (g_shell_state != SHELL_STATE_COMMAND) {
        return;
    }

    switch (type) {
    case KEY_EVENT_UP:
        if (g_history_count == 0) {
            return;
        }
        if (g_history_index == (int32_t) g_history_count) {
            strcpy(g_draft, g_line);
        }
        if (g_history_index > 0) {
            g_history_index--;
        }
        shell_history_load(g_history_index);
        break;
    case KEY_EVENT_DOWN:
        if (g_history_count == 0) {
            return;
        }
        if (g_history_index < (int32_t) g_history_count - 1) {
            g_history_index++;
            shell_history_load(g_history_index);
        } else {
            g_history_index = (int32_t) g_history_count;
            strcpy(g_line, g_draft);
            g_line_len = (uint32_t) strlen(g_line);
            g_cursor_pos = g_line_len;
            shell_redraw_input_line();
        }
        break;
    case KEY_EVENT_LEFT:
        if (g_cursor_pos > 0) {
            g_cursor_pos--;
            shell_redraw_input_line();
        }
        break;
    case KEY_EVENT_RIGHT:
        if (g_cursor_pos < g_line_len) {
            g_cursor_pos++;
            shell_redraw_input_line();
        }
        break;
    default:
        break;
    }
}

void shell_handle_special_key(key_event_type_t type)
{
    if (g_shell_state != SHELL_STATE_COMMAND) {
        return;
    }

    switch (type) {
    case KEY_EVENT_HOME:
        g_cursor_pos = 0;
        shell_redraw_input_line();
        break;
    case KEY_EVENT_END:
        g_cursor_pos = g_line_len;
        shell_redraw_input_line();
        break;
    case KEY_EVENT_DELETE:
        shell_delete_char();
        break;
    default:
        break;
    }
}

void shell_handle_key_event(const key_event_t *event)
{
    if (event->type != KEY_EVENT_CHAR && event->type != KEY_EVENT_TAB && event->type != KEY_EVENT_CTRL_C) {
        return;
    }

    if (event->type == KEY_EVENT_CTRL_C) {
        shell_print_line("^C");
        g_shell_state = SHELL_STATE_COMMAND;
        g_line_len = 0;
        g_cursor_pos = 0;
        g_password_len = 0;
        g_line[0] = '\0';
        g_password[0] = '\0';
        shell_print_prompt();
        return;
    }

    if (g_shell_state == SHELL_STATE_COMMAND) {
        if (event->type == KEY_EVENT_TAB) {
            shell_do_tab_complete();
            return;
        }
        if (event->status.ctrl_down) {
            switch (event->ch) {
            case 'a':
            case 'A':
                g_cursor_pos = 0;
                shell_redraw_input_line();
                return;
            case 'e':
            case 'E':
                g_cursor_pos = g_line_len;
                shell_redraw_input_line();
                return;
            case 'l':
            case 'L':
                console_clear();
                shell_redraw_input_line();
                return;
            case 'u':
            case 'U':
                shell_kill_to_start();
                return;
            case 'k':
            case 'K':
                shell_kill_to_end();
                return;
            default:
                break;
            }
        }
        if (event->ch == '\b') {
            shell_backspace_char();
            return;
        }
        if (event->ch == '\n') {
            char command[SHELL_LINE_MAX];
            console_write("\r\n");
            memcpy(command, g_line, g_line_len + 1);
            shell_history_push(command);
            g_line_len = 0;
            g_cursor_pos = 0;
            g_last_drawn_len = 0;
            g_line[0] = '\0';
            g_draft[0] = '\0';
            shell_execute_line(command, false);
            if (g_shell_state == SHELL_STATE_COMMAND) {
                shell_print_prompt();
            }
            graphics_notify_process_output();
            return;
        }
        shell_insert_char(event->ch);
        return;
    }

    if (event->ch == '\b') {
        if (g_password_len > 0) {
            g_password[--g_password_len] = '\0';
            console_backspace();
        }
        return;
    }
    if (event->ch == '\n') {
        bool ok = shell_authenticate_password(g_password);
        console_write("\r\n");
        if (g_shell_state == SHELL_STATE_SU_PASSWORD) {
            if (ok) {
                g_shell_privilege = SHELL_PRIV_R0;
                shell_print_line("switched to R0");
            } else {
                shell_print_line("authentication failed");
            }
        } else if (g_shell_state == SHELL_STATE_SUDO_PASSWORD) {
            if (ok) {
                char command[SHELL_LINE_MAX];
                strcpy(command, g_pending_sudo);
                shell_execute_line(command, true);
            } else {
                shell_print_line("authentication failed");
            }
        }
        g_shell_state = SHELL_STATE_COMMAND;
        g_password_len = 0;
        g_password[0] = '\0';
        g_pending_sudo[0] = '\0';
        g_line_len = 0;
        g_cursor_pos = 0;
        g_last_drawn_len = 0;
        g_line[0] = '\0';
        g_draft[0] = '\0';
        shell_print_prompt();
        return;
    }
    if (g_password_len + 1 < sizeof(g_password)) {
        g_password[g_password_len++] = event->ch;
        g_password[g_password_len] = '\0';
        console_write_char('*');
    }
}