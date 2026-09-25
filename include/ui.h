#ifndef _UI_H_
#define _UI_H_

#include "stdbool.h"
#include "stdint.h"

#define UI_SYSTEM_ROOT              "C:\\Monios"
#define UI_APPS_DIR                 "C:\\Monios\\Apps"
#define UI_DRIVERS_DIR              "C:\\Monios\\driver"
#define UI_SYSTEM_DIR               "C:\\Monios\\System"
#define UI_SYSTEM_BOOT_DIR          "C:\\Monios\\System\\Boot"
#define UI_SYSTEM_CONFIG_DIR        "C:\\Monios\\System\\Config"
#define UI_SYSTEM_CURSORS_DIR       "C:\\Monios\\System\\Cursors"
#define UI_SYSTEM_FONTS_DIR         "C:\\Monios\\System\\Fonts"
#define UI_SYSTEM_LIB_DIR           "C:\\Monios\\System\\Lib"
#define UI_SYSTEM_MEDIA_DIR         "C:\\Monios\\System\\Media"
#define UI_SYSTEM_SECURITY_DIR      "C:\\Monios\\System\\Security"
#define UI_TRUST_ROOT_DIR           "C:\\Monios\\System\\Security\\Root"
#define UI_TRUST_ROOT_BUNDLE_PATH   "C:\\Monios\\System\\Security\\trust.bin"
#define UI_CUSTOM_COMPONENT_DIR     "C:\\Monios\\System\\UI\\Comps"
#define UI_OSLOG_DIR                "C:\\Monios\\OSlog"
#define UI_USERS_DIR                "C:\\Monios\\Users"
#define UI_ROOT_HOME                "C:\\Monios\\Users\\root"
#define UI_GUEST_HOME               "C:\\Monios\\Users\\guest"
#define UI_DEV_HOME                 "C:\\Monios\\Users\\dev"
#define UI_ROOT_DESKTOP             "C:\\Monios\\Users\\root\\Desktop"

#define UI_KERNEL_IMAGE_PATH        "C:\\Monios\\kernel.exe"
#define UI_LOADER_IMAGE_PATH        "C:\\Monios\\System\\Boot\\loader.bin"
#define UI_BOOT_FONT_PATH           "C:\\Monios\\System\\Fonts\\msyh.ttc"
#define UI_WALLPAPER_PATH           "C:\\Monios\\System\\Media\\wallpaper.jpg"
#define UI_BOOT_IMAGE_PATH          "C:\\Monios\\System\\Media\\boot.bmp"
#define UI_CURSOR_CONFIG_PATH       "C:\\Monios\\System\\Config\\cursor.cfg"
#define UI_CURSOR_ASSET_PATH        "C:\\Monios\\System\\Cursors\\arrow.cur"
#define UI_AUTH_PATH                "C:\\Monios\\System\\Config\\pwd.txt"
#define UI_VERSION_PATH             "C:\\Monios\\System\\version.txt"

#define UI_EXPLORER_PATH            "C:\\Monios\\Apps\\explorar.exe"
#define UI_LOGON_PATH               "C:\\Monios\\Apps\\monilog.exe"
#define UI_PLAYER_PATH              "C:\\Monios\\Apps\\player.exe"
#define UI_BROWSER_PATH             "C:\\Monios\\Apps\\browser.exe"
#define UI_NOTEPAD_PATH             "C:\\Monios\\Apps\\notepad.exe"
#define UI_TASKMGR_PATH             "C:\\Monios\\Apps\\taskmgr.exe"
#define UI_SETUP_PATH               "C:\\Monios\\Apps\\setup.exe"
#define UI_SYSINST_PATH             "C:\\Monios\\Apps\\sysinst.exe"
#define UI_RZDRV_PATH               "C:\\Monios\\driver\\rzdrv.sys"
#define UI_MONIOS_DLL_PATH          "C:\\Monios\\System\\Lib\\monios.dll"
#define UI_CONSOLE_DLL_PATH         "C:\\Monios\\System\\Lib\\console.dll"
#define UI_WINDOWS_DLL_PATH         "C:\\Monios\\System\\Lib\\windows.dll"
#define UI_OSUI_DLL_PATH            "C:\\Monios\\System\\Lib\\osui.dll"

typedef enum {
    UI_NETWORK_NO_ADAPTER = 0,
    UI_NETWORK_DISCONNECTED,
    UI_NETWORK_VALIDATING,
    UI_NETWORK_LIMITED,
    UI_NETWORK_CONNECTED
} ui_network_state_t;

bool ui_ensure_system_layout(void);
bool ui_path_to_windows(const char *path, char *out, uint32_t out_size);
const char *ui_path_drive_root(void);

void ui_network_update(uint64_t now_ticks);
ui_network_state_t ui_network_state(void);
const char *ui_network_label(void);
const char *ui_network_probe_target(void);

#endif
