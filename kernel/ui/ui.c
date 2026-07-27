#include "common.h"
#include "file.h"
#include "kernel.h"
#include "net.h"
#include "path.h"
#include "ui.h"

#define UI_NETWORK_PROBE_INTERVAL_TICKS 500U

static ui_network_state_t g_ui_network_state = UI_NETWORK_NO_ADAPTER;
static char g_ui_network_label[48] = "No network connection";
static char g_ui_network_probe_target[32] = "";
static uint64_t g_ui_network_last_probe_tick;

static bool ui_mkdir_if_missing(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    if (file_exists(path)) {
        return file_is_dir(path);
    }
    return file_mkdir(path);
}

bool ui_ensure_system_layout(void)
{
    bool ok = true;

    ok = ui_mkdir_if_missing(UI_SYSTEM_ROOT) && ok;
    ok = ui_mkdir_if_missing(UI_APPS_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_DRIVERS_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_BOOT_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_CONFIG_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_CURSORS_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_FONTS_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_LIB_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_SYSTEM_MEDIA_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_USERS_DIR) && ok;
    ok = ui_mkdir_if_missing(UI_ROOT_HOME) && ok;
    ok = ui_mkdir_if_missing(UI_GUEST_HOME) && ok;
    ok = ui_mkdir_if_missing(UI_DEV_HOME) && ok;
    ok = ui_mkdir_if_missing(UI_ROOT_DESKTOP) && ok;
    return ok;
}

const char *ui_path_drive_root(void)
{
    return PATH_DEFAULT_DRIVE;
}

bool ui_path_to_windows(const char *path, char *out, uint32_t out_size)
{
    char normalized[PATH_MAX_LEN];

    if (path == NULL || out == NULL || out_size == 0) {
        return false;
    }
    if (!path_resolve(NULL, path, normalized, sizeof(normalized))) {
        out[0] = '\0';
        return false;
    }
    if (strlen(normalized) + 1 > out_size) {
        out[0] = '\0';
        return false;
    }
    strcpy(out, normalized);
    return true;
}

static void ui_network_set(ui_network_state_t state, const char *label, const char *target)
{
    g_ui_network_state = state;
    strlcpy(g_ui_network_label, label != NULL ? label : "", sizeof(g_ui_network_label));
    strlcpy(g_ui_network_probe_target, target != NULL ? target : "", sizeof(g_ui_network_probe_target));
}

void ui_network_update(uint64_t now_ticks)
{
    const net_info_t *info = net_info();
    uint32_t hz = timer_hz();
    uint64_t interval = (uint64_t) hz * 10ULL;

    if (interval == 0) {
        interval = UI_NETWORK_PROBE_INTERVAL_TICKS;
    }
    if (info == NULL || !info->present) {
        g_ui_network_last_probe_tick = now_ticks;
        ui_network_set(UI_NETWORK_NO_ADAPTER, "No network connection", "");
        return;
    }
    if (!info->connected) {
        g_ui_network_last_probe_tick = now_ticks;
        ui_network_set(UI_NETWORK_DISCONNECTED, "No network connection", "");
        return;
    }
    if (!info->dhcp_configured) {
        g_ui_network_last_probe_tick = now_ticks;
        ui_network_set(UI_NETWORK_DISCONNECTED, "No network connection", "");
        return;
    }
    if (g_ui_network_last_probe_tick != 0 &&
        now_ticks - g_ui_network_last_probe_tick < interval) {
        return;
    }

    g_ui_network_last_probe_tick = now_ticks;
    ui_network_set(UI_NETWORK_VALIDATING, "Checking Ethernet", "baidu.com");
    if (net_ping("baidu.com")) {
        ui_network_set(UI_NETWORK_CONNECTED, "Connected to Ethernet", "baidu.com");
        return;
    }
    ui_network_set(UI_NETWORK_VALIDATING, "Checking Ethernet", "google.com");
    if (net_ping("google.com")) {
        ui_network_set(UI_NETWORK_CONNECTED, "Connected to Ethernet", "google.com");
        return;
    }
    ui_network_set(UI_NETWORK_LIMITED, "Cannot connect to Ethernet", "baidu.com/google.com");
}

ui_network_state_t ui_network_state(void)
{
    return g_ui_network_state;
}

const char *ui_network_label(void)
{
    return g_ui_network_label;
}

const char *ui_network_probe_target(void)
{
    return g_ui_network_probe_target;
}
