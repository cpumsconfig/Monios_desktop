#include "audio.h"
#include "cmos.h"
#include "common.h"
#include "console.h"
#include "exec.h"
#include "file.h"
#include "font.h"
#include "interrupt.h"
#include "graphics.h"
#include "mmu.h"
#include "kernel.h"
#include "memory.h"
#include "mouse.h"
#include "net.h"
#include "path.h"
#include "registry.h"
#include "session.h"
#include "shell.h"
#include "task.h"
#include "terminal.h"
#include "ui.h"

#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT  0x01CF
#define BGA_ID         0x0
#define BGA_XRES       0x1
#define BGA_YRES       0x2
#define BGA_BPP        0x3
#define BGA_ENABLE     0x4
#define BGA_VIRT_WIDTH  0x6
#define BGA_VIRT_HEIGHT 0x7
#define BGA_X_OFFSET   0x8
#define BGA_Y_OFFSET   0x9

#define BGA_DISABLED   0x00
#define BGA_ENABLED    0x01
#define BGA_LFB        0x40
#define BGA_ID_5       0xB0C5

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_VENDOR_VMWARE  0x15AD
#define PCI_DEVICE_VMWARE_SVGA2 0x0405

#define SVGA_REG_ID            0
#define SVGA_REG_ENABLE        1
#define SVGA_REG_WIDTH         2
#define SVGA_REG_HEIGHT        3
#define SVGA_REG_BYTES_PER_LINE 12
#define SVGA_REG_BITS_PER_PIXEL 7
#define SVGA_REG_FB_START      13
#define SVGA_REG_FB_OFFSET     14

#define SVGA_ID_2              0x90000002
#define SVGA_ENABLE_ENABLE     0x00000001

#define GRAPHICS_MAX_WIDTH  1440
#define GRAPHICS_MAX_HEIGHT 900
#define FB_WIDTH  g_graphics_width
#define FB_HEIGHT g_graphics_height
#define TASKBAR_HEIGHT 54
#define BUTTON_WIDTH 44
#define BUTTON_HEIGHT 34
#define BUTTON_GAP 8
#define BUTTON_Y (FB_HEIGHT - TASKBAR_HEIGHT + 10)
#define DESKTOP_LABEL_MAX 24
#define START_BUTTON_WIDTH 44
#define DESKTOP_ICON_W 74
#define DESKTOP_ICON_H 82
#define DESKTOP_ICON_GAP_Y 14
#define DESKTOP_ICON_GAP_X 18
#define DESKTOP_ICON_START_X 22
#define DESKTOP_ICON_START_Y 22
#define DESKTOP_ICON_COLS 3
#define CONTEXT_MENU_W 170
#define CONTEXT_MENU_DESKTOP_H 104
#define CONTEXT_MENU_FILES_H 128
#define UI_WINDOW_MAX 24
#define MONIOS_VERSION "1.0"
#define UI_MENU_ITEM_H 24
#define START_MENU_HEADER_H 36
#define UI_DESKTOP_POLL_TICKS 30
#define UI_TITLEBAR_H 28
#define UI_TASK_BUTTON_W 44
#define START_MENU_W 528
#define START_MENU_H 272
#define START_MENU_LEFT_W 144
#define START_MENU_MID_W 176
#define START_MENU_RIGHT_W 176
#define START_MENU_PAD 8
#define START_MENU_X 12
#define FB_PIXELS (GRAPHICS_MAX_WIDTH * GRAPHICS_MAX_HEIGHT)
#define GRAPHICS_LOGIN_USERNAME_MAX 16
#define GRAPHICS_LOGIN_PASSWORD_MAX 64
#define GRAPHICS_FILE_NAME_MAX 32
#define GRAPHICS_FILE_PATH_MAX 128
#define GRAPHICS_FILE_ITEM_MAX 64
#define GRAPHICS_FILE_LIST_BUFFER 2048
#define GRAPHICS_CLIPBOARD_PATH_MAX 256
#define GRAPHICS_ICON_CACHE_MAX 48
#define GRAPHICS_ICON_FLAG_SHORTCUT 0x00000001U
#define GRAPHICS_ICON_FLAG_UAC      0x00000002U
#define GRAPHICS_NOTEPAD_TEXT_MAX 2048
#define GRAPHICS_UAC_TEXT_MAX 96
#define GRAPHICS_SCROLLBAR_W 12
#define GRAPHICS_CONSOLE_ROWS CONSOLE_ROWS
#define GRAPHICS_WINDOW_TITLE_MAX TERMINAL_WINDOW_TITLE_MAX
#define GRAPHICS_CURSOR_CONFIG_PATH UI_CURSOR_CONFIG_PATH
#define GRAPHICS_CURSOR_MAX_WIDTH 32
#define GRAPHICS_CURSOR_MAX_HEIGHT 32
#define GRAPHICS_CURSOR_MAX_PIXELS (GRAPHICS_CURSOR_MAX_WIDTH * GRAPHICS_CURSOR_MAX_HEIGHT)
#define GRAPHICS_CURSOR_ASSET_MAX_BYTES (512U * 1024U)
#define GRAPHICS_WALLPAPER_MAX_WIDTH 320
#define GRAPHICS_WALLPAPER_MAX_HEIGHT 180
#define GRAPHICS_WALLPAPER_MAX_PIXELS (GRAPHICS_WALLPAPER_MAX_WIDTH * GRAPHICS_WALLPAPER_MAX_HEIGHT)
#define GRAPHICS_WALLPAPER_MAX_BYTES (512U * 1024U)
#define GRAPHICS_BOOT_IMAGE_MAX_WIDTH 400
#define GRAPHICS_BOOT_IMAGE_MAX_HEIGHT 225
#define GRAPHICS_BOOT_IMAGE_MAX_PIXELS (GRAPHICS_BOOT_IMAGE_MAX_WIDTH * GRAPHICS_BOOT_IMAGE_MAX_HEIGHT)
#define GRAPHICS_BOOT_IMAGE_MAX_BYTES (512U * 1024U)
#define GRAPHICS_BOOT_FADE_STEPS 18U
#define GRAPHICS_BOOT_FADE_DELAY 160000U
#define UI_COLOR_WINDOW_BG 0x00F8FAFE
#define UI_COLOR_WINDOW_EDGE 0x00CED7E4
#define UI_COLOR_TITLE_BG 0x00F4F7FC
#define UI_COLOR_TITLE_TEXT 0x001C2430
#define UI_COLOR_ACCENT 0x003C6FEA
#define UI_COLOR_ACCENT_DARK 0x002B52B8
#define UI_COLOR_WARN 0x00D94C5A
#define UI_COLOR_TASKBAR 0x00EFF5FC
#define UI_COLOR_TASKBAR_EDGE 0x00D4E0ED

typedef struct {
    const char *label;
    uint32_t color;
} graphics_button_t;

typedef enum {
    UI_WINDOW_NONE = 0,
    UI_WINDOW_LOGON,
    UI_WINDOW_FILES,
    UI_WINDOW_TERMINAL,
    UI_WINDOW_PROCESS_CONSOLE,
    UI_WINDOW_RUN,
    UI_WINDOW_SHELL,
    UI_WINDOW_ABOUT,
    UI_WINDOW_PLAYER,
    UI_WINDOW_NOTEPAD,
    UI_WINDOW_TASKMGR,
    UI_WINDOW_CUBE3D,
    UI_WINDOW_CONTEXT,
    UI_WINDOW_POWER,
    UI_WINDOW_UAC,
    UI_WINDOW_CONTROL_PANEL
} ui_window_kind_t;

typedef enum {
    UI_CLICK_SOURCE_NONE = 0,
    UI_CLICK_SOURCE_DESKTOP,
    UI_CLICK_SOURCE_FILES
} ui_click_source_t;

typedef enum {
    UI_CLIPBOARD_NONE = 0,
    UI_CLIPBOARD_COPY,
    UI_CLIPBOARD_CUT
} ui_clipboard_mode_t;

typedef enum {
    UI_CONTEXT_MENU_DESKTOP = 0,
    UI_CONTEXT_MENU_FILES
} ui_context_menu_mode_t;

typedef enum {
    UI_START_MENU_ROOT = 0,
    UI_START_MENU_APPS,
    UI_START_MENU_SETTINGS,
    UI_START_MENU_DISPLAY,
    UI_START_MENU_CURSOR,
    UI_START_MENU_NETWORK,
    UI_START_MENU_POWER
} ui_start_menu_view_t;

typedef enum {
    GRAPHICS_ICON_FILE = 0,
    GRAPHICS_ICON_FOLDER,
    GRAPHICS_ICON_APP,
    GRAPHICS_ICON_APP_RESOURCE,
    GRAPHICS_ICON_AUDIO,
    GRAPHICS_ICON_PACKAGE,
    GRAPHICS_ICON_DLL,
    GRAPHICS_ICON_SHORTCUT,
    GRAPHICS_ICON_TERMINAL,
    GRAPHICS_ICON_SETTINGS,
    GRAPHICS_ICON_INFO,
    GRAPHICS_ICON_WINDOW,
    GRAPHICS_ICON_DRIVER,
    GRAPHICS_ICON_UAC,
    GRAPHICS_ICON_NETWORK_CONNECTED,
    GRAPHICS_ICON_NETWORK_LIMITED,
    GRAPHICS_ICON_NETWORK_OFFLINE
} graphics_icon_kind_t;

typedef struct {
    bool visible;
    bool minimized;
    bool maximized;
    ui_window_kind_t kind;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint16_t restore_x;
    uint16_t restore_y;
    uint16_t restore_width;
    uint16_t restore_height;
    int32_t owner_pid;
    uint32_t console_scroll_offset;
    bool console_scroll_manual;
    char title[GRAPHICS_WINDOW_TITLE_MAX];
} ui_window_t;

typedef struct {
    char name[GRAPHICS_FILE_NAME_MAX];
    bool is_dir;
} graphics_file_item_t;

typedef struct {
    bool valid;
    char path[GRAPHICS_CLIPBOARD_PATH_MAX];
    uint32_t image_flags;
} graphics_icon_cache_entry_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    const char *label;
} graphics_mode_t;

typedef struct {
    const char *label;
    const uint16_t *shape;
    const uint16_t *fill;
    uint32_t fill_color;
    uint32_t outline_color;
} graphics_cursor_style_t;

static const graphics_button_t g_taskbar_buttons[] = {
    { "\u6587\u4ef6", 0x002F7FD3 },
    { "\u63a7\u5236\u53f0", 0x004A8A44 },
    { "\u8bbe\u7f6e", 0x006A67CE },
    { "\u5173\u4e8e", 0x00C06C2B }
};

static const graphics_mode_t g_graphics_modes[] = {
    { 1024, 768, "1024x768" },
    { 1280, 800, "1280x800" },
    { 1366, 768, "1366x768" },
    { 1440, 900, "1440x900" }
};

static graphics_file_item_t g_desktop_entries[DESKTOP_LABEL_MAX];
static uint32_t g_desktop_entry_count;
static uint32_t g_active_button_index;
static bool g_start_menu_open;
static ui_start_menu_view_t g_start_menu_view;
static bool g_context_menu_open;
static bool g_power_menu_open;
static uint16_t g_power_menu_x;
static uint16_t g_power_menu_y;
static uint16_t g_context_menu_x;
static uint16_t g_context_menu_y;
static bool g_dragging_window;
static uint32_t g_drag_window_index;
static int32_t g_drag_offset_x;
static int32_t g_drag_offset_y;
static uint8_t g_prev_mouse_buttons;
static uint64_t g_last_desktop_scan_tick;
static uint64_t g_last_status_tick;
static uint64_t g_last_click_tick;
static uint32_t g_last_click_index;
static ui_click_source_t g_last_click_source;
static bool g_rainbow_cat_open;
static uint64_t g_rainbow_cat_last_click_tick;
static uint8_t g_rainbow_cat_click_count;
static ui_window_t g_windows[UI_WINDOW_MAX];
static char g_run_input[64];
static uint32_t g_run_input_len;
static bool g_run_input_focus;
static bool g_terminal_input_focus;
static bool g_sleeping;
static char g_login_username[GRAPHICS_LOGIN_USERNAME_MAX];
static char g_login_password[GRAPHICS_LOGIN_PASSWORD_MAX];
static uint32_t g_login_field;
static bool g_login_error;
static char g_uac_program[GRAPHICS_UAC_TEXT_MAX];
static char g_uac_reason[GRAPHICS_UAC_TEXT_MAX];
static char g_uac_password[GRAPHICS_LOGIN_PASSWORD_MAX];
static uint32_t g_uac_password_len;
static bool g_uac_pending;
static bool g_uac_result_ready;
static bool g_uac_result;
static bool g_uac_error;
static bool g_uac_input_focus;
static uint8_t g_uac_last_buttons;
static uint32_t g_uac_privilege_level;
static char g_file_current_path[GRAPHICS_FILE_PATH_MAX];
static graphics_file_item_t g_file_items[GRAPHICS_FILE_ITEM_MAX];
static graphics_icon_cache_entry_t g_icon_cache[GRAPHICS_ICON_CACHE_MAX];
static uint32_t g_icon_cache_next;
static uint32_t g_file_item_count;
static uint32_t g_file_selected_index;
static uint32_t g_file_scroll_offset;
static ui_context_menu_mode_t g_context_menu_mode;
static ui_clipboard_mode_t g_clipboard_mode;
static char g_clipboard_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static bool g_clipboard_is_dir;
static bool g_player_button_pressed;
static bool g_player_browser_open;
static char g_player_current_path[GRAPHICS_FILE_PATH_MAX];
static graphics_file_item_t g_player_items[DESKTOP_LABEL_MAX];
static uint32_t g_player_item_count;
static uint32_t g_player_selected_index;
static char g_player_selected_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static char g_player_status[64];
static char g_notepad_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static char g_notepad_text[GRAPHICS_NOTEPAD_TEXT_MAX];
static uint32_t g_notepad_len;
static bool g_notepad_focus;
static bool g_cube3d_open;
static float g_cube3d_angle_x;
static float g_cube3d_angle_y;
static float g_cube3d_angle_z;
static uint64_t g_cube3d_last_tick;
static uint32_t g_wallpaper_pixels[GRAPHICS_WALLPAPER_MAX_PIXELS];
static uint16_t g_wallpaper_width;
static uint16_t g_wallpaper_height;
static bool g_wallpaper_attempted;
static bool g_wallpaper_loaded;
static uint32_t g_boot_image_pixels[GRAPHICS_BOOT_IMAGE_MAX_PIXELS];
static uint16_t g_boot_image_width;
static uint16_t g_boot_image_height;
static bool g_boot_image_attempted;
static bool g_boot_image_loaded;

static const uint16_t g_cursor_arrow_shape[16] = {
    0x8000, 0xC000, 0xE000, 0xF000,
    0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFFE0, 0xFC00, 0xCC00, 0x8C00,
    0x0600, 0x0600, 0x0300, 0x0000
};

static const uint16_t g_cursor_arrow_fill[16] = {
    0x0000, 0x4000, 0x6000, 0x7000,
    0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7FE0, 0x6C00, 0x4C00, 0x0C00,
    0x0400, 0x0000, 0x0000, 0x0000
};

static const uint16_t g_cursor_dot_shape[16] = {
    0x0000, 0x0000, 0x0780, 0x1FE0,
    0x3FF0, 0x7FF8, 0x7FF8, 0x7FF8,
    0x7FF8, 0x7FF8, 0x3FF0, 0x1FE0,
    0x0780, 0x0000, 0x0000, 0x0000
};

static const uint16_t g_cursor_dot_fill[16] = {
    0x0000, 0x0000, 0x0000, 0x0780,
    0x1FE0, 0x3FF0, 0x3FF0, 0x3FF0,
    0x3FF0, 0x3FF0, 0x1FE0, 0x0780,
    0x0000, 0x0000, 0x0000, 0x0000
};

static const uint16_t g_cursor_cross_shape[16] = {
    0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x7FFE, 0x7FFE,
    0x7FFE, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0000
};

static const uint16_t g_cursor_cross_fill[16] = {
    0x0000, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0000, 0x0180, 0x3FFC,
    0x0180, 0x0000, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0000, 0x0000
};

static const graphics_cursor_style_t g_cursor_styles[] = {
    { "\u7ecf\u5178", g_cursor_arrow_shape, g_cursor_arrow_fill, 0x00FFFFFF, 0x00000000 },
    { "\u84dd\u8272", g_cursor_arrow_shape, g_cursor_arrow_fill, 0x008FD3FF, 0x001B4F7A },
    { "\u91d1\u8272", g_cursor_arrow_shape, g_cursor_arrow_fill, 0x00FFE08A, 0x006B4E00 },
    { "\u5706\u70b9", g_cursor_dot_shape, g_cursor_dot_fill, 0x00FFFFFF, 0x00256EC8 },
    { "\u5341\u5b57", g_cursor_cross_shape, g_cursor_cross_fill, 0x00FFFFFF, 0x00B13434 }
};

static bool g_graphics_active;
static bool g_installer_mode;
static bool g_graphics_vmware_backend;
static uint32_t g_graphics_framebuffer_addr;
static uint16_t g_graphics_width = GRAPHICS_WIDTH;
static uint16_t g_graphics_height = GRAPHICS_HEIGHT;
static uint8_t g_graphics_mode_index = 0;
static uint8_t g_cursor_style_index = 0;
static bool g_graphics_fast_mode_switch;
static bool g_graphics_boot_animation_mode;
static bool g_session_logged_in;
static bool g_double_buffer_enabled = true;
static bool g_vsync_enabled = false;
static uint32_t g_fps_value = 0;
static uint64_t g_last_fps_tick = 0;
static uint32_t g_present_snapshot = 0;
static uint32_t g_selected_login_user;
static uint32_t g_cursor_saved[GRAPHICS_CURSOR_MAX_PIXELS];
static uint16_t g_cursor_x;
static uint16_t g_cursor_y;
static uint16_t g_cursor_draw_width;
static uint16_t g_cursor_draw_height;
static bool g_cursor_drawn;
static bool g_windows_cursor_attempted;
static bool g_windows_cursor_loaded;
static uint16_t g_windows_cursor_width;
static uint16_t g_windows_cursor_height;
static uint16_t g_windows_cursor_hotspot_x;
static uint16_t g_windows_cursor_hotspot_y;
static uint32_t g_windows_cursor_pixels[GRAPHICS_CURSOR_MAX_PIXELS];
static volatile uint32_t *g_framebuffer;
static uint32_t g_backbuffer[FB_PIXELS];
static uint16_t g_svga_io_base;
static uint32_t g_framebuffer_pitch_bytes;
static uint32_t g_framebuffer_pitch_pixels;
static bool g_gpu_present_pending;
static uint32_t g_gpu_submit_count;
static uint32_t g_gpu_present_count;

typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint32_t bar0;
    uint32_t bar1;
    bool found;
} graphics_pci_device_t;

static void graphics_open_window(ui_window_kind_t kind);
static void graphics_close_window(uint32_t index);
static void graphics_minimize_window(uint32_t index);
static void graphics_toggle_maximize_window(uint32_t index);
static void graphics_open_start_menu_view(ui_start_menu_view_t view);
static bool graphics_handle_start_menu_click(uint16_t x, uint16_t y);
static void graphics_open_path(const char *path);
static void graphics_run_command_text(const char *command);
static bool graphics_launch_user_program(const char *path);
static bool graphics_launch_user_program_with_arg(const char *path, const char *arg);
static bool graphics_launch_named_user_program(const char *name);
static int32_t graphics_find_window(ui_window_kind_t kind);
static int32_t graphics_find_process_console_window(int32_t pid);
static bool graphics_window_is_terminal(const ui_window_t *window);
static void graphics_bring_window_to_front(uint32_t index);
static int32_t graphics_find_mode_index(uint16_t width, uint16_t height);
static uint16_t graphics_power_menu_top(void);
static void graphics_reflow_windows(void);
static bool graphics_set_resolution(uint16_t width, uint16_t height);
static bool graphics_path_has_suffix(const char *path, const char *suffix);
static bool graphics_path_is_executable(const char *path);
static bool graphics_path_is_package(const char *path);
static bool graphics_resolve_shortcut(const char *path, char *target, uint32_t target_size);
static bool graphics_create_desktop_shortcut(const char *target_path);
static uint32_t graphics_lerp_color(uint32_t from, uint32_t to, uint32_t num, uint32_t den);
static void graphics_plot(uint16_t x, uint16_t y, uint32_t color);
static void graphics_draw_rect_outline(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
static void graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color);
static void graphics_draw_text_aligned(uint16_t x, uint16_t y, uint16_t width, const char *text, uint32_t color);
static void graphics_draw_text_clipped(uint16_t x, uint16_t y, uint16_t width, const char *text, uint32_t color);
static void graphics_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);
static void graphics_draw_cube3d_window(const ui_window_t *window);
static void graphics_draw_taskbar_status(void);
static void graphics_draw_control_panel_window(const ui_window_t *window);
static void graphics_draw_logon_window(const ui_window_t *window);
static uint16_t graphics_taskbar_pinned_start_x(void);
static uint16_t graphics_taskbar_running_start_x(void);
static void graphics_restore_cursor(void);
static bool graphics_windows_cursor_ready(void);
static bool graphics_cursor_use_windows_asset(void);
static void graphics_draw_rainbow_cat(void);
static void graphics_reset_start_menu(void);
static bool graphics_point_in_rect(uint16_t px, uint16_t py, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
static void graphics_fill(uint32_t color);
static void graphics_append_path_component(char *path, uint32_t path_size, const char *component);
static void graphics_fill_player_browser(void);
static void graphics_player_open_browser(void);
static uint32_t graphics_file_visible_rows(const ui_window_t *window);
static uint32_t graphics_file_max_scroll(const ui_window_t *window);
static void graphics_file_clamp_scroll(const ui_window_t *window);
static void graphics_file_ensure_selected_visible(const ui_window_t *window);
static void graphics_file_scroll_by(int32_t delta);
static uint32_t graphics_terminal_visible_rows(const ui_window_t *window);
static uint32_t graphics_terminal_content_rows(const ui_window_t *window);
static uint32_t graphics_terminal_max_scroll(const ui_window_t *window);
static void graphics_terminal_scroll_by(int32_t delta, ui_window_t *window);
static void graphics_draw_scrollbar(uint16_t x, uint16_t y, uint16_t height,
                                    uint32_t total, uint32_t visible, uint32_t offset,
                                    bool dark);
static bool graphics_player_path_is_audio(const char *path);
static bool graphics_player_try_play(const char *path);

static void graphics_set_terminal_focus(bool focused)
{
    g_terminal_input_focus = focused;
    terminal_set_focus(focused);
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t address =
        (1u << 31) |
        ((uint32_t) bus << 16) |
        ((uint32_t) slot << 11) |
        ((uint32_t) func << 8) |
        (offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void graphics_u32_to_hex8(uint32_t value, char out[9])
{
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t nibble = (uint8_t) ((value >> ((7 - i) * 4)) & 0xF);
        out[i] = (char) (nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
    }
    out[8] = '\0';
}

static void graphics_u32_to_dec(char *out, uint32_t value)
{
    char temp[11];
    uint32_t index = 0;
    uint32_t out_index = 0;

    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (value > 0 && index < sizeof(temp)) {
        temp[index++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    while (index > 0) {
        out[out_index++] = temp[--index];
    }
    out[out_index] = '\0';
}

static void graphics_make_resolution_label(char *out, uint32_t out_size, uint16_t width, uint16_t height)
{
    char number[11];
    uint32_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    graphics_u32_to_dec(number, width);
    for (uint32_t i = 0; number[i] != '\0' && pos + 1 < out_size; i++) {
        out[pos++] = number[i];
    }
    if (pos + 1 < out_size) {
        out[pos++] = 'x';
    }
    graphics_u32_to_dec(number, height);
    for (uint32_t i = 0; number[i] != '\0' && pos + 1 < out_size; i++) {
        out[pos++] = number[i];
    }
    out[pos] = '\0';
}

static int32_t graphics_find_mode_index(uint16_t width, uint16_t height)
{
    for (uint32_t i = 0; i < sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]); i++) {
        if (g_graphics_modes[i].width == width && g_graphics_modes[i].height == height) {
            return (int32_t) i;
        }
    }
    return -1;
}

static uint32_t graphics_cursor_style_count(void)
{
    return (uint32_t) (sizeof(g_cursor_styles) / sizeof(g_cursor_styles[0]));
}

static void graphics_save_cursor_style(void)
{
    char data[4];

    graphics_u32_to_dec(data, g_cursor_style_index);
    strcpy(data + strlen(data), "\n");
    file_write(GRAPHICS_CURSOR_CONFIG_PATH, data, (uint32_t) strlen(data));
}

static void graphics_load_cursor_style(void)
{
    char data[8];
    int32_t read;
    uint32_t value = 0;

    read = file_read(GRAPHICS_CURSOR_CONFIG_PATH, data, sizeof(data) - 1);
    if (read <= 0) {
        return;
    }
    data[read] = '\0';
    for (uint32_t i = 0; data[i] >= '0' && data[i] <= '9'; i++) {
        value = value * 10u + (uint32_t) (data[i] - '0');
    }
    if (value < graphics_cursor_style_count()) {
        g_cursor_style_index = (uint8_t) value;
    }
}

static void graphics_set_cursor_style(uint8_t index)
{
    if (index >= graphics_cursor_style_count()) {
        return;
    }
    if (g_cursor_style_index == index) {
        return;
    }
    graphics_restore_cursor();
    g_cursor_style_index = index;
    graphics_save_cursor_style();
    graphics_draw_shell();
}

static void graphics_draw_cursor_bitmap(uint16_t x, uint16_t y, const graphics_cursor_style_t *style)
{
    if (style == NULL) {
        return;
    }
    for (uint16_t row = 0; row < 16; row++) {
        for (uint16_t col = 0; col < 16; col++) {
            uint16_t mask = (uint16_t) (0x8000 >> col);

            if ((style->shape[row] & mask) == 0) {
                continue;
            }
            graphics_plot((uint16_t) (x + col), (uint16_t) (y + row),
                          (style->fill[row] & mask) != 0 ? style->fill_color : style->outline_color);
        }
    }
}

static uint16_t graphics_power_menu_top(void)
{
    uint16_t top = 12;

    if (FB_HEIGHT > TASKBAR_HEIGHT + 116) {
        top = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 116);
    }
    return top;
}

static bool graphics_is_separator(char ch)
{
    return ch == PATH_SEPARATOR;
}

static uint16_t graphics_clamp_window_x(uint16_t width)
{
    if (FB_WIDTH <= width + 16) {
        return 8;
    }
    return (uint16_t) ((FB_WIDTH - width) / 2u);
}

static uint16_t graphics_clamp_window_y(uint16_t height)
{
    uint16_t top = 24;
    uint16_t bottom = TASKBAR_HEIGHT + 16;

    if (FB_HEIGHT <= height + top + bottom) {
        return top;
    }
    return (uint16_t) ((FB_HEIGHT - height - bottom) / 2u);
}

static void graphics_reset_login(void)
{
    strcpy(g_login_username, "root");
    g_login_password[0] = '\0';
    g_login_field = 1;
    g_login_error = false;
}

static void graphics_copy_uac_text(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t index = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < dst_size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

static void graphics_reset_file_browser(void)
{
    strcpy(g_file_current_path, UI_SYSTEM_ROOT);
    g_file_item_count = 0;
    g_file_selected_index = 0;
    g_file_scroll_offset = 0;
}

static uint32_t graphics_file_visible_rows(const ui_window_t *window)
{
    uint32_t content_height;

    if (window == NULL || window->height <= 78) {
        return 1;
    }
    content_height = window->height - 78;
    return content_height / 22u == 0 ? 1 : content_height / 22u;
}

static uint32_t graphics_file_max_scroll(const ui_window_t *window)
{
    uint32_t visible_rows = graphics_file_visible_rows(window);

    return g_file_item_count > visible_rows ? g_file_item_count - visible_rows : 0;
}

static void graphics_file_clamp_scroll(const ui_window_t *window)
{
    uint32_t max_scroll = graphics_file_max_scroll(window);

    if (g_file_scroll_offset > max_scroll) {
        g_file_scroll_offset = max_scroll;
    }
    if (g_file_item_count == 0) {
        g_file_scroll_offset = 0;
    }
}

static void graphics_file_ensure_selected_visible(const ui_window_t *window)
{
    uint32_t visible_rows = graphics_file_visible_rows(window);
    uint32_t max_scroll;

    graphics_file_clamp_scroll(window);
    max_scroll = graphics_file_max_scroll(window);
    if (g_file_item_count == 0) {
        return;
    }
    if (g_file_selected_index < g_file_scroll_offset) {
        g_file_scroll_offset = g_file_selected_index;
    } else if (g_file_selected_index >= g_file_scroll_offset + visible_rows) {
        g_file_scroll_offset = g_file_selected_index - visible_rows + 1;
    }
    if (g_file_scroll_offset > max_scroll) {
        g_file_scroll_offset = max_scroll;
    }
}

static void graphics_file_scroll_by(int32_t delta)
{
    int32_t next;
    int32_t max_scroll;
    int32_t window_index = graphics_find_window(UI_WINDOW_FILES);
    const ui_window_t *window = window_index >= 0 ? &g_windows[window_index] : NULL;

    max_scroll = (int32_t) graphics_file_max_scroll(window);
    next = (int32_t) g_file_scroll_offset + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > max_scroll) {
        next = max_scroll;
    }
    g_file_scroll_offset = (uint32_t) next;
}

static uint32_t graphics_terminal_visible_rows(const ui_window_t *window)
{
    uint32_t content_height;
    uint32_t rows;

    if (window == NULL || window->height <= 46) {
        return 1;
    }
    content_height = window->height - 46;
    rows = content_height / 14u;
    if (rows == 0) {
        rows = 1;
    }
    return rows > GRAPHICS_CONSOLE_ROWS ? GRAPHICS_CONSOLE_ROWS : rows;
}

static uint32_t graphics_terminal_content_rows(const ui_window_t *window)
{
    int32_t owner_pid = window != NULL ? window->owner_pid : -1;
    uint32_t rows = (uint32_t) console_cursor_row_for_pid(owner_pid) + 1u;

    if (rows == 0) {
        rows = 1;
    }
    return rows > GRAPHICS_CONSOLE_ROWS ? GRAPHICS_CONSOLE_ROWS : rows;
}

static uint32_t graphics_terminal_max_scroll(const ui_window_t *window)
{
    uint32_t visible_rows = graphics_terminal_visible_rows(window);
    uint32_t content_rows = graphics_terminal_content_rows(window);

    return content_rows > visible_rows ? content_rows - visible_rows : 0;
}

static void graphics_terminal_scroll_by(int32_t delta, ui_window_t *window)
{
    int32_t next;
    int32_t max_scroll = (int32_t) graphics_terminal_max_scroll(window);

    if (window == NULL) {
        return;
    }
    next = (int32_t) window->console_scroll_offset + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > max_scroll) {
        next = max_scroll;
    }
    window->console_scroll_offset = (uint32_t) next;
    window->console_scroll_manual = window->console_scroll_offset < (uint32_t) max_scroll;
}

static void graphics_current_desktop_path(char output[GRAPHICS_FILE_PATH_MAX])
{
    const session_user_t *user = session_current_user();

    strcpy(output, UI_ROOT_DESKTOP);
    if (user != NULL && user->home[0] != '\0') {
        strcpy(output, user->home);
        graphics_append_path_component(output, GRAPHICS_FILE_PATH_MAX, "Desktop");
    }
}

static uint32_t graphics_text_width(const char *text)
{
    return font_text_width(text);
}

static void graphics_draw_text_aligned(uint16_t x, uint16_t y, uint16_t width, const char *text, uint32_t color)
{
    uint32_t text_width = graphics_text_width(text);
    uint16_t draw_x = x;

    if (text_width < width) {
        draw_x = (uint16_t) (x + (width - text_width) / 2);
    }
    graphics_draw_text(draw_x, y, text, color);
}

static void graphics_draw_text_clipped(uint16_t x, uint16_t y, uint16_t width, const char *text, uint32_t color)
{
    char clipped[GRAPHICS_WINDOW_TITLE_MAX];
    uint32_t used = 0;
    uint32_t drawn_width = 0;
    const char *cursor = text;

    if (text == NULL || width == 0) {
        return;
    }
    while (*cursor != '\0' && used + 1 < sizeof(clipped)) {
        const char *start = cursor;
        uint32_t codepoint = font_utf8_next(&cursor);
        uint32_t advance = font_codepoint_advance(codepoint);

        if (drawn_width + advance > width) {
            break;
        }
        while (start < cursor && used + 1 < sizeof(clipped)) {
            clipped[used++] = *start++;
        }
        drawn_width += advance;
    }
    clipped[used] = '\0';
    graphics_draw_text(x, y, clipped, color);
}

static void graphics_append_path_component(char *path, uint32_t path_size, const char *component)
{
    uint32_t len = (uint32_t) strlen(path);

    if (len > 3 && path[len - 1] != PATH_SEPARATOR && len + 1 < path_size) {
        path[len++] = PATH_SEPARATOR;
        path[len] = '\0';
    }
    if (len + strlen(component) < path_size) {
        strcpy(path + len, component);
    }
}

static bool graphics_pop_path_component(char *path)
{
    uint32_t len;

    if (path == NULL || path[0] == '\0' || strcmp(path, PATH_ROOT) == 0) {
        return false;
    }
    len = (uint32_t) strlen(path);
    while (len > 3 && path[len - 1] == PATH_SEPARATOR) {
        len--;
    }
    while (len > 3 && path[len - 1] != PATH_SEPARATOR) {
        len--;
    }
    if (len <= 3) {
        strcpy(path, PATH_ROOT);
        return true;
    }
    path[len] = '\0';
    return true;
}

static uint32_t graphics_file_name_from_line(const char *line, char *name, uint32_t name_size)
{
    uint32_t len = 0;

    if (name_size == 0) {
        return 0;
    }
    while (line[len] != '\0' && line[len] != '\r' && line[len] != '\n' && len + 1 < name_size) {
        name[len] = line[len];
        len++;
    }
    name[len] = '\0';
    return len;
}

static void graphics_fill_file_browser(void)
{
    uint32_t count = 0;
    char buffer[GRAPHICS_FILE_LIST_BUFFER];

    g_file_item_count = 0;
    if (!file_list_dir(g_file_current_path, buffer, sizeof(buffer))) {
        return;
    }

    for (uint32_t i = 0; buffer[i] != '\0' && count < GRAPHICS_FILE_ITEM_MAX; i++) {
        uint32_t line_start = i;
        uint32_t line_len = 0;

        while (buffer[i] != '\0' && buffer[i] != '\n') {
            i++;
        }
        line_len = i - line_start;
        if (line_len == 0) {
            continue;
        }
        if (line_len >= sizeof(g_file_items[count].name)) {
            line_len = sizeof(g_file_items[count].name) - 1;
        }
        memcpy(g_file_items[count].name, &buffer[line_start], line_len);
        g_file_items[count].name[line_len] = '\0';
        g_file_items[count].is_dir = line_len > 0 && graphics_is_separator(g_file_items[count].name[line_len - 1]);
        if (g_file_items[count].is_dir) {
            g_file_items[count].name[line_len - 1] = '\0';
        }
        count++;
    }
    g_file_item_count = count;
    if (g_file_selected_index >= g_file_item_count && g_file_item_count > 0) {
        g_file_selected_index = g_file_item_count - 1;
    }
    {
        int32_t window_index = graphics_find_window(UI_WINDOW_FILES);
        graphics_file_ensure_selected_visible(window_index >= 0 ? &g_windows[window_index] : NULL);
    }
}

static void graphics_fill_player_browser(void)
{
    uint32_t count = 0;
    char buffer[512];

    g_player_item_count = 0;
    if (!file_list_dir(g_player_current_path, buffer, sizeof(buffer))) {
        strcpy(g_player_status, "open dir failed");
        return;
    }

    for (uint32_t i = 0; buffer[i] != '\0' && count < DESKTOP_LABEL_MAX; i++) {
        uint32_t line_start = i;
        uint32_t line_len = 0;

        while (buffer[i] != '\0' && buffer[i] != '\n') {
            i++;
        }
        line_len = i - line_start;
        if (line_len == 0) {
            continue;
        }
        if (line_len >= sizeof(g_player_items[count].name)) {
            line_len = sizeof(g_player_items[count].name) - 1;
        }
        memcpy(g_player_items[count].name, &buffer[line_start], line_len);
        g_player_items[count].name[line_len] = '\0';
        g_player_items[count].is_dir = line_len > 0 && graphics_is_separator(g_player_items[count].name[line_len - 1]);
        if (g_player_items[count].is_dir) {
            g_player_items[count].name[line_len - 1] = '\0';
        }
        if (g_player_items[count].is_dir ||
            graphics_path_has_suffix(g_player_items[count].name, ".wav") ||
            graphics_path_has_suffix(g_player_items[count].name, ".WAV") ||
            graphics_path_has_suffix(g_player_items[count].name, ".m4a") ||
            graphics_path_has_suffix(g_player_items[count].name, ".M4A")) {
            count++;
        }
    }
    g_player_item_count = count;
    if (g_player_selected_index >= g_player_item_count && g_player_item_count > 0) {
        g_player_selected_index = g_player_item_count - 1;
    }
}

static void graphics_player_open_browser(void)
{
    if (g_player_current_path[0] == '\0') {
        strcpy(g_player_current_path, UI_ROOT_DESKTOP);
    }
    g_player_browser_open = true;
    g_player_selected_index = 0;
    strcpy(g_player_status, "select audio file");
    graphics_fill_player_browser();
}

static bool graphics_player_path_is_audio(const char *path)
{
    return graphics_path_has_suffix(path, ".wav") ||
           graphics_path_has_suffix(path, ".WAV") ||
           graphics_path_has_suffix(path, ".m4a") ||
           graphics_path_has_suffix(path, ".M4A");
}

static bool graphics_player_try_play(const char *path)
{
    if (path == NULL || path[0] == '\0' || !graphics_player_path_is_audio(path)) {
        strcpy(g_player_status, "not an audio file");
        return false;
    }
    strcpy(g_player_selected_path, path);
    if (audio_play_file(path)) {
        strcpy(g_player_status, "playing");
        g_player_browser_open = false;
        return true;
    }
    strcpy(g_player_status, "play failed");
    return false;
}

static bool graphics_attempt_login(void)
{
    if (!session_validate_credentials(g_login_username, g_login_password)) {
        g_login_error = true;
        g_login_password[0] = '\0';
        return false;
    }
    g_login_error = false;
    g_login_password[0] = '\0';
    g_session_logged_in = true;
    graphics_reset_file_browser();
    graphics_fill_file_browser();
    memset(g_windows, 0, sizeof(g_windows));
    graphics_open_window(UI_WINDOW_FILES);
    return true;
}

static void graphics_reset_uac_request(const char *program_path, const char *reason, uint32_t privilege_level)
{
    memset(g_uac_program, 0, sizeof(g_uac_program));
    memset(g_uac_reason, 0, sizeof(g_uac_reason));
    memset(g_uac_password, 0, sizeof(g_uac_password));
    g_uac_password_len = 0;
    g_uac_error = false;
    g_uac_pending = true;
    g_uac_result_ready = false;
    g_uac_result = false;
    g_uac_last_buttons = 0;
    g_uac_privilege_level = privilege_level;
    if (program_path != NULL) {
        strlcpy(g_uac_program, program_path, sizeof(g_uac_program));
    }
    if (reason != NULL) {
        strlcpy(g_uac_reason, reason, sizeof(g_uac_reason));
    }
}

static bool graphics_finish_uac_request(bool accepted)
{
    g_uac_pending = false;
    g_uac_result = accepted;
    g_uac_result_ready = true;
    if (accepted) {
        g_uac_error = false;
    }
    return accepted;
}

bool graphics_request_uac_elevation(const char *program_path, const char *reason, uint32_t privilege_level)
{
    int32_t window_index;

    if (!g_graphics_active) {
        return false;
    }
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_UAC) {
            graphics_close_window(i);
        }
    }
    graphics_reset_uac_request(program_path, reason, privilege_level);
    graphics_open_window(UI_WINDOW_UAC);
    graphics_draw_shell();
    window_index = graphics_find_window(UI_WINDOW_UAC);
    if (window_index < 0) {
        return graphics_finish_uac_request(false);
    }

    while (g_uac_pending) {
        key_event_t event;
        mouse_snapshot_t snapshot;
        uint64_t last_ticks = timer_ticks();
        const ui_window_t *window = &g_windows[window_index];
        while (keyboard_poll_event(&event)) {
            if (event.type != KEY_EVENT_CHAR) {
                if (event.type == KEY_EVENT_ESC) {
                    graphics_close_window((uint32_t) window_index);
                    asm volatile ("sti");
                    return graphics_finish_uac_request(false);
                }
                continue;
            }
            if (!g_uac_input_focus) {
                continue;
            }
            if (event.ch == '\b') {
                if (g_uac_password_len > 0) {
                    g_uac_password[--g_uac_password_len] = '\0';
                }
            } else if (event.ch == '\n') {
                bool ok = session_verify_password(g_uac_password);

                if (ok) {
                    graphics_close_window((uint32_t) window_index);
                    asm volatile ("sti");
                    return graphics_finish_uac_request(true);
                }
                g_uac_error = true;
                g_uac_password_len = 0;
                g_uac_password[0] = '\0';
            } else if (event.ch >= 32 && event.ch <= 126 && g_uac_password_len + 1 < sizeof(g_uac_password)) {
                g_uac_password[g_uac_password_len++] = event.ch;
                g_uac_password[g_uac_password_len] = '\0';
            }
            graphics_draw_shell();
        }

        mouse_get_snapshot(&snapshot);
        if (!window->visible || window->kind != UI_WINDOW_UAC) {
            asm volatile ("sti");
            return graphics_finish_uac_request(false);
        }
        if ((snapshot.buttons & MOUSE_BUTTON_LEFT) != 0 && (g_uac_last_buttons & MOUSE_BUTTON_LEFT) == 0) {
            uint16_t rel_x = (uint16_t) (snapshot.x_pixels >= window->x ? (snapshot.x_pixels - window->x) : 0);
            uint16_t rel_y = (uint16_t) (snapshot.y_pixels >= window->y ? (snapshot.y_pixels - window->y) : 0);

            if (rel_x >= window->width - 29 && rel_x < window->width - 13 && rel_y >= 3 && rel_y < 17) {
                graphics_close_window((uint32_t) window_index);
                asm volatile ("sti");
                return graphics_finish_uac_request(false);
            }
            if (rel_x >= 16 && rel_x < window->width - 16 && rel_y >= 108 && rel_y < 132) {
                g_uac_input_focus = true;
            }
            if (rel_x >= 200 && rel_x < 272 && rel_y >= 150 && rel_y < 172) {
                bool ok = session_verify_password(g_uac_password);

                if (ok) {
                    graphics_close_window((uint32_t) window_index);
                    asm volatile ("sti");
                    return graphics_finish_uac_request(true);
                }
                g_uac_error = true;
                g_uac_password_len = 0;
                g_uac_password[0] = '\0';
            }
            if (rel_x >= 282 && rel_x < 354 && rel_y >= 150 && rel_y < 172) {
                graphics_close_window((uint32_t) window_index);
                asm volatile ("sti");
                return graphics_finish_uac_request(false);
            }
            graphics_draw_shell();
        }
        g_uac_last_buttons = snapshot.buttons;

        if (timer_ticks() == last_ticks) {
            asm volatile ("sti; hlt; cli");
        }
    }

    asm volatile ("sti");
    return g_uac_result;
}

static void graphics_file_browser_enter_selected(void)
{
    char path[GRAPHICS_FILE_PATH_MAX];

    if (g_file_selected_index >= g_file_item_count) {
        return;
    }
    strcpy(path, g_file_current_path);
    graphics_append_path_component(path, sizeof(path), g_file_items[g_file_selected_index].name);
    if (g_file_items[g_file_selected_index].is_dir) {
        strcpy(g_file_current_path, path);
        g_file_selected_index = 0;
        graphics_fill_file_browser();
        return;
    }
    graphics_open_path(path);
}

static void graphics_file_browser_go_up(void)
{
    if (graphics_pop_path_component(g_file_current_path)) {
        g_file_selected_index = 0;
        graphics_fill_file_browser();
    }
}

static bool graphics_path_has_suffix(const char *path, const char *suffix)
{
    uint32_t path_len = (uint32_t) strlen(path);
    uint32_t suffix_len = (uint32_t) strlen(suffix);

    if (suffix_len > path_len) {
        return false;
    }
    return strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool graphics_copy_file_path(const char *src, const char *dst)
{
    int32_t size;
    char *buffer;

    if (src == NULL || dst == NULL || file_is_dir(src)) {
        return false;
    }
    size = file_size(src);
    if (size < 0) {
        return false;
    }
    buffer = (char *) kmalloc((uint32_t) (size == 0 ? 1 : size));
    if (buffer == NULL) {
        return false;
    }
    if (size > 0 && file_read(src, buffer, (uint32_t) size) != size) {
        kfree(buffer);
        return false;
    }
    if (file_write(dst, buffer, (uint32_t) size) < 0) {
        kfree(buffer);
        return false;
    }
    kfree(buffer);
    return true;
}

static void graphics_build_desktop_entry_path(uint32_t index, char out_path[GRAPHICS_CLIPBOARD_PATH_MAX])
{
    char desktop_path[GRAPHICS_FILE_PATH_MAX];

    graphics_current_desktop_path(desktop_path);
    strcpy(out_path, desktop_path);
    graphics_append_path_component(out_path, GRAPHICS_CLIPBOARD_PATH_MAX, g_desktop_entries[index].name);
}

static void graphics_build_file_entry_path(uint32_t index, char out_path[GRAPHICS_CLIPBOARD_PATH_MAX])
{
    strcpy(out_path, g_file_current_path);
    graphics_append_path_component(out_path, GRAPHICS_CLIPBOARD_PATH_MAX, g_file_items[index].name);
}

static void graphics_clipboard_set(const char *path, bool is_dir, ui_clipboard_mode_t mode)
{
    if (path == NULL) {
        g_clipboard_mode = UI_CLIPBOARD_NONE;
        g_clipboard_path[0] = '\0';
        g_clipboard_is_dir = false;
        return;
    }
    strcpy(g_clipboard_path, path);
    g_clipboard_is_dir = is_dir;
    g_clipboard_mode = mode;
}

static const char *graphics_path_basename(const char *path)
{
    const char *last = path;

    if (path == NULL) {
        return "";
    }
    while (*path != '\0') {
        if (*path == PATH_SEPARATOR) {
            last = path + 1;
        }
        path++;
    }
    return last;
}

static void graphics_trim_shortcut_target(char *text)
{
    uint32_t start = 0;
    uint32_t end;

    if (text == NULL) {
        return;
    }
    while (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n') {
        start++;
    }
    end = (uint32_t) strlen(text + start);
    while (end > 0) {
        char ch = text[start + end - 1];

        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        end--;
    }
    if (start > 0 && end > 0) {
        memmove(text, text + start, end);
    }
    text[end] = '\0';
}

static bool graphics_resolve_shortcut(const char *path, char *target, uint32_t target_size)
{
    int32_t read;

    if (path == NULL || target == NULL || target_size == 0 || file_is_dir(path)) {
        return false;
    }
    if (!graphics_path_has_suffix(path, ".lnk") && !graphics_path_has_suffix(path, ".LNK")) {
        return false;
    }
    read = file_read(path, target, target_size - 1);
    if (read <= 0) {
        return false;
    }
    target[read] = '\0';
    graphics_trim_shortcut_target(target);
    return target[0] != '\0';
}

static bool graphics_shortcut_char_allowed(char ch)
{
    return (ch >= '0' && ch <= '9') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z');
}

static char graphics_shortcut_char_normalize(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return (char) (ch - 'A' + 'a');
    }
    if (graphics_shortcut_char_allowed(ch)) {
        return ch;
    }
    return '_';
}

static void graphics_make_shortcut_filename(const char *target_path, uint32_t attempt, char *out, uint32_t out_size)
{
    const char *base = graphics_path_basename(target_path);
    uint32_t out_index = 0;
    uint32_t max_base = attempt == 0 ? 8u : 7u;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (target_path == NULL || target_path[0] == '\0' || out_size < 5) {
        return;
    }
    while (base[out_index] != '\0' && base[out_index] != '.' && out_index < max_base && out_index + 5 < out_size) {
        out[out_index] = graphics_shortcut_char_normalize(base[out_index]);
        out_index++;
    }
    if (out_index == 0) {
        out[out_index++] = 'l';
        out[out_index++] = 'i';
        out[out_index++] = 'n';
        out[out_index++] = 'k';
    }
    if (attempt > 0 && out_index + 5 < out_size) {
        out[out_index++] = (char) ('0' + (attempt % 10u));
    }
    strcpy(out + out_index, ".lnk");
}

static bool graphics_create_desktop_shortcut(const char *target_path)
{
    char desktop_path[GRAPHICS_FILE_PATH_MAX];
    char link_name[16];
    char link_path[GRAPHICS_CLIPBOARD_PATH_MAX];
    char body[GRAPHICS_CLIPBOARD_PATH_MAX + 2];

    if (target_path == NULL || target_path[0] == '\0') {
        return false;
    }
    graphics_current_desktop_path(desktop_path);
    if (!file_exists(desktop_path)) {
        file_mkdir(desktop_path);
    }
    for (uint32_t attempt = 0; attempt < 10; attempt++) {
        graphics_make_shortcut_filename(target_path, attempt, link_name, sizeof(link_name));
        if (link_name[0] == '\0') {
            return false;
        }
        strcpy(link_path, desktop_path);
        graphics_append_path_component(link_path, sizeof(link_path), link_name);
        if (file_exists(link_path)) {
            continue;
        }
        strcpy(body, target_path);
        strcpy(body + strlen(body), "\n");
        return file_write(link_path, body, (uint32_t) strlen(body)) >= 0;
    }
    return false;
}

static bool graphics_launch_user_program(const char *path)
{
    if (exec_active() ||
        path == NULL ||
        path[0] == '\0' ||
        !file_exists(path) ||
        file_is_dir(path)) {
        return false;
    }

    shell_exec_path(path);
    return true;
}

static bool graphics_launch_user_program_with_arg(const char *path, const char *arg)
{
    if (exec_active() ||
        path == NULL ||
        path[0] == '\0' ||
        arg == NULL ||
        arg[0] == '\0' ||
        !file_exists(path) || file_is_dir(path)) {
        return false;
    }

    shell_exec_path_with_arg(path, arg);
    return true;
}

static bool graphics_resolve_app_executable_path(const char *path, char *resolved, uint32_t resolved_size)
{
    const char *base;
    uint32_t base_len;

    if (path == NULL || resolved == NULL || resolved_size == 0 || !graphics_path_is_executable(path)) {
        return false;
    }
    base = graphics_path_basename(path);
    if (base == NULL || base[0] == '\0') {
        return false;
    }
    base_len = (uint32_t) strlen(base);
    if (base_len == 0 || base_len + 7 > resolved_size) {
        return false;
    }

    strcpy(resolved, UI_APPS_DIR);
    graphics_append_path_component(resolved, resolved_size, base);
    return file_exists(resolved) && !file_is_dir(resolved);
}

static bool graphics_launch_named_user_program(const char *name)
{
    typedef struct {
        const char *name;
        const char *path;
    } app_alias_t;
    static const app_alias_t apps[] = {
        { "explorar", UI_EXPLORER_PATH },
        { "explorar.exe", UI_EXPLORER_PATH },
        { "monilog", UI_LOGON_PATH },
        { "monilog.exe", UI_LOGON_PATH },
        { "demo", UI_APPS_DIR PATH_SEPARATOR_STR "demo.exe" },
        { "demo.exe", UI_APPS_DIR PATH_SEPARATOR_STR "demo.exe" },
        { "player", UI_PLAYER_PATH },
        { "player.exe", UI_PLAYER_PATH },
        { "notepad", UI_NOTEPAD_PATH },
        { "notepad.exe", UI_NOTEPAD_PATH },
        { "square", UI_APPS_DIR PATH_SEPARATOR_STR "square.exe" },
        { "square.exe", UI_APPS_DIR PATH_SEPARATOR_STR "square.exe" },
        { "cube3d", UI_APPS_DIR PATH_SEPARATOR_STR "cube3d.exe" },
        { "cube3d.exe", UI_APPS_DIR PATH_SEPARATOR_STR "cube3d.exe" },
        { "setup", UI_SETUP_PATH },
        { "setup.exe", UI_SETUP_PATH },
        { "appdev", UI_APPS_DIR PATH_SEPARATOR_STR "appdev.exe" },
        { "appdev.exe", UI_APPS_DIR PATH_SEPARATOR_STR "appdev.exe" },
    };

    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (uint32_t i = 0; i < sizeof(apps) / sizeof(apps[0]); i++) {
        if (strcasecmp(name, apps[i].name) == 0) {
            return graphics_launch_user_program(apps[i].path);
        }
    }
    return false;
}

static void graphics_context_open(uint16_t x, uint16_t y, ui_context_menu_mode_t mode)
{
    uint16_t menu_h = mode == UI_CONTEXT_MENU_FILES ? CONTEXT_MENU_FILES_H : CONTEXT_MENU_DESKTOP_H;

    g_start_menu_open = false;
    g_context_menu_open = true;
    g_context_menu_mode = mode;
    g_context_menu_x = x > FB_WIDTH - CONTEXT_MENU_W ? (uint16_t) (FB_WIDTH - CONTEXT_MENU_W) : x;
    g_context_menu_y = y > FB_HEIGHT - TASKBAR_HEIGHT - menu_h ? (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - menu_h) : y;
}

static void graphics_open_start_menu_view(ui_start_menu_view_t view)
{
    g_start_menu_open = true;
    g_start_menu_view = view;
    g_context_menu_open = false;
    g_power_menu_open = false;
}

static bool graphics_handle_start_menu_click(uint16_t x, uint16_t y)
{
    uint16_t menu_x = START_MENU_X;
    uint16_t menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - START_MENU_H - 12);
    uint16_t root_top = (uint16_t) (menu_y + START_MENU_HEADER_H + 8);

    if (!g_start_menu_open) {
        return false;
    }
    if (!graphics_point_in_rect(x, y, menu_x, menu_y, START_MENU_W, START_MENU_H)) {
        g_start_menu_open = false;
        g_power_menu_open = false;
        g_context_menu_open = false;
        return false;
    }

    switch (g_start_menu_view) {
    case UI_START_MENU_ROOT:
        if (graphics_point_in_rect(x, y, menu_x + 8, root_top + 0 * UI_MENU_ITEM_H, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_APPS;
            return true;
        }
        if (graphics_point_in_rect(x, y, menu_x + 8, root_top + 1 * UI_MENU_ITEM_H, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_SETTINGS;
            return true;
        }
        if (graphics_point_in_rect(x, y, menu_x + 8, root_top + 2 * UI_MENU_ITEM_H, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_NETWORK;
            return true;
        }
        if (graphics_point_in_rect(x, y, menu_x + 8, root_top + 3 * UI_MENU_ITEM_H, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_POWER;
            return true;
        }
        break;
    case UI_START_MENU_APPS:
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 0 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            graphics_launch_user_program(UI_EXPLORER_PATH);
            g_start_menu_open = false;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 1 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            graphics_open_window(UI_WINDOW_TERMINAL);
            g_start_menu_open = false;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 2 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            graphics_launch_user_program(UI_PLAYER_PATH);
            g_start_menu_open = false;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 3 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            graphics_launch_user_program(UI_NOTEPAD_PATH);
            g_start_menu_open = false;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 4 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            graphics_open_task_manager();
            g_start_menu_open = false;
            return true;
        }
        break;
    case UI_START_MENU_SETTINGS:
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 0 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_DISPLAY;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 1 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_CURSOR;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 2 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            g_start_menu_view = UI_START_MENU_NETWORK;
            return true;
        }
        break;
    case UI_START_MENU_DISPLAY:
        for (uint32_t mode = 0; mode < sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]); mode++) {
            uint16_t row_y = (uint16_t) (root_top + mode * UI_MENU_ITEM_H);
            if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + START_MENU_MID_W + 16), row_y, START_MENU_RIGHT_W - 24, UI_MENU_ITEM_H - 2)) {
                graphics_set_resolution(g_graphics_modes[mode].width, g_graphics_modes[mode].height);
                g_start_menu_open = false;
                return true;
            }
        }
        break;
    case UI_START_MENU_CURSOR:
        for (uint32_t style = 0; style < graphics_cursor_style_count(); style++) {
            uint16_t col = (uint16_t) (style & 1u);
            uint16_t row = (uint16_t) (style >> 1);
            uint16_t button_x = (uint16_t) (menu_x + START_MENU_LEFT_W + START_MENU_MID_W + 16 + col * 78);
            uint16_t button_y = (uint16_t) (root_top + row * 28);

            if (graphics_point_in_rect(x, y, button_x, button_y, 68, 22)) {
                graphics_set_cursor_style((uint8_t) style);
                g_start_menu_open = false;
                return true;
            }
        }
        break;
    case UI_START_MENU_NETWORK:
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 0 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            ui_network_update(timer_ticks());
            g_start_menu_open = false;
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 1 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            g_start_menu_open = false;
            graphics_open_window(UI_WINDOW_CONTROL_PANEL);
            return true;
        }
        break;
    case UI_START_MENU_POWER:
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 0 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            kernel_request_shutdown();
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 1 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            kernel_request_reboot();
            return true;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (menu_x + START_MENU_LEFT_W + 12), (uint16_t) (root_top + 2 * UI_MENU_ITEM_H), START_MENU_MID_W - 24, UI_MENU_ITEM_H - 2)) {
            kernel_request_sleep();
            g_start_menu_open = false;
            return true;
        }
        break;
    default:
        break;
    }
    g_start_menu_open = false;
    g_start_menu_view = UI_START_MENU_ROOT;
    return false;
}

static void graphics_open_path(const char *path)
{
    char app_path[GRAPHICS_CLIPBOARD_PATH_MAX];
    char exec_path[GRAPHICS_CLIPBOARD_PATH_MAX];
    char shortcut_target[GRAPHICS_CLIPBOARD_PATH_MAX];
    const char *open_path = path;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    if (graphics_resolve_shortcut(path, shortcut_target, sizeof(shortcut_target))) {
        open_path = shortcut_target;
    }
    if (file_is_dir(open_path)) {
        strcpy(g_file_current_path, open_path);
        g_file_selected_index = 0;
        graphics_fill_file_browser();
        graphics_open_window(UI_WINDOW_FILES);
        return;
    }
    if (strcasecmp(graphics_path_basename(open_path), "taskmgr.exe") == 0) {
        graphics_open_task_manager();
        return;
    }
    if (graphics_path_is_executable(open_path)) {
        if (graphics_resolve_app_executable_path(open_path, exec_path, sizeof(exec_path))) {
            graphics_launch_user_program(exec_path);
        } else {
            graphics_launch_user_program(open_path);
        }
        return;
    }
    if (!file_is_dir(open_path) &&
        registry_default_app_for_path(open_path, app_path, sizeof(app_path)) &&
        file_exists(app_path) && !file_is_dir(app_path)) {
        graphics_launch_user_program_with_arg(app_path, open_path);
        return;
    }
    if (graphics_path_is_package(open_path)) {
        graphics_launch_user_program(open_path);
    }
}

static void graphics_run_command_text(const char *command)
{
    if (command == NULL || command[0] == '\0') {
        return;
    }
    if (strcasecmp(command, "command") == 0 ||
        strcasecmp(command, "cmd") == 0 ||
        strcasecmp(command, "console") == 0 ||
        strcasecmp(command, "terminal") == 0) {
        graphics_open_window(UI_WINDOW_TERMINAL);
    } else if (graphics_launch_named_user_program(command)) {
        return;
    } else if (strcasecmp(command, "about") == 0) {
        graphics_open_window(UI_WINDOW_ABOUT);
    } else {
        if (graphics_find_window(UI_WINDOW_TERMINAL) < 0) {
            graphics_open_window(UI_WINDOW_TERMINAL);
        }
        shell_exec_path(command);
    }
}

static bool graphics_notepad_load_file(const char *path)
{
    int32_t size;

    if (path == NULL || file_is_dir(path)) {
        return false;
    }
    size = file_read(path, g_notepad_text, sizeof(g_notepad_text) - 1);
    if (size < 0) {
        return false;
    }
    g_notepad_len = (uint32_t) size;
    g_notepad_text[g_notepad_len] = '\0';
    strcpy(g_notepad_path, path);
    return true;
}

static bool graphics_notepad_save_file(void)
{
    if (g_notepad_path[0] == '\0') {
        return false;
    }
    return file_write(g_notepad_path, g_notepad_text, g_notepad_len) >= 0;
}

static uint32_t graphics_pci_memory_bar_base(uint32_t bar)
{
    if ((bar & 0x1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static void graphics_log_device_info(const graphics_pci_device_t *dev)
{
    char msg[64] = "graphics pci: vend=0x";
    char vendor_hex[9];
    char device_hex[9];
    char bar0_hex[9];
    char bar1_hex[9];

    graphics_u32_to_hex8(dev->vendor, vendor_hex);
    graphics_u32_to_hex8(dev->device, device_hex);
    graphics_u32_to_hex8(dev->bar0, bar0_hex);
    graphics_u32_to_hex8(dev->bar1, bar1_hex);

    memcpy(msg + 21, vendor_hex, 8);
    memcpy(msg + 29, " dev=0x", 7);
    memcpy(msg + 36, device_hex, 8);
    msg[44] = '\0';
    log_write(msg);

    memcpy(msg, "graphics pci: bar0=0x", 21);
    memcpy(msg + 21, bar0_hex, 8);
    memcpy(msg + 29, " bar1=0x", 8);
    memcpy(msg + 37, bar1_hex, 8);
    msg[45] = '\0';
    log_write(msg);
}

static graphics_pci_device_t graphics_find_display_device(void)
{
    graphics_pci_device_t dev;
    memset(&dev, 0, sizeof(dev));

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_config_read32((uint8_t) bus, slot, func, 0x00);
                uint32_t class_reg;

                if ((id & 0xFFFFu) == 0xFFFFu) {
                    if (func == 0) {
                        break;
                    }
                    continue;
                }

                class_reg = pci_config_read32((uint8_t) bus, slot, func, 0x08);
                if (((class_reg >> 24) & 0xFFu) != 0x03u) {
                    continue;
                }
                dev.vendor = (uint16_t) (id & 0xFFFFu);
                dev.device = (uint16_t) ((id >> 16) & 0xFFFFu);
                dev.bar0 = pci_config_read32((uint8_t) bus, slot, func, 0x10);
                dev.bar1 = pci_config_read32((uint8_t) bus, slot, func, 0x14);
                dev.found = true;
                return dev;
            }
        }
    }
    dev.bar1 = 0xE0000000u;
    return dev;
}

static uint32_t graphics_find_framebuffer_address(const graphics_pci_device_t *dev)
{
    uint32_t bar0_addr = graphics_pci_memory_bar_base(dev->bar0);
    uint32_t bar1_addr = graphics_pci_memory_bar_base(dev->bar1);

    if (dev->vendor == PCI_VENDOR_VMWARE && dev->device == PCI_DEVICE_VMWARE_SVGA2) {
        if (bar1_addr != 0) {
            return bar1_addr;
        }
        if (bar0_addr != 0) {
            return bar0_addr;
        }
        return 0xE0000000u;
    }

    if (bar0_addr != 0) {
        return bar0_addr;
    }
    if (bar1_addr != 0) {
        return bar1_addr;
    }
    return 0xE0000000u;
}

static bool graphics_framebuffer_address_valid(uint32_t addr)
{
    if (addr < 0x01000000u) {
        return false;
    }
    if (addr == 0xFFFFFFFFu) {
        return false;
    }
    return true;
}

static void svga_write(uint32_t reg, uint32_t value)
{
    outl(g_svga_io_base + 0, reg);
    outl(g_svga_io_base + 1, value);
}

static uint32_t svga_read(uint32_t reg)
{
    outl(g_svga_io_base + 0, reg);
    return inl(g_svga_io_base + 1);
}

static void bga_write(uint16_t index, uint16_t value)
{
    outw(BGA_INDEX_PORT, index);
    outw(BGA_DATA_PORT, value);
}

static uint16_t bga_read(uint16_t index)
{
    outw(BGA_INDEX_PORT, index);
    return inw(BGA_DATA_PORT);
}

static void graphics_log_bga_mode(void)
{
    char msg[96] = "graphics bga: id=0x";
    char hex[9];
    char dec[12];
    uint32_t pos = 19;
    uint16_t id = bga_read(BGA_ID);
    uint16_t enable = bga_read(BGA_ENABLE);
    uint16_t xres = bga_read(BGA_XRES);
    uint16_t yres = bga_read(BGA_YRES);
    uint16_t bpp = bga_read(BGA_BPP);

    graphics_u32_to_hex8(id, hex);
    memcpy(msg + pos, hex + 4, 4);
    pos += 4;
    memcpy(msg + pos, " enable=0x", 10);
    pos += 10;
    graphics_u32_to_hex8(enable, hex);
    memcpy(msg + pos, hex + 4, 4);
    pos += 4;
    memcpy(msg + pos, " mode=", 6);
    pos += 6;
    graphics_u32_to_dec(dec, xres);
    memcpy(msg + pos, dec, strlen(dec));
    pos += (uint32_t) strlen(dec);
    msg[pos++] = 'x';
    graphics_u32_to_dec(dec, yres);
    memcpy(msg + pos, dec, strlen(dec));
    pos += (uint32_t) strlen(dec);
    memcpy(msg + pos, " bpp=", 5);
    pos += 5;
    graphics_u32_to_dec(dec, bpp);
    memcpy(msg + pos, dec, strlen(dec));
    pos += (uint32_t) strlen(dec);
    msg[pos] = '\0';
    log_write(msg);
}

static void graphics_plot(uint16_t x, uint16_t y, uint32_t color)
{
    uint64_t index;

    if (!g_graphics_active || g_framebuffer == NULL || x >= FB_WIDTH || y >= FB_HEIGHT) {
        return;
    }
    index = (uint64_t) y * FB_WIDTH + x;
    if (index >= (uint64_t) FB_WIDTH * FB_HEIGHT) {
        return;
    }
    g_backbuffer[index] = color;
}

static uint64_t graphics_framebuffer_index(uint16_t x, uint16_t y)
{
    return (uint64_t) y * g_framebuffer_pitch_pixels + x;
}

static void graphics_present(void)
{
    if (!g_graphics_active || g_framebuffer == NULL) {
        return;
    }
    g_gpu_present_pending = true;
    g_gpu_submit_count++;
    graphics_flush_gpu();
}

void graphics_flush_gpu(void)
{
    if (!g_graphics_active || g_framebuffer == NULL) {
        return;
    }
    if (!g_gpu_present_pending) {
        return;
    }

    for (uint32_t y = 0; y < FB_HEIGHT; y++) {
        uint64_t dst_index = (uint64_t) y * g_framebuffer_pitch_pixels;
        uint64_t src_index = (uint64_t) y * FB_WIDTH;
        for (uint32_t x = 0; x < FB_WIDTH; x++) {
            g_framebuffer[dst_index + x] = g_backbuffer[src_index + x];
        }
    }
    g_gpu_present_pending = false;
    g_gpu_present_count++;
    /* update fps once per second */
    {
        uint64_t now = timer_ticks();
        if (g_last_fps_tick == 0) {
            g_last_fps_tick = now;
            g_present_snapshot = g_gpu_present_count;
        } else if (now - g_last_fps_tick >= timer_hz()) {
            uint32_t delta = g_gpu_present_count - g_present_snapshot;
            g_fps_value = delta;
            g_present_snapshot = g_gpu_present_count;
            g_last_fps_tick = now;
        }
    }
}

void graphics_set_double_buffer(bool enabled)
{
    g_double_buffer_enabled = enabled;
}

void graphics_set_vsync(bool enabled)
{
    g_vsync_enabled = enabled;
}

bool graphics_get_double_buffer(void)
{
    return g_double_buffer_enabled;
}

bool graphics_get_vsync(void)
{
    return g_vsync_enabled;
}

uint32_t graphics_get_fps(void)
{
    return g_fps_value;
}

static void graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            graphics_plot((uint16_t) (x + col), (uint16_t) (y + row), color);
        }
    }
}

static void graphics_fill_rect_gradient(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t top_color, uint32_t bottom_color)
{
    uint16_t den = height > 1 ? (uint16_t) (height - 1) : 1;

    for (uint16_t row = 0; row < height; row++) {
        uint32_t color = graphics_lerp_color(top_color, bottom_color, row, den);

        for (uint16_t col = 0; col < width; col++) {
            graphics_plot((uint16_t) (x + col), (uint16_t) (y + row), color);
        }
    }
}

static void graphics_fill_soft_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    if (width < 6 || height < 6) {
        graphics_fill_rect(x, y, width, height, color);
        return;
    }
    graphics_fill_rect((uint16_t) (x + 2), y, (uint16_t) (width - 4), height, color);
    graphics_fill_rect(x, (uint16_t) (y + 2), width, (uint16_t) (height - 4), color);
    graphics_plot((uint16_t) (x + 1), (uint16_t) (y + 1), color);
    graphics_plot((uint16_t) (x + width - 2), (uint16_t) (y + 1), color);
    graphics_plot((uint16_t) (x + 1), (uint16_t) (y + height - 2), color);
    graphics_plot((uint16_t) (x + width - 2), (uint16_t) (y + height - 2), color);
}

static void graphics_draw_soft_rect_outline(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    if (width < 6 || height < 6) {
        graphics_draw_rect_outline(x, y, width, height, color);
        return;
    }
    for (uint16_t col = 2; col + 2 < width; col++) {
        graphics_plot((uint16_t) (x + col), y, color);
        graphics_plot((uint16_t) (x + col), (uint16_t) (y + height - 1), color);
    }
    for (uint16_t row = 2; row + 2 < height; row++) {
        graphics_plot(x, (uint16_t) (y + row), color);
        graphics_plot((uint16_t) (x + width - 1), (uint16_t) (y + row), color);
    }
    graphics_plot((uint16_t) (x + 1), (uint16_t) (y + 1), color);
    graphics_plot((uint16_t) (x + width - 2), (uint16_t) (y + 1), color);
    graphics_plot((uint16_t) (x + 1), (uint16_t) (y + height - 2), color);
    graphics_plot((uint16_t) (x + width - 2), (uint16_t) (y + height - 2), color);
}

static void graphics_draw_shadow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    graphics_fill_soft_rect((uint16_t) (x + 7), (uint16_t) (y + 8), width, height, 0x00364664);
    graphics_draw_soft_rect_outline((uint16_t) (x + 3), (uint16_t) (y + 4), width, height, 0x008FA7C0);
}

static void graphics_reset_start_menu(void)
{
    g_start_menu_open = false;
    g_start_menu_view = UI_START_MENU_ROOT;
}

static void graphics_draw_rainbow_cat(void)
{
    static const uint32_t stripes[6] = {
        0x00FF5F5F, 0x00FFB45F, 0x00FFF45F, 0x0069E08A, 0x005FB7FF, 0x00B16BFF
    };
    uint16_t cat_x = (uint16_t) (FB_WIDTH / 2 - 84);
    uint16_t cat_y = (uint16_t) (FB_HEIGHT / 2 - 42);

    graphics_fill(0x00000000);
    for (uint16_t row = 0; row < 84; row++) {
        graphics_fill_rect(0, (uint16_t) (cat_y + row), FB_WIDTH, 1, stripes[(row / 14) % 6]);
    }
    graphics_fill_soft_rect(cat_x, cat_y, 168, 84, 0x00FFFDF8);
    graphics_draw_soft_rect_outline(cat_x, cat_y, 168, 84, 0x002E3440);
    graphics_fill_soft_rect((uint16_t) (cat_x + 14), (uint16_t) (cat_y + 16), 40, 40, 0x00FFF2E4);
    graphics_fill_soft_rect((uint16_t) (cat_x + 114), (uint16_t) (cat_y + 16), 40, 40, 0x00FFF2E4);
    graphics_fill_rect((uint16_t) (cat_x + 52), (uint16_t) (cat_y + 22), 64, 40, 0x00FFF8EA);
    graphics_fill_rect((uint16_t) (cat_x + 64), (uint16_t) (cat_y + 34), 16, 8, 0x00243B5B);
    graphics_fill_rect((uint16_t) (cat_x + 88), (uint16_t) (cat_y + 34), 16, 8, 0x00243B5B);
    graphics_draw_text((uint16_t) (cat_x + 58), (uint16_t) (cat_y + 56), "Nyan", 0x00243B5B);
}

bool graphics_user_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    if (!g_graphics_active || x >= FB_WIDTH || y >= FB_HEIGHT || width == 0 || height == 0) {
        return false;
    }
    if ((uint32_t) x + width > FB_WIDTH) {
        width = (uint16_t) (FB_WIDTH - x);
    }
    if ((uint32_t) y + height > FB_HEIGHT) {
        height = (uint16_t) (FB_HEIGHT - y);
    }
    graphics_fill_rect(x, y, width, height, color);
    return true;
}

bool graphics_user_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    if (!g_graphics_active || text == NULL || x >= FB_WIDTH || y >= FB_HEIGHT) {
        return false;
    }
    graphics_draw_text(x, y, text, color);
    return true;
}

void graphics_user_present(void)
{
    if (g_graphics_active) {
        graphics_present();
    }
}

static void graphics_draw_rect_outline(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    for (uint16_t col = 0; col < width; col++) {
        graphics_plot((uint16_t) (x + col), y, color);
        graphics_plot((uint16_t) (x + col), (uint16_t) (y + height - 1), color);
    }
    for (uint16_t row = 0; row < height; row++) {
        graphics_plot(x, (uint16_t) (y + row), color);
        graphics_plot((uint16_t) (x + width - 1), (uint16_t) (y + row), color);
    }
}

static void graphics_draw_codepoint(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color)
{
    font_draw_codepoint(x, y, codepoint, color, graphics_plot);
}

static void graphics_draw_char(uint16_t x, uint16_t y, char ch, uint32_t color)
{
    graphics_draw_codepoint(x, y, (uint8_t) ch, color);
}

static void graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color)
{
    uint16_t origin_x = x;

    while (text != NULL && *text != '\0') {
        uint32_t codepoint = font_utf8_next(&text);

        if (codepoint == '\r') {
            x = origin_x;
            continue;
        }
        if (codepoint == '\n') {
            x = origin_x;
            y = (uint16_t) (y + UI_FONT_HEIGHT);
            continue;
        }
        graphics_draw_codepoint(x, y, codepoint, color);
        x = (uint16_t) (x + font_codepoint_advance(codepoint));
    }
}

#define CUBE3D_TABLE_SIZE 256
static float g_cube3d_sin_table[CUBE3D_TABLE_SIZE];
static bool g_cube3d_table_ready;

static void graphics_cube3d_init_table(void)
{
    if (g_cube3d_table_ready) {
        return;
    }
    for (uint32_t i = 0; i < CUBE3D_TABLE_SIZE; i++) {
        float angle = (float) i * 2.0f * 3.14159265f / (float) CUBE3D_TABLE_SIZE;
        float x = angle;
        if (x > 3.14159265f) {
            x -= 2.0f * 3.14159265f;
        }
        float result = x;
        float term = x;
        for (uint32_t n = 1; n <= 7; n++) {
            term *= -x * x / (float) ((2u * n) * (2u * n + 1u));
            result += term;
        }
        g_cube3d_sin_table[i] = result;
    }
    g_cube3d_table_ready = true;
}

static float graphics_cube3d_sin(float x)
{
    float twopi = 2.0f * 3.14159265f;

    graphics_cube3d_init_table();
    x = x - (int) (x / twopi) * twopi;
    if (x < 0.0f) {
        x += twopi;
    }
    return g_cube3d_sin_table[(uint32_t) ((x / twopi) * (float) CUBE3D_TABLE_SIZE) & (CUBE3D_TABLE_SIZE - 1u)];
}

static float graphics_cube3d_cos(float x)
{
    return graphics_cube3d_sin(x + 3.14159265f / 2.0f);
}

static void graphics_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if (x0 >= 0 && x0 < FB_WIDTH && y0 >= 0 && y0 < FB_HEIGHT) {
            graphics_plot((uint16_t) x0, (uint16_t) y0, color);
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        {
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x0 = (int16_t) (x0 + sx);
            }
            if (e2 <= dx) {
                err += dx;
                y0 = (int16_t) (y0 + sy);
            }
        }
    }
}

static bool graphics_path_is_executable(const char *path)
{
    return graphics_path_has_suffix(path, ".exe") || graphics_path_has_suffix(path, ".EXE");
}

static bool graphics_path_is_package(const char *path)
{
    return graphics_path_has_suffix(path, ".rzs") || graphics_path_has_suffix(path, ".RZS");
}

static bool graphics_path_is_pe_image(const char *path)
{
    return graphics_path_is_executable(path) ||
           graphics_path_has_suffix(path, ".sys") ||
           graphics_path_has_suffix(path, ".SYS") ||
           graphics_path_has_suffix(path, ".dll") ||
           graphics_path_has_suffix(path, ".DLL");
}

static uint32_t graphics_cached_image_flags_for_path(const char *path)
{
    uint32_t slot = GRAPHICS_ICON_CACHE_MAX;
    uint32_t flags;

    if (path == NULL || path[0] == '\0' || !graphics_path_is_pe_image(path)) {
        return 0;
    }
    if (!file_exists(path) || file_is_dir(path)) {
        return 0;
    }
    for (uint32_t i = 0; i < GRAPHICS_ICON_CACHE_MAX; i++) {
        if (g_icon_cache[i].valid && strcasecmp(g_icon_cache[i].path, path) == 0) {
            return g_icon_cache[i].image_flags;
        }
        if (!g_icon_cache[i].valid && slot == GRAPHICS_ICON_CACHE_MAX) {
            slot = i;
        }
    }
    flags = exec_image_flags_for_path(path);
    if (slot == GRAPHICS_ICON_CACHE_MAX) {
        slot = g_icon_cache_next++ % GRAPHICS_ICON_CACHE_MAX;
    }
    g_icon_cache[slot].valid = true;
    strlcpy(g_icon_cache[slot].path, path, sizeof(g_icon_cache[slot].path));
    g_icon_cache[slot].image_flags = flags;
    return flags;
}

static graphics_icon_kind_t graphics_icon_kind_for_path(const char *path, bool is_dir)
{
    uint32_t image_flags;

    if (is_dir) {
        return GRAPHICS_ICON_FOLDER;
    }
    if (path == NULL) {
        return GRAPHICS_ICON_FILE;
    }
    if (graphics_path_has_suffix(path, ".lnk") || graphics_path_has_suffix(path, ".LNK")) {
        return GRAPHICS_ICON_SHORTCUT;
    }
    if (graphics_path_has_suffix(path, ".sys") || graphics_path_has_suffix(path, ".SYS")) {
        return GRAPHICS_ICON_DRIVER;
    }
    if (graphics_path_has_suffix(path, ".dll") || graphics_path_has_suffix(path, ".DLL")) {
        return GRAPHICS_ICON_DLL;
    }
    if (graphics_path_is_executable(path)) {
        image_flags = graphics_cached_image_flags_for_path(path);
        if ((image_flags & EXEC_IMAGE_FLAG_ICON_RESOURCE) != 0) {
            return GRAPHICS_ICON_APP_RESOURCE;
        }
        return GRAPHICS_ICON_APP;
    }
    if (graphics_path_is_package(path)) {
        return GRAPHICS_ICON_PACKAGE;
    }
    if (graphics_player_path_is_audio(path)) {
        return GRAPHICS_ICON_AUDIO;
    }
    return GRAPHICS_ICON_FILE;
}

static uint32_t graphics_icon_flags_for_path(const char *path, bool is_dir)
{
    uint32_t flags = 0;
    uint32_t image_flags;

    if (is_dir || path == NULL || path[0] == '\0') {
        return 0;
    }
    image_flags = graphics_cached_image_flags_for_path(path);
    if ((image_flags & (EXEC_IMAGE_FLAG_NEEDS_R0 | EXEC_IMAGE_FLAG_NEEDS_R2)) != 0) {
        flags |= GRAPHICS_ICON_FLAG_UAC;
    }
    return flags;
}

static graphics_icon_kind_t graphics_icon_kind_for_window(ui_window_kind_t kind)
{
    switch (kind) {
    case UI_WINDOW_FILES:
        return GRAPHICS_ICON_FOLDER;
    case UI_WINDOW_TERMINAL:
    case UI_WINDOW_PROCESS_CONSOLE:
    case UI_WINDOW_SHELL:
    case UI_WINDOW_RUN:
        return GRAPHICS_ICON_TERMINAL;
    case UI_WINDOW_CONTROL_PANEL:
        return GRAPHICS_ICON_SETTINGS;
    case UI_WINDOW_ABOUT:
        return GRAPHICS_ICON_INFO;
    case UI_WINDOW_PLAYER:
        return GRAPHICS_ICON_AUDIO;
    case UI_WINDOW_UAC:
        return GRAPHICS_ICON_UAC;
    case UI_WINDOW_NOTEPAD:
        return GRAPHICS_ICON_FILE;
    case UI_WINDOW_TASKMGR:
    case UI_WINDOW_CUBE3D:
    default:
        return GRAPHICS_ICON_WINDOW;
    }
}

static void graphics_make_display_label(const char *name, char *out, uint32_t out_size)
{
    uint32_t len;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (name == NULL) {
        return;
    }
    len = (uint32_t) strlen(name);
    if (graphics_path_has_suffix(name, ".lnk") || graphics_path_has_suffix(name, ".LNK")) {
        if (len >= 4) {
            len -= 4;
        }
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, name, len);
    out[len] = '\0';
}

static void graphics_draw_shortcut_badge(uint16_t x, uint16_t y, bool small)
{
    uint16_t size = small ? 8 : 14;

    graphics_fill_soft_rect(x, y, size, size, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(x, y, size, size, 0x003C6FEA);
    graphics_draw_line((int16_t) (x + size - 3), (int16_t) (y + 2), (int16_t) (x + 3), (int16_t) (y + size - 4), 0x003C6FEA);
    graphics_draw_line((int16_t) (x + size - 4), (int16_t) (y + 2), (int16_t) (x + size - 3), (int16_t) (y + 6), 0x003C6FEA);
    graphics_draw_line((int16_t) (x + size - 4), (int16_t) (y + 2), (int16_t) (x + size - 8), (int16_t) (y + 3), 0x003C6FEA);
}

static void graphics_draw_uac_badge(uint16_t x, uint16_t y, bool small)
{
    uint16_t size = small ? 10 : 16;
    uint16_t half = (uint16_t) (size / 2);

    graphics_fill_soft_rect(x, y, size, size, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(x, y, size, size, 0x00708090);
    graphics_fill_rect((uint16_t) (x + 2), (uint16_t) (y + 2), (uint16_t) (half - 2), (uint16_t) (half - 2), 0x002C78D4);
    graphics_fill_rect((uint16_t) (x + half), (uint16_t) (y + 2), (uint16_t) (half - 2), (uint16_t) (half - 2), 0x00F2C542);
    graphics_fill_rect((uint16_t) (x + 2), (uint16_t) (y + half), (uint16_t) (half - 2), (uint16_t) (half - 2), 0x00F2C542);
    graphics_fill_rect((uint16_t) (x + half), (uint16_t) (y + half), (uint16_t) (half - 2), (uint16_t) (half - 2), 0x002C78D4);
}

static void graphics_draw_icon(uint16_t x, uint16_t y, graphics_icon_kind_t kind, bool small)
{
    uint16_t w = small ? 18 : 48;
    uint16_t h = small ? 18 : 48;
    uint16_t ix = x;
    uint16_t iy = y;

    switch (kind) {
    case GRAPHICS_ICON_FOLDER:
        graphics_fill_soft_rect((uint16_t) (ix + 2), (uint16_t) (iy + (small ? 4 : 10)), (uint16_t) (w - 4), (uint16_t) (h - (small ? 6 : 14)), 0x00F4C84E);
        graphics_fill_soft_rect((uint16_t) (ix + 4), (uint16_t) (iy + (small ? 2 : 6)), (uint16_t) (small ? 8 : 18), (uint16_t) (small ? 5 : 10), 0x00FFD96A);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 2), (uint16_t) (iy + (small ? 4 : 10)), (uint16_t) (w - 4), (uint16_t) (h - (small ? 6 : 14)), 0x00B68018);
        break;
    case GRAPHICS_ICON_APP:
        graphics_fill_soft_rect((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x002C78D4);
        graphics_fill_rect_gradient((uint16_t) (ix + 7), (uint16_t) (iy + 7), (uint16_t) (w - 14), (uint16_t) (h - 14), 0x005FC9FF, 0x003C6FEA);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x00B7D8FF);
        if (!small) {
            graphics_fill_rect((uint16_t) (ix + 18), (uint16_t) (iy + 17), 6, 6, 0x00FFFFFF);
            graphics_fill_rect((uint16_t) (ix + 26), (uint16_t) (iy + 17), 6, 6, 0x00FFFFFF);
            graphics_fill_rect((uint16_t) (ix + 18), (uint16_t) (iy + 25), 6, 6, 0x00FFFFFF);
            graphics_fill_rect((uint16_t) (ix + 26), (uint16_t) (iy + 25), 6, 6, 0x00FFFFFF);
        }
        break;
    case GRAPHICS_ICON_APP_RESOURCE:
        graphics_fill_soft_rect((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x00FFFFFF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x006FA8DC);
        graphics_fill_rect_gradient((uint16_t) (ix + 8), (uint16_t) (iy + 7), (uint16_t) (w - 16), (uint16_t) (h - 16), 0x0038BDF8, 0x002C78D4);
        graphics_fill_soft_rect((uint16_t) (ix + 10), (uint16_t) (iy + 9), (uint16_t) (small ? 8 : 18), (uint16_t) (small ? 7 : 15), 0x00FFFFFF);
        graphics_fill_soft_rect((uint16_t) (ix + (small ? 8 : 22)), (uint16_t) (iy + (small ? 10 : 25)), (uint16_t) (small ? 8 : 16), (uint16_t) (small ? 6 : 12), 0x00FFE08A);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 8), (uint16_t) (iy + 7), (uint16_t) (w - 16), (uint16_t) (h - 16), 0x00CDE4FF);
        break;
    case GRAPHICS_ICON_AUDIO:
        graphics_fill_soft_rect((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x00FFF8F2);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 3), (uint16_t) (iy + 2), (uint16_t) (w - 6), (uint16_t) (h - 4), 0x00D6A76B);
        graphics_draw_line((int16_t) (ix + w / 2), (int16_t) (iy + 7), (int16_t) (ix + w / 2), (int16_t) (iy + h - 8), 0x00D86D31);
        graphics_draw_line((int16_t) (ix + w / 2), (int16_t) (iy + 7), (int16_t) (ix + w - 7), (int16_t) (iy + 10), 0x00D86D31);
        graphics_fill_soft_rect((uint16_t) (ix + 7), (uint16_t) (iy + h - 12), (uint16_t) (small ? 7 : 14), (uint16_t) (small ? 6 : 10), 0x00D86D31);
        break;
    case GRAPHICS_ICON_PACKAGE:
        graphics_fill_soft_rect((uint16_t) (ix + 4), (uint16_t) (iy + 5), (uint16_t) (w - 8), (uint16_t) (h - 8), 0x00FCE7EE);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 4), (uint16_t) (iy + 5), (uint16_t) (w - 8), (uint16_t) (h - 8), 0x00D94C5A);
        graphics_fill_rect_gradient((uint16_t) (ix + 9), (uint16_t) (iy + 10), (uint16_t) (w - 18), (uint16_t) (h - 18), 0x00FF8BA0, 0x00D94C5A);
        break;
    case GRAPHICS_ICON_DLL:
        graphics_fill_soft_rect((uint16_t) (ix + 5), (uint16_t) (iy + 4), (uint16_t) (w - 10), (uint16_t) (h - 8), 0x00E7FBF1);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 5), (uint16_t) (iy + 4), (uint16_t) (w - 10), (uint16_t) (h - 8), 0x0030A46C);
        for (uint16_t pin = 0; pin < 4; pin++) {
            uint16_t py = (uint16_t) (iy + 8 + pin * (small ? 3 : 8));
            graphics_fill_rect((uint16_t) (ix + 2), py, 4, 2, 0x0030A46C);
            graphics_fill_rect((uint16_t) (ix + w - 6), py, 4, 2, 0x0030A46C);
        }
        if (!small) {
            graphics_draw_text((uint16_t) (ix + 17), (uint16_t) (iy + 20), "DLL", 0x0030A46C);
        }
        break;
    case GRAPHICS_ICON_DRIVER:
        graphics_fill_soft_rect((uint16_t) (ix + 4), (uint16_t) (iy + 5), (uint16_t) (w - 8), (uint16_t) (h - 10), 0x00EEF7FF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 4), (uint16_t) (iy + 5), (uint16_t) (w - 8), (uint16_t) (h - 10), 0x004C82B8);
        for (uint16_t pin = 0; pin < 4; pin++) {
            uint16_t px = (uint16_t) (ix + 8 + pin * (small ? 3 : 8));
            graphics_fill_rect(px, (uint16_t) (iy + 2), 2, 4, 0x004C82B8);
            graphics_fill_rect(px, (uint16_t) (iy + h - 7), 2, 4, 0x004C82B8);
        }
        graphics_fill_rect_gradient((uint16_t) (ix + (small ? 6 : 10)),
                                    (uint16_t) (iy + (small ? 7 : 12)),
                                    (uint16_t) (small ? 6 : w - 20),
                                    (uint16_t) (small ? 6 : 18),
                                    0x006BB9FF,
                                    0x002C78D4);
        if (!small) {
            graphics_draw_text((uint16_t) (ix + 14), (uint16_t) (iy + 30), "SYS", 0x002C5F99);
        }
        break;
    case GRAPHICS_ICON_UAC:
        graphics_fill_soft_rect((uint16_t) (ix + 7), (uint16_t) (iy + 4), (uint16_t) (w - 14), (uint16_t) (h - 8), 0x00FFFFFF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 7), (uint16_t) (iy + 4), (uint16_t) (w - 14), (uint16_t) (h - 8), 0x008AA1B8);
        graphics_fill_rect((uint16_t) (ix + w / 2 - 9), (uint16_t) (iy + 11), 9, (uint16_t) (small ? 7 : 15), 0x002C78D4);
        graphics_fill_rect((uint16_t) (ix + w / 2), (uint16_t) (iy + 11), 9, (uint16_t) (small ? 7 : 15), 0x00F4C542);
        graphics_fill_rect((uint16_t) (ix + w / 2 - 9), (uint16_t) (iy + (small ? 18 : 26)), 9, (uint16_t) (small ? 5 : 12), 0x00F4C542);
        graphics_fill_rect((uint16_t) (ix + w / 2), (uint16_t) (iy + (small ? 18 : 26)), 9, (uint16_t) (small ? 5 : 12), 0x002C78D4);
        graphics_draw_soft_rect_outline((uint16_t) (ix + w / 2 - 10), (uint16_t) (iy + 10), 20, (uint16_t) (small ? 14 : 28), 0x00424E5F);
        break;
    case GRAPHICS_ICON_NETWORK_CONNECTED:
    case GRAPHICS_ICON_NETWORK_LIMITED:
    case GRAPHICS_ICON_NETWORK_OFFLINE:
        {
            uint32_t color = kind == GRAPHICS_ICON_NETWORK_CONNECTED ? 0x002E8B57 :
                             (kind == GRAPHICS_ICON_NETWORK_LIMITED ? 0x00C0801F : 0x00B43A3A);
            graphics_fill_soft_rect((uint16_t) (ix + 2), (uint16_t) (iy + 4), (uint16_t) (w - 7), (uint16_t) (h - 8), 0x00F7FBFF);
            graphics_draw_soft_rect_outline((uint16_t) (ix + 2), (uint16_t) (iy + 4), (uint16_t) (w - 7), (uint16_t) (h - 8), 0x008AA1B8);
            graphics_fill_rect((uint16_t) (ix + 6), (uint16_t) (iy + h - 6), (uint16_t) (w - 15), 2, 0x008AA1B8);
            if (kind == GRAPHICS_ICON_NETWORK_CONNECTED) {
                graphics_draw_line((int16_t) (ix + 6), (int16_t) (iy + 12), (int16_t) (ix + 10), (int16_t) (iy + 16), color);
                graphics_draw_line((int16_t) (ix + 10), (int16_t) (iy + 16), (int16_t) (ix + 17), (int16_t) (iy + 8), color);
            } else if (kind == GRAPHICS_ICON_NETWORK_LIMITED) {
                graphics_fill_rect((uint16_t) (ix + w - 6), (uint16_t) (iy + 6), 2, (uint16_t) (h - 12), color);
                graphics_plot((uint16_t) (ix + w - 6), (uint16_t) (iy + h - 4), color);
            } else {
                graphics_draw_line((int16_t) (ix + 5), (int16_t) (iy + 7), (int16_t) (ix + w - 5), (int16_t) (iy + h - 7), color);
                graphics_draw_line((int16_t) (ix + w - 5), (int16_t) (iy + 7), (int16_t) (ix + 5), (int16_t) (iy + h - 7), color);
            }
        }
        break;
    case GRAPHICS_ICON_SHORTCUT:
        graphics_draw_icon(ix, iy, GRAPHICS_ICON_APP, small);
        graphics_draw_shortcut_badge((uint16_t) (ix + (small ? 0 : 2)), (uint16_t) (iy + h - (small ? 9 : 16)), small);
        break;
    case GRAPHICS_ICON_TERMINAL:
        graphics_fill_soft_rect((uint16_t) (ix + 2), (uint16_t) (iy + 3), (uint16_t) (w - 4), (uint16_t) (h - 6), 0x0017202C);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 2), (uint16_t) (iy + 3), (uint16_t) (w - 4), (uint16_t) (h - 6), 0x004A6278);
        graphics_draw_line((int16_t) (ix + 7), (int16_t) (iy + 9), (int16_t) (ix + 12), (int16_t) (iy + 12), 0x00B8F7C8);
        graphics_draw_line((int16_t) (ix + 7), (int16_t) (iy + 15), (int16_t) (ix + 12), (int16_t) (iy + 12), 0x00B8F7C8);
        graphics_fill_rect((uint16_t) (ix + 14), (uint16_t) (iy + 15), (uint16_t) (small ? 5 : 14), 2, 0x00B8F7C8);
        break;
    case GRAPHICS_ICON_SETTINGS:
        graphics_fill_soft_rect((uint16_t) (ix + 3), (uint16_t) (iy + 3), (uint16_t) (w - 6), (uint16_t) (h - 6), 0x00EEF2FF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 3), (uint16_t) (iy + 3), (uint16_t) (w - 6), (uint16_t) (h - 6), 0x006A67CE);
        graphics_draw_line((int16_t) (ix + w / 2), (int16_t) (iy + 7), (int16_t) (ix + w / 2), (int16_t) (iy + h - 7), 0x006A67CE);
        graphics_draw_line((int16_t) (ix + 7), (int16_t) (iy + h / 2), (int16_t) (ix + w - 7), (int16_t) (iy + h / 2), 0x006A67CE);
        graphics_fill_soft_rect((uint16_t) (ix + w / 2 - 5), (uint16_t) (iy + h / 2 - 5), 10, 10, 0x006A67CE);
        break;
    case GRAPHICS_ICON_INFO:
        graphics_fill_soft_rect((uint16_t) (ix + 4), (uint16_t) (iy + 3), (uint16_t) (w - 8), (uint16_t) (h - 6), 0x00FFF7EB);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 4), (uint16_t) (iy + 3), (uint16_t) (w - 8), (uint16_t) (h - 6), 0x00C06C2B);
        graphics_fill_soft_rect((uint16_t) (ix + w / 2 - 2), (uint16_t) (iy + 8), 4, 4, 0x00C06C2B);
        graphics_fill_rect((uint16_t) (ix + w / 2 - 1), (uint16_t) (iy + 15), 2,
                           small ? 1 : (uint16_t) (h - 24), 0x00C06C2B);
        break;
    case GRAPHICS_ICON_WINDOW:
        graphics_fill_soft_rect((uint16_t) (ix + 3), (uint16_t) (iy + 4), (uint16_t) (w - 6), (uint16_t) (h - 8), 0x00F7FBFF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 3), (uint16_t) (iy + 4), (uint16_t) (w - 6), (uint16_t) (h - 8), 0x008AA1B8);
        graphics_fill_rect((uint16_t) (ix + 5), (uint16_t) (iy + 6), (uint16_t) (w - 10), 4, 0x003C6FEA);
        break;
    case GRAPHICS_ICON_FILE:
    default:
        graphics_fill_soft_rect((uint16_t) (ix + 8), (uint16_t) (iy + 3), (uint16_t) (w - 14), (uint16_t) (h - 6), 0x00FFFFFF);
        graphics_draw_soft_rect_outline((uint16_t) (ix + 8), (uint16_t) (iy + 3), (uint16_t) (w - 14), (uint16_t) (h - 6), 0x00A7BBD1);
        graphics_draw_line((int16_t) (ix + w - 12), (int16_t) (iy + 3), (int16_t) (ix + w - 5), (int16_t) (iy + 10), 0x00A7BBD1);
        if (!small) {
            graphics_fill_rect((uint16_t) (ix + 16), (uint16_t) (iy + 21), 20, 2, 0x00CAD8E8);
            graphics_fill_rect((uint16_t) (ix + 16), (uint16_t) (iy + 27), 16, 2, 0x00CAD8E8);
        }
        break;
    }
}

static void graphics_draw_file_icon(uint16_t x, uint16_t y, const char *path, bool is_dir, bool small)
{
    char shortcut_target[GRAPHICS_CLIPBOARD_PATH_MAX];
    const char *icon_path = path;
    bool icon_is_dir = is_dir;
    bool shortcut = false;
    graphics_icon_kind_t kind;
    uint32_t flags;
    uint16_t box = small ? 18 : 48;

    if (!is_dir && path != NULL &&
        (graphics_path_has_suffix(path, ".lnk") || graphics_path_has_suffix(path, ".LNK"))) {
        shortcut = true;
        if (graphics_resolve_shortcut(path, shortcut_target, sizeof(shortcut_target))) {
            icon_path = shortcut_target;
            icon_is_dir = file_is_dir(icon_path);
        }
    }
    kind = shortcut && icon_path == path ? GRAPHICS_ICON_APP : graphics_icon_kind_for_path(icon_path, icon_is_dir);
    flags = graphics_icon_flags_for_path(icon_path, icon_is_dir);
    graphics_draw_icon(x, y, kind, small);
    if ((flags & GRAPHICS_ICON_FLAG_UAC) != 0) {
        graphics_draw_uac_badge((uint16_t) (x + box - (small ? 9 : 15)),
                                (uint16_t) (y + box - (small ? 9 : 15)),
                                small);
    }
    if (shortcut) {
        graphics_draw_shortcut_badge((uint16_t) (x + (small ? 0 : 2)),
                                     (uint16_t) (y + box - (small ? 9 : 16)),
                                     small);
    }
}

typedef struct {
    float x;
    float y;
    float z;
} cube3d_vec3_t;

static cube3d_vec3_t graphics_cube3d_rotate(float vx, float vy, float vz, float ax, float ay, float az)
{
    float cx = graphics_cube3d_cos(ax);
    float sx = graphics_cube3d_sin(ax);
    float cy = graphics_cube3d_cos(ay);
    float sy = graphics_cube3d_sin(ay);
    float cz = graphics_cube3d_cos(az);
    float sz = graphics_cube3d_sin(az);
    float x;
    float y;
    float z;

    x = vx * cy + vz * sy;
    z = -vx * sy + vz * cy;
    vx = x;
    vz = z;

    y = vy * cx - vz * sx;
    z = vy * sx + vz * cx;
    vy = y;
    vz = z;

    x = vx * cz - vy * sz;
    y = vx * sz + vy * cz;

    return (cube3d_vec3_t) { x, y, vz };
}

static void graphics_draw_cube3d_window(const ui_window_t *window)
{
    static const float verts[8][3] = {
        { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f },
        { 1.0f, 1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f },
        { -1.0f, -1.0f, 1.0f }, { 1.0f, -1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f }, { -1.0f, 1.0f, 1.0f }
    };
    static const uint8_t edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
    };
    cube3d_vec3_t rotated[8];
    int16_t proj_x[8];
    int16_t proj_y[8];
    uint16_t inner_x;
    uint16_t inner_y;
    uint16_t inner_w;
    uint16_t inner_h;
    float scale;
    float center_x;
    float center_y;
    uint32_t i;

    if (window == NULL) {
        return;
    }

    inner_x = (uint16_t) (window->x + 8);
    inner_y = (uint16_t) (window->y + 28);
    inner_w = window->width > 16 ? (uint16_t) (window->width - 16) : window->width;
    inner_h = window->height > 36 ? (uint16_t) (window->height - 36) : window->height;
    graphics_fill_rect(inner_x, inner_y, inner_w, inner_h, 0x000E1728);
    graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "Cube3D", 0x00DCEAFF);
    graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + window->height - 18), "drag to move", 0x0089A7C7);

    if (inner_w < 80 || inner_h < 80) {
        return;
    }

    scale = (float) ((inner_w < inner_h ? inner_w : inner_h) * 0.28f);
    center_x = (float) inner_x + (float) inner_w * 0.5f;
    center_y = (float) inner_y + (float) inner_h * 0.5f;

    for (i = 0; i < 8; i++) {
        rotated[i] = graphics_cube3d_rotate(verts[i][0], verts[i][1], verts[i][2], g_cube3d_angle_x, g_cube3d_angle_y, g_cube3d_angle_z);
        {
            float perspective = 4.0f / (4.0f + rotated[i].z);
            proj_x[i] = (int16_t) (center_x + rotated[i].x * scale * perspective);
            proj_y[i] = (int16_t) (center_y + rotated[i].y * scale * perspective);
        }
    }

    for (i = 0; i < 12; i++) {
        uint32_t color = (i < 4) ? 0x00DCEAFF : (i < 8 ? 0x005CC8FF : 0x00FFFFFF);
        graphics_draw_line(proj_x[edges[i][0]], proj_y[edges[i][0]],
                           proj_x[edges[i][1]], proj_y[edges[i][1]], color);
    }
}

static bool graphics_point_in_rect(uint16_t px, uint16_t py, uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    return px >= x && px < (uint16_t) (x + width) && py >= y && py < (uint16_t) (y + height);
}

static void graphics_window_keep_on_screen(ui_window_t *window)
{
    int32_t max_x;
    int32_t max_y;

    if (window == NULL || !window->visible) {
        return;
    }
    if (window->width > FB_WIDTH - 16) {
        window->width = (uint16_t) (FB_WIDTH > 32 ? FB_WIDTH - 16 : FB_WIDTH);
    }
    if (window->height > FB_HEIGHT - TASKBAR_HEIGHT - 16) {
        window->height = (uint16_t) (FB_HEIGHT > TASKBAR_HEIGHT + 32 ? FB_HEIGHT - TASKBAR_HEIGHT - 16 : FB_HEIGHT);
    }
    max_x = (int32_t) FB_WIDTH - (int32_t) window->width - 8;
    max_y = (int32_t) FB_HEIGHT - TASKBAR_HEIGHT - (int32_t) window->height - 8;
    if (max_x < 8) {
        max_x = 8;
    }
    if (max_y < 8) {
        max_y = 8;
    }
    if (window->x < 8) {
        window->x = 8;
    }
    if (window->y < 8) {
        window->y = 8;
    }
    if ((int32_t) window->x > max_x) {
        window->x = (uint16_t) max_x;
    }
    if ((int32_t) window->y > max_y) {
        window->y = (uint16_t) max_y;
    }
}

static void graphics_window_set(ui_window_t *window, ui_window_kind_t kind, uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *title)
{
    window->visible = true;
    window->minimized = false;
    window->maximized = false;
    window->kind = kind;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->restore_x = x;
    window->restore_y = y;
    window->restore_width = width;
    window->restore_height = height;
    window->owner_pid = -1;
    window->console_scroll_offset = 0;
    window->console_scroll_manual = false;
    strlcpy(window->title, title != NULL ? title : "", sizeof(window->title));
    graphics_window_keep_on_screen(window);
}

static void graphics_reflow_windows(void)
{
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        graphics_window_keep_on_screen(&g_windows[i]);
    }
    if (g_cursor_x > FB_WIDTH - 16) {
        g_cursor_x = (uint16_t) (FB_WIDTH - 16);
    }
    if (g_cursor_y > FB_HEIGHT - 16) {
        g_cursor_y = (uint16_t) (FB_HEIGHT - 16);
    }
}

static void graphics_bring_window_to_front(uint32_t index)
{
    ui_window_t saved;

    if (index >= UI_WINDOW_MAX || index == UI_WINDOW_MAX - 1) {
        return;
    }

    saved = g_windows[index];
    for (uint32_t i = index; i + 1 < UI_WINDOW_MAX; i++) {
        g_windows[i] = g_windows[i + 1];
    }
    g_windows[UI_WINDOW_MAX - 1] = saved;
    g_windows[UI_WINDOW_MAX - 1].minimized = false;
}

static void graphics_minimize_window(uint32_t index)
{
    if (index >= UI_WINDOW_MAX || !g_windows[index].visible) {
        return;
    }
    g_windows[index].minimized = true;
    g_windows[index].maximized = false;
}

static void graphics_toggle_maximize_window(uint32_t index)
{
    ui_window_t *window;

    if (index >= UI_WINDOW_MAX || !g_windows[index].visible) {
        return;
    }
    window = &g_windows[index];
    if (!window->maximized) {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
        window->x = 8;
        window->y = 8;
        window->width = FB_WIDTH > 16 ? (uint16_t) (FB_WIDTH - 16) : FB_WIDTH;
        window->height = FB_HEIGHT > TASKBAR_HEIGHT + 16 ? (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 16) : FB_HEIGHT;
        window->maximized = true;
        window->minimized = false;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->maximized = false;
        window->minimized = false;
    }
    graphics_window_keep_on_screen(window);
}

static int32_t graphics_find_window(ui_window_kind_t kind)
{
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible && g_windows[i].kind == kind) {
            return (int32_t) i;
        }
    }
    return -1;
}

static int32_t graphics_find_process_console_window(int32_t pid)
{
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible &&
            g_windows[i].kind == UI_WINDOW_PROCESS_CONSOLE &&
            g_windows[i].owner_pid == pid) {
            return (int32_t) i;
        }
    }
    return -1;
}

static bool graphics_window_is_terminal(const ui_window_t *window)
{
    return window != NULL &&
           (window->kind == UI_WINDOW_TERMINAL ||
            window->kind == UI_WINDOW_PROCESS_CONSOLE);
}

static void graphics_open_window(ui_window_kind_t kind)
{
    ui_window_t *window = NULL;
    uint32_t window_index = 0;
    bool allow_multiple = kind == UI_WINDOW_TERMINAL ||
                          kind == UI_WINDOW_NOTEPAD ||
                          kind == UI_WINDOW_PLAYER ||
                          kind == UI_WINDOW_CUBE3D;

    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!allow_multiple && g_windows[i].visible && g_windows[i].kind == kind) {
            window = &g_windows[i];
            window_index = i;
            break;
        }
        if (!g_windows[i].visible && window == NULL) {
            window = &g_windows[i];
            window_index = i;
        }
    }
    if (window == NULL) {
        return;
    }

    if (window->visible) {
        if (window->minimized) {
            window->minimized = false;
            graphics_bring_window_to_front(window_index);
            graphics_draw_shell();
            return;
        }
        graphics_bring_window_to_front(window_index);
        graphics_draw_shell();
        return;
    }

    switch (kind) {
    case UI_WINDOW_LOGON:
        graphics_window_set(window, kind, graphics_clamp_window_x(380), graphics_clamp_window_y(300), 380, 300, "MONIOS");
        graphics_reset_login();
        break;
    case UI_WINDOW_FILES:
        graphics_window_set(window, kind, graphics_clamp_window_x(390), 82, 390, 270, "\u6587\u4ef6");
        graphics_fill_file_browser();
        break;
    case UI_WINDOW_TERMINAL:
        graphics_window_set(window, kind, graphics_clamp_window_x((uint16_t) (500 + window_index * 20)), (uint16_t) (128 + window_index * 18), 500, 286, "\u63a7\u5236\u53f0");
        window->console_scroll_manual = false;
        window->console_scroll_offset = GRAPHICS_CONSOLE_ROWS;
        graphics_set_terminal_focus(true);
        break;
    case UI_WINDOW_RUN:
        graphics_window_set(window, kind, graphics_clamp_window_x(320), graphics_clamp_window_y(120), 320, 120, "\u8fd0\u884c");
        g_run_input_len = 0;
        g_run_input[0] = '\0';
        g_run_input_focus = true;
        break;
    case UI_WINDOW_SHELL:
        graphics_window_set(window, kind, graphics_clamp_window_x(460), 120, 460, 280, "\u547d\u4ee4");
        break;
    case UI_WINDOW_ABOUT:
        graphics_window_set(window, kind, graphics_clamp_window_x(320), graphics_clamp_window_y(190), 320, 190, "\u5173\u4e8e");
        break;
    case UI_WINDOW_PLAYER:
        graphics_window_set(window, kind, graphics_clamp_window_x(370), 118, 370, 320, "\u64ad\u653e\u5668");
        if (g_player_current_path[0] == '\0') {
            strcpy(g_player_current_path, UI_ROOT_DESKTOP);
        }
        if (g_player_status[0] == '\0') {
            strcpy(g_player_status, "ready");
        }
        break;
    case UI_WINDOW_NOTEPAD:
        graphics_window_set(window, kind, graphics_clamp_window_x(540), 78, 540, 360, "\u8bb0\u4e8b\u672c");
        g_notepad_focus = true;
        if (g_notepad_path[0] == '\0') {
            strcpy(g_notepad_path, UI_ROOT_DESKTOP PATH_SEPARATOR_STR "note.txt");
            g_notepad_text[0] = '\0';
            g_notepad_len = 0;
        }
        break;
    case UI_WINDOW_TASKMGR:
        graphics_window_set(window, kind, graphics_clamp_window_x(390), 120, 390, 272, "\u4efb\u52a1\u7ba1\u7406\u5668");
        break;
    case UI_WINDOW_CUBE3D:
        graphics_window_set(window, kind, graphics_clamp_window_x(500), 96, 500, 370, "Cube3D");
        g_cube3d_open = true;
        if (g_cube3d_last_tick == 0) {
            g_cube3d_angle_x = 0.45f;
            g_cube3d_angle_y = 0.15f;
            g_cube3d_angle_z = 0.0f;
        }
        break;
    case UI_WINDOW_POWER:
        graphics_window_set(window, kind, graphics_clamp_window_x(220), graphics_clamp_window_y(140), 220, 140, "\u7535\u6e90");
        break;
    case UI_WINDOW_UAC:
        graphics_window_set(window, kind, graphics_clamp_window_x(400), graphics_clamp_window_y(210), 400, 210, "UAC");
        g_uac_input_focus = true;
        break;
    case UI_WINDOW_CONTROL_PANEL:
        graphics_window_set(window, kind, graphics_clamp_window_x(430), 88, 430, 354, "\u63a7\u5236\u9762\u677f");
        break;
    default:
        break;
    }
    graphics_bring_window_to_front(window_index);
}

bool graphics_open_process_console_window(int32_t pid, const char *title)
{
    ui_window_t *window = NULL;
    uint32_t window_index = 0;
    int32_t existing_index;

    if (!g_graphics_active || g_installer_mode || pid < 0) {
        return false;
    }

    existing_index = graphics_find_process_console_window(pid);
    if (existing_index >= 0) {
        window = &g_windows[existing_index];
        if (title != NULL && title[0] != '\0') {
            strlcpy(window->title, title, sizeof(window->title));
        }
        window->minimized = false;
        graphics_set_terminal_focus(true);
        graphics_bring_window_to_front((uint32_t) existing_index);
        graphics_draw_shell();
        graphics_flush_gpu();
        return true;
    }

    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible) {
            window = &g_windows[i];
            window_index = i;
            break;
        }
    }
    if (window == NULL) {
        return false;
    }

    graphics_window_set(window,
                        UI_WINDOW_PROCESS_CONSOLE,
                        graphics_clamp_window_x((uint16_t) (560 + window_index * 20)),
                        (uint16_t) (128 + window_index * 18),
                        560,
                        286,
                        title != NULL && title[0] != '\0' ? title : "console");
    window->owner_pid = pid;
    window->console_scroll_manual = false;
    window->console_scroll_offset = GRAPHICS_CONSOLE_ROWS;
    graphics_set_terminal_focus(true);
    graphics_bring_window_to_front(window_index);
    graphics_draw_shell();
    graphics_flush_gpu();
    return true;
}

void graphics_close_process_console_window(int32_t pid)
{
    int32_t index = graphics_find_process_console_window(pid);

    if (index < 0) {
        return;
    }
    graphics_close_window((uint32_t) index);
    graphics_draw_shell();
    graphics_flush_gpu();
}

void graphics_mark_process_console_finished(int32_t pid, int32_t exit_code)
{
    int32_t index;
    char suffix[24];
    char code[12];

    if (pid < 0) {
        return;
    }
    index = graphics_find_process_console_window(pid);
    if (index < 0) {
        return;
    }
    if (exit_code < 0) {
        graphics_u32_to_dec(code, (uint32_t) (-(exit_code + 1)) + 1U);
    } else {
        graphics_u32_to_dec(code, (uint32_t) exit_code);
    }
    strcpy(suffix, exit_code < 0 ? " [exit -" : " [exit ");
    strcpy(suffix + strlen(suffix), code);
    strcpy(suffix + strlen(suffix), "]");
    if (strlen(g_windows[index].title) + strlen(suffix) + 1 < sizeof(g_windows[index].title)) {
        strcpy(g_windows[index].title + strlen(g_windows[index].title), suffix);
    }
    graphics_set_terminal_focus(false);
    graphics_draw_shell();
    graphics_flush_gpu();
}

bool graphics_set_process_console_window_title(int32_t pid, const char *title)
{
    int32_t index;

    if (pid < 0 || title == NULL || title[0] == '\0') {
        return false;
    }
    index = graphics_find_process_console_window(pid);
    if (index < 0) {
        return false;
    }
    strlcpy(g_windows[index].title, title, sizeof(g_windows[index].title));
    graphics_draw_shell();
    graphics_flush_gpu();
    return true;
}

static void graphics_close_window(uint32_t index)
{
    ui_window_kind_t kind;
    int32_t owner_pid;

    if (index >= UI_WINDOW_MAX) {
        return;
    }
    if (!g_windows[index].visible) {
        return;
    }
    kind = g_windows[index].kind;
    owner_pid = g_windows[index].owner_pid;
    g_windows[index].visible = false;
    g_windows[index].minimized = false;
    g_windows[index].maximized = false;
    g_windows[index].owner_pid = -1;
    g_windows[index].title[0] = '\0';
    if (g_dragging_window && g_drag_window_index == index) {
        g_dragging_window = false;
    }
    if (kind == UI_WINDOW_RUN) {
        g_run_input_focus = false;
        g_run_input_len = 0;
        g_run_input[0] = '\0';
    }
    if (kind == UI_WINDOW_TERMINAL || kind == UI_WINDOW_PROCESS_CONSOLE) {
        bool terminal_visible = false;

        for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
            if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_TERMINAL) {
                terminal_visible = true;
                break;
            }
        }
        graphics_set_terminal_focus(terminal_visible);
    }
    if (kind == UI_WINDOW_PROCESS_CONSOLE && owner_pid >= 0) {
        console_destroy_for_pid(owner_pid);
    }
    if (kind == UI_WINDOW_NOTEPAD) {
        g_notepad_focus = false;
    }
    if (kind == UI_WINDOW_PLAYER) {
        g_player_button_pressed = false;
        g_player_browser_open = false;
    }
    if (kind == UI_WINDOW_CUBE3D) {
        g_cube3d_open = false;
        g_cube3d_last_tick = 0;
    }
    if (kind == UI_WINDOW_UAC) {
        g_uac_input_focus = false;
        g_uac_pending = false;
        if (!g_uac_result_ready) {
            g_uac_result = false;
            g_uac_result_ready = true;
        }
    }
}

static void graphics_fill(uint32_t color)
{
    uint32_t pixels = (uint32_t) FB_WIDTH * (uint32_t) FB_HEIGHT;

    for (uint32_t i = 0; i < pixels; i++) {
        g_backbuffer[i] = color;
    }
}

void graphics_clear(uint8_t color)
{
    if (!g_graphics_active) {
        return;
    }
    graphics_fill((uint32_t) color);
    g_cursor_drawn = false;
}

static void graphics_dec_percent(char *out, uint32_t value)
{
    uint32_t index = 0;

    if (value >= 100) {
        out[index++] = '1';
        out[index++] = '0';
        out[index++] = '0';
    } else {
        if (value >= 10) {
            out[index++] = (char) ('0' + (value / 10));
        }
        out[index++] = (char) ('0' + (value % 10));
    }
    out[index++] = '%';
    out[index] = '\0';
}

static uint8_t graphics_lerp_channel(uint8_t from, uint8_t to, uint32_t num, uint32_t den)
{
    int32_t delta;

    if (den == 0) {
        return to;
    }
    if (num > den) {
        num = den;
    }
    delta = (int32_t) to - (int32_t) from;
    return (uint8_t) ((int32_t) from + (delta * (int32_t) num) / (int32_t) den);
}

static uint32_t graphics_lerp_color(uint32_t from, uint32_t to, uint32_t num, uint32_t den)
{
    uint8_t from_r = (uint8_t) ((from >> 16) & 0xFF);
    uint8_t from_g = (uint8_t) ((from >> 8) & 0xFF);
    uint8_t from_b = (uint8_t) (from & 0xFF);
    uint8_t to_r = (uint8_t) ((to >> 16) & 0xFF);
    uint8_t to_g = (uint8_t) ((to >> 8) & 0xFF);
    uint8_t to_b = (uint8_t) (to & 0xFF);
    uint8_t r = graphics_lerp_channel(from_r, to_r, num, den);
    uint8_t g = graphics_lerp_channel(from_g, to_g, num, den);
    uint8_t b = graphics_lerp_channel(from_b, to_b, num, den);

    return ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
}

static void graphics_fill_vertical_gradient(uint32_t top_color, uint32_t bottom_color)
{
    for (uint16_t y = 0; y < FB_HEIGHT; y++) {
        uint32_t color = graphics_lerp_color(top_color, bottom_color, y, FB_HEIGHT > 1 ? FB_HEIGHT - 1 : 1);
        uint64_t row_index = (uint64_t) y * FB_WIDTH;
        for (uint16_t x = 0; x < FB_WIDTH; x++) {
            g_backbuffer[row_index + x] = color;
        }
    }
}

static void graphics_wait_ticks(uint64_t wait_ticks)
{
    uint64_t start = timer_ticks();

    while (timer_ticks() - start < wait_ticks) {
    }
}

static uint16_t graphics_read_le16(const uint8_t *data);
static uint32_t graphics_read_le32(const uint8_t *data);

static bool graphics_load_boot_image_asset(void)
{
    uint8_t *data;
    int32_t size;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t raw_height;
    uint32_t height;
    uint32_t stride;
    bool top_down;

    if (g_boot_image_attempted) {
        return g_boot_image_loaded;
    }
    g_boot_image_attempted = true;
    size = file_size(UI_BOOT_IMAGE_PATH);
    if (size <= 54 || (uint32_t) size > GRAPHICS_BOOT_IMAGE_MAX_BYTES) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) size);
    if (data == NULL) {
        return false;
    }
    if (file_read(UI_BOOT_IMAGE_PATH, data, (uint32_t) size) != size) {
        kfree(data);
        return false;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        kfree(data);
        return false;
    }

    pixel_offset = graphics_read_le32(data + 10);
    dib_size = graphics_read_le32(data + 14);
    width = (int32_t) graphics_read_le32(data + 18);
    raw_height = (int32_t) graphics_read_le32(data + 22);
    if (dib_size < 40 || width <= 0 || raw_height == 0 ||
        width > GRAPHICS_BOOT_IMAGE_MAX_WIDTH ||
        raw_height > GRAPHICS_BOOT_IMAGE_MAX_HEIGHT ||
        raw_height < -(int32_t) GRAPHICS_BOOT_IMAGE_MAX_HEIGHT ||
        graphics_read_le16(data + 26) != 1 ||
        graphics_read_le16(data + 28) != 24 ||
        graphics_read_le32(data + 30) != 0) {
        kfree(data);
        return false;
    }

    top_down = raw_height < 0;
    height = top_down ? (uint32_t) -raw_height : (uint32_t) raw_height;
    stride = (((uint32_t) width * 3U) + 3U) & ~3U;
    if (pixel_offset > (uint32_t) size || stride == 0 || height == 0 ||
        stride > ((uint32_t) size - pixel_offset) / height) {
        kfree(data);
        return false;
    }

    for (uint32_t y = 0; y < height; y++) {
        uint32_t source_y = top_down ? y : height - 1U - y;
        const uint8_t *row = data + pixel_offset + source_y * stride;

        for (uint32_t x = 0; x < (uint32_t) width; x++) {
            uint8_t b = row[x * 3U];
            uint8_t g = row[x * 3U + 1U];
            uint8_t r = row[x * 3U + 2U];
            g_boot_image_pixels[y * GRAPHICS_BOOT_IMAGE_MAX_WIDTH + x] =
                ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
        }
    }
    g_boot_image_width = (uint16_t) width;
    g_boot_image_height = (uint16_t) height;
    g_boot_image_loaded = true;
    kfree(data);
    log_write("graphics: boot image loaded");
    return true;
}

bool graphics_boot_load_image(void)
{
    return graphics_load_boot_image_asset();
}

static void graphics_draw_boot_image(uint32_t brightness)
{
    uint32_t scaled_width;
    uint32_t scaled_height;
    uint32_t crop_x = 0;
    uint32_t crop_y = 0;

    if (!g_boot_image_loaded || g_boot_image_width == 0 || g_boot_image_height == 0) {
        graphics_fill(0x00000000);
        return;
    }
    if (brightness > 100U) {
        brightness = 100U;
    }

    if ((uint64_t) FB_WIDTH * g_boot_image_height >=
        (uint64_t) FB_HEIGHT * g_boot_image_width) {
        scaled_width = FB_WIDTH;
        scaled_height = ((uint32_t) FB_WIDTH * g_boot_image_height) / g_boot_image_width;
        crop_y = scaled_height > FB_HEIGHT ? (scaled_height - FB_HEIGHT) / 2U : 0;
    } else {
        scaled_height = FB_HEIGHT;
        scaled_width = ((uint32_t) FB_HEIGHT * g_boot_image_width) / g_boot_image_height;
        crop_x = scaled_width > FB_WIDTH ? (scaled_width - FB_WIDTH) / 2U : 0;
    }
    if (scaled_width == 0 || scaled_height == 0) {
        graphics_fill(0x00000000);
        return;
    }

    for (uint16_t y = 0; y < FB_HEIGHT; y++) {
        uint32_t source_y = (((uint32_t) y + crop_y) * g_boot_image_height) / scaled_height;
        if (source_y >= g_boot_image_height) {
            source_y = g_boot_image_height - 1U;
        }
        for (uint16_t x = 0; x < FB_WIDTH; x++) {
            uint32_t source_x = (((uint32_t) x + crop_x) * g_boot_image_width) / scaled_width;
            uint32_t color;
            uint32_t r;
            uint32_t g;
            uint32_t b;

            if (source_x >= g_boot_image_width) {
                source_x = g_boot_image_width - 1U;
            }
            color = g_boot_image_pixels[source_y * GRAPHICS_BOOT_IMAGE_MAX_WIDTH + source_x];
            r = (((color >> 16) & 0xFFU) * brightness) / 100U;
            g = (((color >> 8) & 0xFFU) * brightness) / 100U;
            b = ((color & 0xFFU) * brightness) / 100U;
            graphics_plot(x, y, (r << 16) | (g << 8) | b);
        }
    }
}

static void graphics_boot_show_brightness(uint32_t brightness)
{
    graphics_draw_boot_image(brightness);
    graphics_present();
}

static void graphics_boot_delay(void)
{
    for (volatile uint32_t delay = 0; delay < GRAPHICS_BOOT_FADE_DELAY; delay++) {
        asm volatile ("pause");
    }
}

static void graphics_show_boot_loading_screen(void)
{
    graphics_boot_show_brightness(g_boot_image_loaded ? 100U : 0U);
}

void graphics_boot_animation(void)
{
    if (!g_graphics_active) {
        return;
    }
    graphics_show_boot_loading_screen();
}

void graphics_boot_update(uint32_t progress, const char *status)
{
    (void) progress;
    (void) status;
    if (!g_graphics_active) {
        return;
    }
    graphics_show_boot_loading_screen();
}

void graphics_boot_finish(void)
{
    if (!g_graphics_active) {
        return;
    }
    if (!g_boot_image_loaded && !graphics_boot_load_image()) {
        graphics_boot_show_brightness(0);
        return;
    }

    for (uint32_t frame = 0; frame <= GRAPHICS_BOOT_FADE_STEPS; frame++) {
        graphics_boot_show_brightness(100U - (frame * 100U) / GRAPHICS_BOOT_FADE_STEPS);
        graphics_boot_delay();
    }
    for (uint32_t frame = 0; frame <= GRAPHICS_BOOT_FADE_STEPS; frame++) {
        graphics_boot_show_brightness((frame * 100U) / GRAPHICS_BOOT_FADE_STEPS);
        graphics_boot_delay();
    }
    for (uint32_t frame = 0; frame <= GRAPHICS_BOOT_FADE_STEPS; frame++) {
        graphics_boot_show_brightness(100U - (frame * 100U) / GRAPHICS_BOOT_FADE_STEPS);
        graphics_boot_delay();
    }
}

void graphics_draw_bsod(const char *process, const char *code, const char *text, uint32_t progress)
{
    char progress_text[18] = "收集：";
    char percent[5];
    uint16_t bar_width;

    if (!g_graphics_active || g_framebuffer == NULL) {
        return;
    }
    if (progress > 100) {
        progress = 100;
    }

    g_cursor_drawn = false;
    graphics_fill(0x00000000);
    graphics_present();

    graphics_fill(0x000024AA);
    graphics_fill_rect(72, 68, 880, 620, 0x000035C8);
    graphics_draw_rect_outline(72, 68, 880, 620, 0x005A8BFF);

    graphics_draw_text(112, 116, "MONIOS 调试错误", 0x00FFFFFF);
    graphics_draw_text(112, 174, "进程：", 0x00DCEAFF);
    graphics_draw_text(200, 174, process != NULL ? process : "内核", 0x00FFFFFF);
    graphics_draw_text(112, 224, "错误代码：", 0x00DCEAFF);
    graphics_draw_text(232, 224, code != NULL ? code : "0000000000000000", 0x00FFFFFF);
    graphics_draw_text(112, 274, "错误信息：", 0x00DCEAFF);
    graphics_draw_text(232, 274, text != NULL ? text : "未知错误", 0x00FFFFFF);

    graphics_fill_rect(112, 388, 680, 28, 0x00001266);
    graphics_draw_rect_outline(112, 388, 680, 28, 0x0098BAFF);
    bar_width = (uint16_t) ((progress * 676) / 100);
    if (bar_width > 0) {
        graphics_fill_rect(114, 390, bar_width, 24, 0x00B9D8FF);
    }

    graphics_dec_percent(percent, progress);
    strcpy(progress_text + 12, percent);
    graphics_draw_text(112, 436, progress_text, 0x00FFFFFF);
    graphics_draw_text(112, 496, "收集数据达到100%后，系统将写入串口并重启。", 0x00DCEAFF);
    graphics_present();
}

static uint16_t graphics_read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t graphics_read_le32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static bool graphics_cursor_decode_dib(const uint8_t *data, uint32_t size,
                                       uint16_t entry_width, uint16_t entry_height,
                                       uint16_t hotspot_x, uint16_t hotspot_y)
{
    uint32_t dib_size;
    int32_t dib_width;
    int32_t dib_height;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t visible_height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t colors_used = 0;
    uint32_t color_entries = 0;
    uint32_t pixel_offset;
    uint32_t xor_stride;
    uint32_t and_stride;
    uint32_t xor_bytes;
    uint32_t and_offset;
    uint16_t dest_width;
    uint16_t dest_height;
    bool top_down;
    bool has_alpha = false;

    if (data == NULL || size < 40) {
        return false;
    }
    dib_size = graphics_read_le32(data);
    if (dib_size < 40 || dib_size > size) {
        return false;
    }
    dib_width = (int32_t) graphics_read_le32(data + 4);
    dib_height = (int32_t) graphics_read_le32(data + 8);
    planes = graphics_read_le16(data + 12);
    bpp = graphics_read_le16(data + 14);
    compression = graphics_read_le32(data + 16);
    if (dib_size >= 40) {
        colors_used = graphics_read_le32(data + 32);
    }
    if (dib_width <= 0 || dib_height == 0 || planes != 1 ||
        compression != 0 || (bpp != 32 && bpp != 24)) {
        return false;
    }

    source_width = (uint32_t) dib_width;
    source_height = dib_height < 0 ? (uint32_t) -dib_height : (uint32_t) dib_height;
    top_down = dib_height < 0;
    visible_height = source_height;
    if (entry_height != 0 && source_height >= (uint32_t) entry_height * 2U) {
        visible_height = (uint32_t) entry_height;
    } else if ((source_height & 1U) == 0 && source_height / 2U <= 256U) {
        visible_height = source_height / 2U;
    }
    if (visible_height == 0 || source_width == 0) {
        return false;
    }
    if (source_width > 256U || visible_height > 256U) {
        return false;
    }

    if (bpp <= 8) {
        color_entries = colors_used != 0 ? colors_used : (1U << bpp);
    }
    pixel_offset = dib_size + color_entries * 4U;
    xor_stride = (((source_width * (uint32_t) bpp) + 31U) / 32U) * 4U;
    and_stride = ((source_width + 31U) / 32U) * 4U;
    if (pixel_offset > size || xor_stride == 0 || visible_height == 0 ||
        xor_stride > (size - pixel_offset) / visible_height) {
        return false;
    }
    xor_bytes = xor_stride * visible_height;
    and_offset = pixel_offset + xor_bytes;

    if (bpp == 32) {
        for (uint32_t sy = 0; sy < visible_height && !has_alpha; sy++) {
            uint32_t row_index = top_down ? sy : visible_height - 1U - sy;
            const uint8_t *row = data + pixel_offset + row_index * xor_stride;

            for (uint32_t sx = 0; sx < source_width; sx++) {
                if (row[sx * 4U + 3U] != 0) {
                    has_alpha = true;
                    break;
                }
            }
        }
    }

    dest_width = (uint16_t) (source_width > GRAPHICS_CURSOR_MAX_WIDTH ? GRAPHICS_CURSOR_MAX_WIDTH : source_width);
    dest_height = (uint16_t) (visible_height > GRAPHICS_CURSOR_MAX_HEIGHT ? GRAPHICS_CURSOR_MAX_HEIGHT : visible_height);
    if (dest_width == 0 || dest_height == 0) {
        return false;
    }

    memset(g_windows_cursor_pixels, 0, sizeof(g_windows_cursor_pixels));
    for (uint16_t dy = 0; dy < dest_height; dy++) {
        uint32_t sy = ((uint32_t) dy * visible_height) / dest_height;
        uint32_t row_index = top_down ? sy : visible_height - 1U - sy;
        const uint8_t *row = data + pixel_offset + row_index * xor_stride;
        const uint8_t *mask = NULL;

        if (and_offset <= size && and_stride != 0 &&
            and_stride <= (size - and_offset) / visible_height) {
            mask = data + and_offset + row_index * and_stride;
        }
        for (uint16_t dx = 0; dx < dest_width; dx++) {
            uint32_t sx = ((uint32_t) dx * source_width) / dest_width;
            const uint8_t *pixel = row + sx * (bpp / 8U);
            uint8_t alpha = 0xFF;
            uint32_t color;

            if (bpp == 32 && has_alpha) {
                alpha = pixel[3];
            }
            if (mask != NULL && (mask[sx / 8U] & (uint8_t) (0x80U >> (sx & 7U))) != 0) {
                alpha = 0;
            }
            if (alpha == 0) {
                color = 0;
            } else {
                color = 0xFF000000U |
                        ((uint32_t) pixel[2] << 16) |
                        ((uint32_t) pixel[1] << 8) |
                        (uint32_t) pixel[0];
            }
            g_windows_cursor_pixels[(uint32_t) dy * GRAPHICS_CURSOR_MAX_WIDTH + dx] = color;
        }
    }

    g_windows_cursor_width = dest_width;
    g_windows_cursor_height = dest_height;
    g_windows_cursor_hotspot_x = hotspot_x < dest_width ? hotspot_x : 0;
    g_windows_cursor_hotspot_y = hotspot_y < dest_height ? hotspot_y : 0;
    return true;
}

static bool graphics_load_windows_cursor_asset(void)
{
    uint8_t *data;
    int32_t size;
    uint16_t type;
    uint16_t count;
    uint32_t best_index = 0xFFFFFFFFU;
    uint32_t best_score = 0xFFFFFFFFU;

    if (g_windows_cursor_loaded) {
        return true;
    }
    if (g_windows_cursor_attempted) {
        return false;
    }

    size = file_size(UI_CURSOR_ASSET_PATH);
    if (size <= 0) {
        if (file_exists(UI_CURSOR_ASSET_PATH)) {
            g_windows_cursor_attempted = true;
        }
        return false;
    }
    g_windows_cursor_attempted = true;
    if ((uint32_t) size < 22U || (uint32_t) size > GRAPHICS_CURSOR_ASSET_MAX_BYTES) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) size);
    if (data == NULL) {
        return false;
    }
    if (file_read(UI_CURSOR_ASSET_PATH, data, (uint32_t) size) != size) {
        kfree(data);
        return false;
    }

    type = graphics_read_le16(data + 2);
    count = graphics_read_le16(data + 4);
    if (graphics_read_le16(data) != 0 || type != 2 || count == 0 ||
        count > 32 || 6U + (uint32_t) count * 16U > (uint32_t) size) {
        kfree(data);
        return false;
    }

    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = data + 6U + i * 16U;
        uint32_t width = entry[0] == 0 ? 256U : entry[0];
        uint32_t height = entry[1] == 0 ? 256U : entry[1];
        uint32_t bytes = graphics_read_le32(entry + 8);
        uint32_t offset = graphics_read_le32(entry + 12);
        uint32_t score;

        if (bytes == 0 || offset >= (uint32_t) size || bytes > (uint32_t) size - offset) {
            continue;
        }
        if (width <= GRAPHICS_CURSOR_MAX_WIDTH && height <= GRAPHICS_CURSOR_MAX_HEIGHT) {
            score = (GRAPHICS_CURSOR_MAX_WIDTH - width) + (GRAPHICS_CURSOR_MAX_HEIGHT - height);
        } else {
            score = 1000U + width + height;
        }
        if (score < best_score) {
            best_score = score;
            best_index = i;
        }
    }
    if (best_index == 0xFFFFFFFFU) {
        kfree(data);
        return false;
    }

    {
        const uint8_t *entry = data + 6U + best_index * 16U;
        uint16_t entry_width = entry[0] == 0 ? 256U : entry[0];
        uint16_t entry_height = entry[1] == 0 ? 256U : entry[1];
        uint16_t hotspot_x = graphics_read_le16(entry + 4);
        uint16_t hotspot_y = graphics_read_le16(entry + 6);
        uint32_t bytes = graphics_read_le32(entry + 8);
        uint32_t offset = graphics_read_le32(entry + 12);

        if (offset + 8U <= (uint32_t) size &&
            data[offset] == 0x89 && data[offset + 1] == 'P' && data[offset + 2] == 'N' && data[offset + 3] == 'G') {
            kfree(data);
            return false;
        }
        if (!graphics_cursor_decode_dib(data + offset, bytes, entry_width, entry_height, hotspot_x, hotspot_y)) {
            kfree(data);
            return false;
        }
    }

    g_windows_cursor_loaded = true;
    log_write("graphics: windows cursor loaded");
    kfree(data);
    return true;
}

static bool graphics_windows_cursor_ready(void)
{
    return g_windows_cursor_loaded || graphics_load_windows_cursor_asset();
}

static bool graphics_cursor_use_windows_asset(void)
{
    return g_cursor_style_index == 0 && graphics_windows_cursor_ready();
}

static bool graphics_load_wallpaper_asset(void)
{
    uint8_t *data;
    int32_t size;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t raw_height;
    uint32_t height;
    uint32_t stride;
    bool top_down;

    if (g_wallpaper_attempted) {
        return g_wallpaper_loaded;
    }
    g_wallpaper_attempted = true;
    size = file_size(UI_WALLPAPER_PATH);
    if (size <= 54 || (uint32_t) size > GRAPHICS_WALLPAPER_MAX_BYTES) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) size);
    if (data == NULL) {
        return false;
    }
    if (file_read(UI_WALLPAPER_PATH, data, (uint32_t) size) != size) {
        kfree(data);
        return false;
    }
    if (data[0] != 'B' || data[1] != 'M') {
        kfree(data);
        return false;
    }
    pixel_offset = graphics_read_le32(data + 10);
    dib_size = graphics_read_le32(data + 14);
    width = (int32_t) graphics_read_le32(data + 18);
    raw_height = (int32_t) graphics_read_le32(data + 22);
    if (dib_size < 40 || width <= 0 || raw_height == 0 ||
        width > GRAPHICS_WALLPAPER_MAX_WIDTH ||
        raw_height > GRAPHICS_WALLPAPER_MAX_HEIGHT ||
        raw_height < -(int32_t) GRAPHICS_WALLPAPER_MAX_HEIGHT ||
        graphics_read_le16(data + 26) != 1 ||
        graphics_read_le16(data + 28) != 24 ||
        graphics_read_le32(data + 30) != 0) {
        kfree(data);
        return false;
    }
    top_down = raw_height < 0;
    height = top_down ? (uint32_t) -raw_height : (uint32_t) raw_height;
    stride = (((uint32_t) width * 3U) + 3U) & ~3U;
    if (pixel_offset > (uint32_t) size || stride == 0 || height == 0 ||
        stride > ((uint32_t) size - pixel_offset) / height) {
        kfree(data);
        return false;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint32_t source_y = top_down ? y : height - 1U - y;
        const uint8_t *row = data + pixel_offset + source_y * stride;

        for (uint32_t x = 0; x < (uint32_t) width; x++) {
            uint8_t b = row[x * 3U];
            uint8_t g = row[x * 3U + 1U];
            uint8_t r = row[x * 3U + 2U];
            g_wallpaper_pixels[y * GRAPHICS_WALLPAPER_MAX_WIDTH + x] =
                ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
        }
    }
    g_wallpaper_width = (uint16_t) width;
    g_wallpaper_height = (uint16_t) height;
    g_wallpaper_loaded = true;
    kfree(data);
    return true;
}

static bool graphics_draw_wallpaper_asset(uint16_t height)
{
    uint32_t scaled_width;
    uint32_t scaled_height;
    uint32_t crop_x = 0;
    uint32_t crop_y = 0;

    if (height == 0 || !graphics_load_wallpaper_asset()) {
        return false;
    }

    if ((uint64_t) FB_WIDTH * g_wallpaper_height >=
        (uint64_t) height * g_wallpaper_width) {
        scaled_width = FB_WIDTH;
        scaled_height = ((uint32_t) FB_WIDTH * g_wallpaper_height) / g_wallpaper_width;
        crop_y = scaled_height > height ? (scaled_height - height) / 2U : 0;
    } else {
        scaled_height = height;
        scaled_width = ((uint32_t) height * g_wallpaper_width) / g_wallpaper_height;
        crop_x = scaled_width > FB_WIDTH ? (scaled_width - FB_WIDTH) / 2U : 0;
    }
    if (scaled_width == 0 || scaled_height == 0) {
        return false;
    }

    for (uint16_t y = 0; y < height; y++) {
        uint32_t sy = (((uint32_t) y + crop_y) * g_wallpaper_height) / scaled_height;

        if (sy >= g_wallpaper_height) {
            sy = g_wallpaper_height - 1U;
        }
        for (uint16_t x = 0; x < FB_WIDTH; x++) {
            uint32_t sx = (((uint32_t) x + crop_x) * g_wallpaper_width) / scaled_width;

            if (sx >= g_wallpaper_width) {
                sx = g_wallpaper_width - 1U;
            }
            graphics_plot(x, y, g_wallpaper_pixels[sy * GRAPHICS_WALLPAPER_MAX_WIDTH + sx]);
        }
    }
    return true;
}
static void graphics_draw_desktop(void)
{
    uint16_t usable_h = (uint16_t) (FB_HEIGHT > TASKBAR_HEIGHT ? FB_HEIGHT - TASKBAR_HEIGHT : FB_HEIGHT);
    uint16_t center_x = (uint16_t) (FB_WIDTH / 2);
    uint16_t fold_w = (uint16_t) (FB_WIDTH / 3);
    uint16_t fold_h = (uint16_t) (usable_h / 2);

    if (graphics_draw_wallpaper_asset(usable_h)) {
        graphics_fill_rect_gradient(0, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), FB_WIDTH, TASKBAR_HEIGHT, 0x00F7FBFF, UI_COLOR_TASKBAR);
        for (uint16_t x = 0; x < FB_WIDTH; x++) {
            graphics_plot(x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), UI_COLOR_TASKBAR_EDGE);
            graphics_plot(x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 1), 0x00FFFFFF);
        }
        return;
    }

    graphics_fill_vertical_gradient(0x000B1024, 0x00244CA8);
    for (uint16_t y = 0; y < usable_h; y++) {
        uint16_t left_w = (uint16_t) ((y * fold_w) / (usable_h == 0 ? 1 : usable_h));
        uint16_t right_w = (uint16_t) (((usable_h - y) * fold_w) / (usable_h == 0 ? 1 : usable_h));
        uint16_t glow_y = (uint16_t) (usable_h / 5 + y / 4);

        if (center_x > left_w) {
            graphics_fill_rect_gradient((uint16_t) (center_x - left_w), y, left_w, 1, 0x001B68D8, 0x006CB6FF);
        }
        if (center_x + right_w < FB_WIDTH) {
            graphics_fill_rect_gradient(center_x, y, right_w, 1, 0x006A52D8, 0x0020B9FF);
        }
        if (y < fold_h && glow_y < usable_h) {
            uint16_t glow_w = (uint16_t) (fold_w / 2 + y / 2);
            uint16_t glow_x = center_x > glow_w ? (uint16_t) (center_x - glow_w) : 0;
            graphics_fill_rect_gradient(glow_x, glow_y, (uint16_t) (glow_w * 2), 1, 0x001C8FE8, 0x0049D4FF);
        }
    }
    graphics_fill_rect_gradient(0, 0, FB_WIDTH, 72, 0x00142846, 0x000B1024);
    graphics_fill_rect_gradient(0, (uint16_t) (usable_h > 90 ? usable_h - 90 : 0), FB_WIDTH, 90, 0x00254DA8, 0x000B1024);
    graphics_fill_rect_gradient(0, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), FB_WIDTH, TASKBAR_HEIGHT, 0x00F7FBFF, UI_COLOR_TASKBAR);
    for (uint16_t x = 0; x < FB_WIDTH; x++) {
        graphics_plot(x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), UI_COLOR_TASKBAR_EDGE);
        graphics_plot(x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 1), 0x00FFFFFF);
    }
}

static void graphics_load_desktop_entries(void)
{
    char buffer[768];
    char desktop_path[GRAPHICS_FILE_PATH_MAX];
    uint32_t index = 0;

    g_desktop_entry_count = 0;
    graphics_current_desktop_path(desktop_path);
    if (!file_exists(desktop_path)) {
        file_mkdir(desktop_path);
    }
    if (!file_list_dir(desktop_path, buffer, sizeof(buffer))) {
        return;
    }

    for (uint32_t i = 0; buffer[i] != '\0' && index < DESKTOP_LABEL_MAX;) {
        uint32_t line_start = i;
        uint32_t length = 0;

        while (buffer[i] != '\0' && buffer[i] != '\n') {
            i++;
        }
        while (line_start + length < i && length < sizeof(g_desktop_entries[index].name) - 1) {
            g_desktop_entries[index].name[length] = buffer[line_start + length];
            length++;
        }
        if (length == 0) {
            continue;
        }
        g_desktop_entries[index].name[length] = '\0';
        g_desktop_entries[index].is_dir = graphics_is_separator(g_desktop_entries[index].name[length - 1]);
        if (g_desktop_entries[index].is_dir) {
            g_desktop_entries[index].name[length - 1] = '\0';
        }
        g_desktop_entry_count++;
        index++;
        if (buffer[i] == '\n') {
            i++;
        }
    }
}

static void graphics_desktop_icon_position(uint32_t index, uint16_t *out_x, uint16_t *out_y)
{
    if (out_x != NULL) {
        *out_x = (uint16_t) (DESKTOP_ICON_START_X + (index % DESKTOP_ICON_COLS) * (DESKTOP_ICON_W + DESKTOP_ICON_GAP_X));
    }
    if (out_y != NULL) {
        *out_y = (uint16_t) (DESKTOP_ICON_START_Y + (index / DESKTOP_ICON_COLS) * (DESKTOP_ICON_H + DESKTOP_ICON_GAP_Y));
    }
}

static void graphics_draw_desktop_icons(void)
{
    if (!g_session_logged_in) {
        return;
    }
    for (uint32_t i = 0; i < g_desktop_entry_count; i++) {
        char path[GRAPHICS_CLIPBOARD_PATH_MAX];
        char label[GRAPHICS_FILE_NAME_MAX];
        uint16_t x;
        uint16_t y;
        bool selected = g_last_click_source == UI_CLICK_SOURCE_DESKTOP && g_last_click_index == i;

        graphics_desktop_icon_position(i, &x, &y);
        graphics_build_desktop_entry_path(i, path);
        graphics_make_display_label(g_desktop_entries[i].name, label, sizeof(label));
        if (selected) {
            graphics_fill_soft_rect((uint16_t) (x + 4), y, 66, 74, 0x004C83C6);
            graphics_draw_soft_rect_outline((uint16_t) (x + 4), y, 66, 74, 0x00B8E2FF);
        }
        graphics_draw_file_icon((uint16_t) (x + 13), (uint16_t) (y + 5),
                                path, g_desktop_entries[i].is_dir, false);
        graphics_draw_text_aligned((uint16_t) (x + 1), (uint16_t) (y + 62),
                                   DESKTOP_ICON_W, label, 0x00344A5C);
        graphics_draw_text_aligned(x, (uint16_t) (y + 61), DESKTOP_ICON_W, label, 0x00FFFFFF);
    }
}

static uint16_t graphics_taskbar_pinned_start_x(void)
{
    return 12;
}

static uint16_t graphics_taskbar_running_start_x(void)
{
    uint32_t pinned_count = (uint32_t) (sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]));
    return (uint16_t) (graphics_taskbar_pinned_start_x() + START_BUTTON_WIDTH + 10u +
                       pinned_count * (BUTTON_WIDTH + BUTTON_GAP) + 10u);
}

static void graphics_draw_taskbar(void)
{
    if (!g_session_logged_in) {
        return;
    }
    uint16_t start_x = graphics_taskbar_pinned_start_x();
    uint16_t task_x;
    uint16_t start_icon_x;
    uint16_t start_icon_y;
    uint32_t start_fill = g_start_menu_open ? 0x00E8F0FF : 0x00FFFFFF;
    uint32_t start_border = g_start_menu_open ? UI_COLOR_ACCENT : 0x00D7E2EF;

    graphics_fill_soft_rect(start_x, BUTTON_Y, START_BUTTON_WIDTH, BUTTON_HEIGHT, start_fill);
    graphics_draw_soft_rect_outline(start_x, BUTTON_Y, START_BUTTON_WIDTH, BUTTON_HEIGHT, start_border);
    start_icon_x = (uint16_t) (start_x + 16);
    start_icon_y = (uint16_t) (BUTTON_Y + 9);
    graphics_fill_rect(start_icon_x, start_icon_y, 7, 7, 0x003C6FEA);
    graphics_fill_rect((uint16_t) (start_icon_x + 9), start_icon_y, 7, 7, 0x004DB5FF);
    graphics_fill_rect(start_icon_x, (uint16_t) (start_icon_y + 9), 7, 7, 0x004DB5FF);
    graphics_fill_rect((uint16_t) (start_icon_x + 9), (uint16_t) (start_icon_y + 9), 7, 7, 0x003C6FEA);

    task_x = (uint16_t) (start_x + START_BUTTON_WIDTH + 10);
    for (uint32_t i = 0; i < sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]); i++) {
        uint16_t x = (uint16_t) (task_x + i * (BUTTON_WIDTH + BUTTON_GAP));
        uint32_t fill = i == g_active_button_index ? 0x00FFFFFF : 0x00F6FAFF;
        uint32_t border = i == g_active_button_index ? UI_COLOR_ACCENT : 0x00D7E2EF;
        graphics_icon_kind_t icon_kind = GRAPHICS_ICON_FOLDER;

        graphics_fill_soft_rect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, fill);
        graphics_draw_soft_rect_outline(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, border);
        if (i == g_active_button_index) {
            graphics_fill_rect((uint16_t) (x + 14), (uint16_t) (BUTTON_Y + BUTTON_HEIGHT - 3), (uint16_t) (BUTTON_WIDTH - 28), 2, UI_COLOR_ACCENT);
        }
        if (i == 1) {
            icon_kind = GRAPHICS_ICON_TERMINAL;
        } else if (i == 2) {
            icon_kind = GRAPHICS_ICON_SETTINGS;
        } else if (i == 3) {
            icon_kind = GRAPHICS_ICON_INFO;
        }
        graphics_draw_icon((uint16_t) (x + 13), (uint16_t) (BUTTON_Y + 8), icon_kind, true);
    }

    task_x = graphics_taskbar_running_start_x();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible || g_windows[i].kind == UI_WINDOW_LOGON || g_windows[i].kind == UI_WINDOW_POWER) {
            continue;
        }
        if (task_x + UI_TASK_BUTTON_W >= FB_WIDTH - 184) {
            break;
        }
        graphics_fill_soft_rect(task_x, BUTTON_Y, UI_TASK_BUTTON_W, BUTTON_HEIGHT, 0x00FFFFFF);
        graphics_draw_soft_rect_outline(task_x, BUTTON_Y, UI_TASK_BUTTON_W, BUTTON_HEIGHT, 0x00CAD8E8);
        graphics_draw_icon((uint16_t) (task_x + 13), (uint16_t) (BUTTON_Y + 8), graphics_icon_kind_for_window(g_windows[i].kind), true);
        task_x = (uint16_t) (task_x + UI_TASK_BUTTON_W + 8);
    }
    graphics_draw_taskbar_status();
}

static bool graphics_taskbar_window_at(uint16_t x, uint16_t y, uint32_t *out_index)
{
    uint16_t task_x;

    if (y < BUTTON_Y || y >= BUTTON_Y + BUTTON_HEIGHT) {
        return false;
    }
    task_x = graphics_taskbar_running_start_x();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible || g_windows[i].kind == UI_WINDOW_LOGON || g_windows[i].kind == UI_WINDOW_POWER) {
            continue;
        }
        if (x >= task_x && x < task_x + UI_TASK_BUTTON_W) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return true;
        }
        task_x = (uint16_t) (task_x + UI_TASK_BUTTON_W + 8);
    }
    return false;
}

static void graphics_two_digits(char *out, uint8_t value)
{
    out[0] = (char) ('0' + (value / 10u));
    out[1] = (char) ('0' + (value % 10u));
}

static void graphics_four_digits(char *out, uint16_t value)
{
    out[0] = (char) ('0' + (value / 1000u) % 10u);
    out[1] = (char) ('0' + (value / 100u) % 10u);
    out[2] = (char) ('0' + (value / 10u) % 10u);
    out[3] = (char) ('0' + value % 10u);
}

static void graphics_draw_taskbar_status(void)
{
    cmos_time_t now;
    char time_text[9];
    char date_text[11];
    uint16_t panel_x = FB_WIDTH - 174;
    graphics_icon_kind_t net_icon = GRAPHICS_ICON_NETWORK_OFFLINE;
    ui_network_state_t net_state = ui_network_state();

    if (net_state == UI_NETWORK_CONNECTED) {
        net_icon = GRAPHICS_ICON_NETWORK_CONNECTED;
    } else if (net_state == UI_NETWORK_LIMITED || net_state == UI_NETWORK_VALIDATING) {
        net_icon = GRAPHICS_ICON_NETWORK_LIMITED;
    }

    cmos_read_time(&now);
    graphics_fill_soft_rect(panel_x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 7), 162, 38, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(panel_x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 7), 162, 38, 0x00D7E2EF);

    graphics_draw_icon((uint16_t) (panel_x + 12), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 16), net_icon, true);

    graphics_two_digits(&time_text[0], now.hour);
    time_text[2] = ':';
    graphics_two_digits(&time_text[3], now.minute);
    time_text[5] = ':';
    graphics_two_digits(&time_text[6], now.second);
    time_text[8] = '\0';

    graphics_four_digits(&date_text[0], now.year);
    date_text[4] = '-';
    graphics_two_digits(&date_text[5], now.month);
    date_text[7] = '-';
    graphics_two_digits(&date_text[8], now.day);
    date_text[10] = '\0';

    graphics_draw_text((uint16_t) (panel_x + 48), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 11), time_text, 0x001F2937);
    graphics_draw_text((uint16_t) (panel_x + 48), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 27), date_text, 0x00566A80);
}

static void graphics_draw_start_menu(void)
{
    if (!g_session_logged_in || !g_start_menu_open) {
        return;
    }

    static const char *root_items[] = { "\u5e94\u7528", "\u8bbe\u7f6e", "\u7f51\u7edc", "\u7535\u6e90" };
    static const char *apps_items[] = { "\u6587\u4ef6", "\u7ec8\u7aef", "\u64ad\u653e\u5668", "\u8bb0\u4e8b\u672c", "\u4efb\u52a1\u7ba1\u7406\u5668" };
    static const char *settings_items[] = { "\u663e\u793a", "\u6307\u9488", "\u7f51\u7edc" };
    static const char *power_items[] = { "\u5173\u673a", "\u91cd\u542f", "\u4f11\u7720" };
    uint16_t menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - START_MENU_H - 12);
    uint16_t root_top = (uint16_t) (menu_y + START_MENU_HEADER_H + 8);
    uint16_t left_x = (uint16_t) (START_MENU_X + 8);
    uint16_t mid_x = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + 12);
    uint16_t right_x = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + START_MENU_MID_W + 16);

    graphics_draw_shadow(START_MENU_X, menu_y, START_MENU_W, START_MENU_H);
    graphics_fill_soft_rect(START_MENU_X, menu_y, START_MENU_W, START_MENU_H, 0x00F7FAFE);
    graphics_draw_soft_rect_outline(START_MENU_X, menu_y, START_MENU_W, START_MENU_H, 0x00CFDAE8);
    graphics_fill_rect_gradient((uint16_t) (START_MENU_X + 2), (uint16_t) (menu_y + 2), (uint16_t) (START_MENU_W - 4), START_MENU_HEADER_H - 2, 0x00FFFFFF, 0x00EEF4FB);
    graphics_draw_text((uint16_t) (START_MENU_X + 16), (uint16_t) (menu_y + 19), "MONIOS", 0x001F2937);
    graphics_draw_text((uint16_t) (START_MENU_X + 92), (uint16_t) (menu_y + 19), "launcher", 0x00566A80);

    for (uint32_t i = 0; i < 4; i++) {
        uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
        bool root_selected = (g_start_menu_view == UI_START_MENU_APPS && i == 0) ||
                             (g_start_menu_view == UI_START_MENU_SETTINGS && i == 1) ||
                             (g_start_menu_view == UI_START_MENU_NETWORK && i == 2) ||
                             (g_start_menu_view == UI_START_MENU_POWER && i == 3);
        uint32_t fill = root_selected ? 0x00E7F0FF : 0x00FFFFFF;

        graphics_fill_soft_rect(left_x, row_y, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2, fill);
        if (root_selected) {
            graphics_fill_rect(left_x, (uint16_t) (row_y + 4), 3, (uint16_t) (UI_MENU_ITEM_H - 10), UI_COLOR_ACCENT);
        }
        graphics_draw_text((uint16_t) (left_x + 10), (uint16_t) (row_y + 6), root_items[i], 0x001F2937);
    }

    if (g_start_menu_view == UI_START_MENU_APPS || g_start_menu_view == UI_START_MENU_SETTINGS ||
        g_start_menu_view == UI_START_MENU_DISPLAY || g_start_menu_view == UI_START_MENU_CURSOR ||
        g_start_menu_view == UI_START_MENU_NETWORK || g_start_menu_view == UI_START_MENU_POWER) {
        graphics_fill_soft_rect(mid_x, root_top, START_MENU_MID_W - 12, 5 * UI_MENU_ITEM_H - 2, 0x00FFFFFF);
        graphics_draw_soft_rect_outline(mid_x, root_top, START_MENU_MID_W - 12, 5 * UI_MENU_ITEM_H - 2, 0x00D7E2EF);
    }

    if (g_start_menu_view == UI_START_MENU_APPS) {
        for (uint32_t i = 0; i < sizeof(apps_items) / sizeof(apps_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_fill_soft_rect(mid_x + 2, row_y, START_MENU_MID_W - 16, UI_MENU_ITEM_H - 2, 0x00F6FAFF);
            graphics_draw_text((uint16_t) (mid_x + 14), (uint16_t) (row_y + 6), apps_items[i], 0x001F2937);
        }
    } else if (g_start_menu_view == UI_START_MENU_SETTINGS) {
        for (uint32_t i = 0; i < sizeof(settings_items) / sizeof(settings_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_fill_soft_rect(mid_x + 2, row_y, START_MENU_MID_W - 16, UI_MENU_ITEM_H - 2, 0x00F6FAFF);
            graphics_draw_text((uint16_t) (mid_x + 14), (uint16_t) (row_y + 6), settings_items[i], 0x001F2937);
        }
    } else if (g_start_menu_view == UI_START_MENU_DISPLAY) {
        for (uint32_t i = 0; i < sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            uint32_t fill = i == g_graphics_mode_index ? UI_COLOR_ACCENT : 0x00F6FAFF;
            graphics_fill_soft_rect(right_x, row_y, START_MENU_RIGHT_W - 20, UI_MENU_ITEM_H - 2, fill);
            graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (row_y + 6), g_graphics_modes[i].label, i == g_graphics_mode_index ? 0x00FFFFFF : 0x001F2937);
        }
    } else if (g_start_menu_view == UI_START_MENU_CURSOR) {
        for (uint32_t i = 0; i < graphics_cursor_style_count(); i++) {
            uint16_t col = (uint16_t) (i & 1u);
            uint16_t row = (uint16_t) (i >> 1);
            uint16_t button_x = (uint16_t) (right_x + 12 + col * 82);
            uint16_t button_y = (uint16_t) (root_top + row * 28);
            uint32_t fill = i == g_cursor_style_index ? UI_COLOR_ACCENT : 0x00F6FAFF;

            graphics_fill_soft_rect(button_x, button_y, 70, 22, fill);
            graphics_draw_text_aligned(button_x, (uint16_t) (button_y + 6), 70, g_cursor_styles[i].label, i == g_cursor_style_index ? 0x00FFFFFF : 0x001F2937);
        }
    } else if (g_start_menu_view == UI_START_MENU_NETWORK) {
        graphics_fill_soft_rect(right_x, root_top, START_MENU_RIGHT_W - 20, 5 * UI_MENU_ITEM_H - 2, 0x00FFFFFF);
        graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (root_top + 6), "ping 172.16.58.1", 0x001F2937);
        graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (root_top + 30), "control panel", 0x001F2937);
    } else if (g_start_menu_view == UI_START_MENU_POWER) {
        for (uint32_t i = 0; i < sizeof(power_items) / sizeof(power_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_fill_soft_rect(mid_x + 2, row_y, START_MENU_MID_W - 16, UI_MENU_ITEM_H - 2, 0x00FFF5F6);
            graphics_draw_text((uint16_t) (mid_x + 14), (uint16_t) (row_y + 6), power_items[i], 0x007A1E2B);
        }
    }
}

static void graphics_draw_context_menu(void)
{
    uint16_t menu_h;

    if (!g_session_logged_in || !g_context_menu_open) {
        return;
    }

    menu_h = g_context_menu_mode == UI_CONTEXT_MENU_FILES ? CONTEXT_MENU_FILES_H : CONTEXT_MENU_DESKTOP_H;
    graphics_draw_shadow(g_context_menu_x, g_context_menu_y, CONTEXT_MENU_W, menu_h);
    graphics_fill_soft_rect(g_context_menu_x, g_context_menu_y, CONTEXT_MENU_W, menu_h, 0x001D2832);
    graphics_draw_soft_rect_outline(g_context_menu_x, g_context_menu_y, CONTEXT_MENU_W, menu_h, 0x006D8798);
    if (g_context_menu_mode == UI_CONTEXT_MENU_FILES) {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u590d\u5236", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u526a\u5207", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), g_clipboard_mode != UI_CLIPBOARD_NONE ? "\u7c98\u8d34" : "-", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u521b\u5efa\u5feb\u6377\u65b9\u5f0f", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 110), "\u6253\u5f00", 0x00FFFFFF);
    } else {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u5237\u65b0", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u8fd0\u884c", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), "\u8bbe\u7f6e", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u6ce8\u9500", 0x00FFFFFF);
    }
}

static void graphics_draw_power_menu(void)
{
    if (!g_power_menu_open) {
        return;
    }

    graphics_draw_shadow(g_power_menu_x, g_power_menu_y, 170, 104);
    graphics_fill_soft_rect(g_power_menu_x, g_power_menu_y, 170, 104, 0x001D2832);
    graphics_draw_soft_rect_outline(g_power_menu_x, g_power_menu_y, 170, 104, 0x006D8798);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 14), "\u5173\u673a", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 38), "\u91cd\u542f", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 62), "\u4f11\u7720", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 86), "\u6ce8\u9500", 0x00FFFFFF);
}

static void graphics_draw_control_panel_window(const ui_window_t *window)
{
    const net_info_t *info = net_info();
    char mode_text[32];
    char line[96];
    uint32_t mode_count = (uint32_t) (sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]));
    uint32_t cursor_count = graphics_cursor_style_count();

    if (window == NULL) {
        return;
    }

    graphics_make_resolution_label(mode_text, sizeof(mode_text), FB_WIDTH, FB_HEIGHT);
    graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "\u663e\u793a", 0x0017232E);
    strcpy(line, "\u5f53\u524d ");
    strcpy(line + strlen(line), mode_text);
    graphics_draw_text((uint16_t) (window->x + 76), (uint16_t) (window->y + 42), line, 0x004A6278);

    for (uint32_t i = 0; i < mode_count; i++) {
        uint16_t row = (uint16_t) (window->y + 70 + i * 32);
        uint32_t fill = i == g_graphics_mode_index ? 0x00256EC8 : 0x00F7FBFE;
        uint32_t border = i == g_graphics_mode_index ? 0x00CDE4FF : 0x00A7BBD1;
        uint32_t text = i == g_graphics_mode_index ? 0x00FFFFFF : 0x0017232E;

        graphics_fill_rect((uint16_t) (window->x + 18), row, 128, 24, fill);
        graphics_draw_rect_outline((uint16_t) (window->x + 18), row, 128, 24, border);
        graphics_draw_text_aligned((uint16_t) (window->x + 18), (uint16_t) (row + 7), 128, g_graphics_modes[i].label, text);
        if (i == 0) {
            graphics_draw_text((uint16_t) (window->x + 158), (uint16_t) (row + 7), "\u9ed8\u8ba4", 0x004A6278);
        }
    }

    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 42), "\u684c\u9762", 0x0017232E);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 66), "\u7a97\u53e3\u4f1a\u81ea\u52a8\u56de\u5230\u53ef\u89c1\u533a", 0x004A6278);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 90), "\u4efb\u52a1\u680f\u548c\u9f20\u6807\u8ddf\u968f\u5207\u6362", 0x004A6278);

    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 118), "\u6307\u9488", 0x0017232E);
    strcpy(line, "\u5f53\u524d ");
    strcpy(line + strlen(line), g_cursor_styles[g_cursor_style_index].label);
    graphics_draw_text((uint16_t) (window->x + 286), (uint16_t) (window->y + 118), line, 0x004A6278);
    graphics_fill_rect((uint16_t) (window->x + 238), (uint16_t) (window->y + 142), 38, 38, 0x00FFFFFF);
    graphics_draw_rect_outline((uint16_t) (window->x + 238), (uint16_t) (window->y + 142), 38, 38, 0x00A7BBD1);
    graphics_draw_cursor_bitmap((uint16_t) (window->x + 249), (uint16_t) (window->y + 153), &g_cursor_styles[g_cursor_style_index]);
    for (uint32_t i = 0; i < cursor_count; i++) {
        uint16_t col = (uint16_t) (i & 1u);
        uint16_t row = (uint16_t) (i >> 1);
        uint16_t button_x = (uint16_t) (window->x + 286 + col * 64);
        uint16_t button_y = (uint16_t) (window->y + 142 + row * 28);
        uint32_t fill = i == g_cursor_style_index ? 0x00256EC8 : 0x00F7FBFE;
        uint32_t border = i == g_cursor_style_index ? 0x00CDE4FF : 0x00A7BBD1;
        uint32_t text = i == g_cursor_style_index ? 0x00FFFFFF : 0x0017232E;

        graphics_fill_rect(button_x, button_y, 58, 22, fill);
        graphics_draw_rect_outline(button_x, button_y, 58, 22, border);
        graphics_draw_text_aligned(button_x, (uint16_t) (button_y + 6), 58, g_cursor_styles[i].label, text);
    }

    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 230), "\u7f51\u7edc", 0x0017232E);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 252), info->present ? info->driver : "offline", info->connected ? 0x002E7D32 : 0x00A0442F);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 272), info->ip_text, 0x004A6278);
    strcpy(line, "ping tx ");
    graphics_u32_to_dec(line + strlen(line), info->ping_requests);
    strcpy(line + strlen(line), " rx ");
    graphics_u32_to_dec(line + strlen(line), info->ping_replies);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 292), line, 0x004A6278);
    graphics_draw_text((uint16_t) (window->x + 238), (uint16_t) (window->y + 312), ui_network_label(), 0x004A6278);
}

static void graphics_draw_logon_window(const ui_window_t *window)
{
    uint16_t card_x;
    uint16_t card_y;
    uint16_t input_x;
    uint16_t user_x;
    uint16_t button_x;
    bool wallpaper_loaded;

    if (window == NULL) {
        return;
    }
    /* Windows 11 style full-screen login surface. */
    wallpaper_loaded = graphics_draw_wallpaper_asset(FB_HEIGHT);
    if (!wallpaper_loaded) {
        graphics_fill_vertical_gradient(0x001D4F8F, 0x00091424);
        graphics_fill_rect_gradient(0, 0, FB_WIDTH, 96, 0x00142432, 0x00091424);
        graphics_fill_rect_gradient(0, (uint16_t) (FB_HEIGHT > 120 ? FB_HEIGHT - 120 : 0), FB_WIDTH, 120, 0x00091424, 0x001B314D);
    }
    card_x = window->x;
    card_y = window->y;
    input_x = (uint16_t) (card_x + 62);
    user_x = (uint16_t) (card_x + 98);
    button_x = (uint16_t) (card_x + (window->width - 128) / 2);

    graphics_draw_shadow(card_x, card_y, window->width, window->height);
    graphics_fill_soft_rect(card_x, card_y, window->width, window->height, 0x00F7FBFF);
    graphics_draw_soft_rect_outline(card_x, card_y, window->width, window->height, 0x00DBE7F4);

    graphics_fill_soft_rect((uint16_t) (card_x + 152), (uint16_t) (card_y + 30), 76, 76, 0x00DCEBFF);
    graphics_draw_soft_rect_outline((uint16_t) (card_x + 152), (uint16_t) (card_y + 30), 76, 76, 0x00B7D4FF);
    graphics_fill_soft_rect((uint16_t) (card_x + 177), (uint16_t) (card_y + 47), 26, 24, 0x003C6FEA);
    graphics_fill_soft_rect((uint16_t) (card_x + 165), (uint16_t) (card_y + 72), 50, 24, 0x003C6FEA);

    graphics_draw_text_aligned(card_x, (uint16_t) (card_y + 118), window->width, "MONIOS", 0x001C2430);
    graphics_fill_soft_rect(user_x, (uint16_t) (card_y + 142), (uint16_t) (window->width - 196), 26,
                            g_login_field == 0 ? 0x00FFFFFF : 0x00F2F6FB);
    graphics_draw_soft_rect_outline(user_x, (uint16_t) (card_y + 142), (uint16_t) (window->width - 196), 26,
                                    g_login_field == 0 ? UI_COLOR_ACCENT : 0x00D3DFEC);
    graphics_draw_text_aligned(user_x, (uint16_t) (card_y + 149), (uint16_t) (window->width - 196), g_login_username, 0x002E3A48);

    graphics_fill_soft_rect(input_x, (uint16_t) (card_y + 178), (uint16_t) (window->width - 124), 30, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(input_x, (uint16_t) (card_y + 178), (uint16_t) (window->width - 124), 30,
                                    g_login_field == 1 ? UI_COLOR_ACCENT : 0x00BFCEDF);
    if (g_login_password[0] == '\0') {
        graphics_draw_text((uint16_t) (input_x + 12), (uint16_t) (card_y + 187), "Password", 0x008296A8);
    } else {
        for (uint32_t i = 0; g_login_password[i] != '\0' && i < 24; i++) {
            graphics_fill_soft_rect((uint16_t) (input_x + 12 + i * 10), (uint16_t) (card_y + 190), 5, 5, 0x002E3A48);
        }
    }

    graphics_fill_soft_rect(button_x, (uint16_t) (card_y + 222), 128, 30, UI_COLOR_ACCENT);
    graphics_draw_soft_rect_outline(button_x, (uint16_t) (card_y + 222), 128, 30, 0x00B7D8FF);
    graphics_draw_text_aligned(button_x, (uint16_t) (card_y + 231), 128, "Sign in", 0x00FFFFFF);
    if (session_auth_locked()) {
        graphics_draw_text_aligned(card_x, (uint16_t) (card_y + 262), window->width, "Too many attempts. Try again shortly.", 0x00C0392B);
    } else if (g_login_error) {
        graphics_draw_text_aligned(card_x, (uint16_t) (card_y + 262), window->width, "Invalid password", 0x00C0392B);
    }
}

static void graphics_draw_scrollbar(uint16_t x, uint16_t y, uint16_t height,
                                    uint32_t total, uint32_t visible, uint32_t offset,
                                    bool dark)
{
    uint16_t thumb_height;
    uint16_t thumb_y;
    uint32_t max_offset;
    uint32_t track_range;
    uint32_t thumb_offset;

    if (height == 0 || total == 0 || visible >= total) {
        return;
    }
    if (visible == 0) {
        visible = 1;
    }
    if (offset > total - visible) {
        offset = total - visible;
    }
    thumb_height = (uint16_t) (((uint32_t) height * visible) / total);
    if (thumb_height < 18) {
        thumb_height = 18;
    }
    if (thumb_height > height) {
        thumb_height = height;
    }
    max_offset = total - visible;
    track_range = height - thumb_height;
    thumb_offset = max_offset == 0 ? 0 : ((uint32_t) track_range * offset) / max_offset;

    graphics_fill_rect(x, y, GRAPHICS_SCROLLBAR_W, height, dark ? 0x00242D38 : 0x00E3EBF4);
    graphics_draw_rect_outline(x, y, GRAPHICS_SCROLLBAR_W, height, dark ? 0x004A6278 : 0x00A7BBD1);
    thumb_y = (uint16_t) (y + thumb_offset);
    graphics_fill_soft_rect(x + 2, thumb_y + 2, GRAPHICS_SCROLLBAR_W - 4,
                            thumb_height > 4 ? thumb_height - 4 : thumb_height,
                            dark ? 0x007B9BB7 : 0x007A9BC0);
}

static void graphics_draw_window_content(const ui_window_t *window)
{
    if (window->kind == UI_WINDOW_LOGON) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 40), "\u7528\u6237\u540d", 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 16), (uint16_t) (window->y + 56), (uint16_t) (window->width - 32), 24, 0x00FFFFFF);
        graphics_draw_rect_outline((uint16_t) (window->x + 16), (uint16_t) (window->y + 56), (uint16_t) (window->width - 32), 24, g_login_field == 0 ? 0x002A6CC8 : 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 24), (uint16_t) (window->y + 63), g_login_username, 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 94), "\u5bc6\u7801", 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 16), (uint16_t) (window->y + 110), (uint16_t) (window->width - 32), 24, 0x00FFFFFF);
        graphics_draw_rect_outline((uint16_t) (window->x + 16), (uint16_t) (window->y + 110), (uint16_t) (window->width - 32), 24, g_login_field == 1 ? 0x002A6CC8 : 0x008AA1B8);
        for (uint32_t i = 0; g_login_password[i] != '\0'; i++) {
            graphics_plot((uint16_t) (window->x + 24 + i * 8), (uint16_t) (window->y + 117), 0x0017232E);
            graphics_plot((uint16_t) (window->x + 28 + i * 8), (uint16_t) (window->y + 117), 0x0017232E);
        }
        if (g_login_error) {
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 144), "\u65e0\u6548\u51ed\u636e", 0x00C0392B);
        }
        graphics_fill_rect((uint16_t) (window->x + 16), (uint16_t) (window->y + window->height - 42), 92, 22, 0x00256EC8);
        graphics_draw_rect_outline((uint16_t) (window->x + 16), (uint16_t) (window->y + window->height - 42), 92, 22, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 16), (uint16_t) (window->y + window->height - 36), 92, "\u767b\u5f55", 0x00FFFFFF);
        return;
    }
    if (window->kind == UI_WINDOW_FILES) {
        uint32_t visible_rows = graphics_file_visible_rows(window);
        uint32_t first_item;
        uint32_t last_item;
        uint16_t list_top = (uint16_t) (window->y + 66);
        uint16_t list_height = window->height > 78 ? (uint16_t) (window->height - 78) : 1;
        uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);

        graphics_file_clamp_scroll(window);
        graphics_fill_rect((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x00DCE8F4);
        graphics_draw_rect_outline((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 18), (uint16_t) (window->y + 37), g_file_current_path, 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 12), list_top, 18, 18, 0x00D8E9FA);
        graphics_draw_rect_outline((uint16_t) (window->x + 12), list_top, 18, 18, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 17), (uint16_t) (list_top + 3), "<", 0x0017232E);
        first_item = g_file_scroll_offset;
        last_item = first_item + visible_rows;
        if (last_item > g_file_item_count) {
            last_item = g_file_item_count;
        }
        for (uint32_t i = first_item; i < last_item; i++) {
            uint32_t row_index = i - first_item;
            uint16_t row_y = (uint16_t) (list_top + row_index * 22);
            uint32_t fill = i == g_file_selected_index ? 0x00D8E9FA : 0x00F7FBFE;
            char path[GRAPHICS_CLIPBOARD_PATH_MAX];

            graphics_fill_rect((uint16_t) (window->x + 36), row_y,
                               (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W), 20, fill);
            graphics_draw_rect_outline((uint16_t) (window->x + 36), row_y,
                                       (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W), 20, 0x00A7BBD1);
            graphics_build_file_entry_path(i, path);
            graphics_draw_file_icon((uint16_t) (window->x + 42), (uint16_t) (row_y + 1),
                                    path, g_file_items[i].is_dir, true);
            graphics_draw_text((uint16_t) (window->x + 66), (uint16_t) (row_y + 5), g_file_items[i].name, 0x0017232E);
            if (g_file_items[i].is_dir) {
                graphics_draw_text_aligned((uint16_t) (window->x + window->width - 64), (uint16_t) (row_y + 5), 40, "\u6587\u4ef6\u5939", 0x002A6CC8);
            }
        }
        graphics_draw_scrollbar(scrollbar_x, list_top, list_height,
                                g_file_item_count, visible_rows, g_file_scroll_offset, false);
        return;
    }
    if (graphics_window_is_terminal(window)) {
        const uint32_t *buffer = console_buffer_for_pid(window->owner_pid);
        uint32_t visible_rows = graphics_terminal_visible_rows(window);
        uint32_t content_rows = graphics_terminal_content_rows(window);
        uint16_t list_top = (uint16_t) (window->y + 34);
        uint16_t list_height = window->height > 46 ? (uint16_t) (window->height - 46) : 1;
        uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);
        uint32_t max_scroll = graphics_terminal_max_scroll(window);
        uint32_t scroll_offset = window->console_scroll_offset;

        if (max_scroll == 0) {
            scroll_offset = 0;
        } else if (!window->console_scroll_manual) {
            scroll_offset = max_scroll;
        } else if (scroll_offset > max_scroll) {
            scroll_offset = max_scroll;
        }
        graphics_fill_rect((uint16_t) (window->x + 12), list_top,
                           (uint16_t) (window->width - 24), list_height, 0x00161D27);
        for (uint32_t row = 0; row < visible_rows; row++) {
            uint16_t draw_x = (uint16_t) (window->x + 16);

            if (scroll_offset + row >= content_rows) {
                continue;
            }
            for (uint16_t col = 0; col < CONSOLE_COLUMNS; col++) {
                uint32_t codepoint = buffer[(scroll_offset + row) * CONSOLE_COLUMNS + col];
                uint32_t advance;

                if (codepoint == ' ') {
                    draw_x = (uint16_t) (draw_x + UI_FONT_ADVANCE);
                    continue;
                }
                advance = font_codepoint_advance(codepoint);
                if ((uint32_t) draw_x + advance >=
                    (uint32_t) window->x + window->width - GRAPHICS_SCROLLBAR_W - 12) {
                    break;
                }
                graphics_draw_codepoint(draw_x,
                                        (uint16_t) (window->y + 40 + row * 14),
                                        codepoint,
                                        0x00E8EEF5);
                draw_x = (uint16_t) (draw_x + advance);
            }
        }
        graphics_draw_scrollbar(scrollbar_x, list_top, list_height,
                                content_rows, visible_rows, scroll_offset, true);
        return;
    }
    if (window->kind == UI_WINDOW_RUN) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "命令：", 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 116), (uint16_t) (window->y + 36), 170, 24, 0x00FFFFFF);
        graphics_draw_rect_outline((uint16_t) (window->x + 116), (uint16_t) (window->y + 36), 170, 24, g_run_input_focus ? 0x002A6CC8 : 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 122), (uint16_t) (window->y + 44), g_run_input, 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 204), (uint16_t) (window->y + 78), 72, 22, 0x00256EC8);
        graphics_draw_rect_outline((uint16_t) (window->x + 204), (uint16_t) (window->y + 78), 72, 22, 0x00CDE4FF);
        graphics_draw_text((uint16_t) (window->x + 224), (uint16_t) (window->y + 85), "打开", 0x00FFFFFF);
        return;
    }
    if (window->kind == UI_WINDOW_SHELL) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "命令行", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 64), "使用文本模式控制台", 0x0017232E);
        return;
    }
    if (window->kind == UI_WINDOW_ABOUT) {
        char version_content[2048];
        char line[256];
        uint32_t line_num = 0;
        uint32_t pos = 0;
        
        if (file_read(UI_VERSION_PATH, version_content, sizeof(version_content)) > 0) {
            while (pos < sizeof(version_content) && version_content[pos] != '\0' && line_num < 6) {
                uint32_t line_pos = 0;
                while (pos < sizeof(version_content) && version_content[pos] != '\n' && version_content[pos] != '\0' && line_pos < sizeof(line) - 1) {
                    line[line_pos++] = version_content[pos++];
                }
                line[line_pos] = '\0';
                if (version_content[pos] == '\n') pos++;
                graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42 + line_num * 22), line, 0x0017232E);
                line_num++;
            }
        } else {
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "MONIOS 桌面", 0x0017232E);
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 64), "版本 " MONIOS_VERSION, 0x0017232E);
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 86), "字体：微软雅黑", 0x0017232E);
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 108), "网络 声卡 图形", 0x0017232E);
        }
        return;
    }
    if (window->kind == UI_WINDOW_PLAYER) {
        uint32_t fill = g_player_button_pressed ? 0x00E2C35A : 0x00256EC8;
        char volume_text[16];
        uint8_t volume = audio_volume();
        uint16_t volume_fill_width;
        uint32_t volume_pos;
        const char *track = audio_current_track()[0] ? audio_current_track() : (g_player_selected_path[0] ? g_player_selected_path : "\u65e0\u97f3\u8f68");
        strcpy(volume_text, "\u97f3\u91cf ");
        volume_pos = (uint32_t) strlen(volume_text);
        if (volume == 100u) {
            volume_text[volume_pos++] = '1';
            volume_text[volume_pos++] = '0';
            volume_text[volume_pos++] = '0';
        } else if (volume >= 10u) {
            volume_text[volume_pos++] = (char) ('0' + (volume / 10u));
            volume_text[volume_pos++] = (char) ('0' + (volume % 10u));
        } else {
            volume_text[volume_pos++] = (char) ('0' + volume);
        }
        volume_text[volume_pos++] = '%';
        volume_text[volume_pos] = '\0';
        volume_fill_width = (uint16_t) ((volume * 142u) / 100u);
        graphics_draw_text_aligned((uint16_t) (window->x + 18), (uint16_t) (window->y + 42), (uint16_t) (window->width - 36), track, 0x0017232E);
        graphics_draw_text_aligned((uint16_t) (window->x + 18), (uint16_t) (window->y + 60), (uint16_t) (window->width - 36), g_player_status[0] ? g_player_status : "ready", 0x004A6278);
        /* Play/Pause button */
        graphics_fill_rect((uint16_t) (window->x + 20), (uint16_t) (window->y + 82), 78, 34, fill);
        graphics_draw_rect_outline((uint16_t) (window->x + 20), (uint16_t) (window->y + 82), 78, 34, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 20), (uint16_t) (window->y + 94), 78, audio_is_paused() ? "\u64ad\u653e" : "\u6682\u505c", 0x00FFFFFF);
        /* Stop button */
        graphics_fill_rect((uint16_t) (window->x + 110), (uint16_t) (window->y + 82), 78, 34, 0x00D96464);
        graphics_draw_rect_outline((uint16_t) (window->x + 110), (uint16_t) (window->y + 82), 78, 34, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 110), (uint16_t) (window->y + 94), 78, "\u505c\u6b62", 0x00FFFFFF);
        /* Open file button */
        graphics_fill_rect((uint16_t) (window->x + 200), (uint16_t) (window->y + 82), 136, 34, 0x003498DB);
        graphics_draw_rect_outline((uint16_t) (window->x + 200), (uint16_t) (window->y + 82), 136, 34, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 200), (uint16_t) (window->y + 94), 136, "\u9009\u62e9\u6587\u4ef6", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (window->x + 20), (uint16_t) (window->y + 128), volume_text, 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 92), (uint16_t) (window->y + 126), 142, 16, 0x00E8EEF5);
        graphics_fill_rect((uint16_t) (window->x + 92), (uint16_t) (window->y + 126), volume_fill_width, 16, 0x0037A66A);
        graphics_draw_rect_outline((uint16_t) (window->x + 92), (uint16_t) (window->y + 126), 142, 16, 0x008AA1B8);
        graphics_fill_rect((uint16_t) (window->x + 246), (uint16_t) (window->y + 123), 34, 22, 0x00F0F4F8);
        graphics_draw_rect_outline((uint16_t) (window->x + 246), (uint16_t) (window->y + 123), 34, 22, 0x008AA1B8);
        graphics_draw_text_aligned((uint16_t) (window->x + 246), (uint16_t) (window->y + 129), 34, "-", 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 292), (uint16_t) (window->y + 123), 34, 22, 0x00F0F4F8);
        graphics_draw_rect_outline((uint16_t) (window->x + 292), (uint16_t) (window->y + 123), 34, 22, 0x008AA1B8);
        graphics_draw_text_aligned((uint16_t) (window->x + 292), (uint16_t) (window->y + 129), 34, "+", 0x0017232E);
        if (g_player_browser_open) {
            graphics_fill_rect((uint16_t) (window->x + 14), (uint16_t) (window->y + 154), (uint16_t) (window->width - 28), 22, 0x00DCE8F4);
            graphics_draw_rect_outline((uint16_t) (window->x + 14), (uint16_t) (window->y + 154), (uint16_t) (window->width - 28), 22, 0x008AA1B8);
            graphics_draw_text((uint16_t) (window->x + 22), (uint16_t) (window->y + 160), g_player_current_path, 0x0017232E);
            graphics_fill_rect((uint16_t) (window->x + 14), (uint16_t) (window->y + 182), 24, 20, 0x00D8E9FA);
            graphics_draw_rect_outline((uint16_t) (window->x + 14), (uint16_t) (window->y + 182), 24, 20, 0x008AA1B8);
            graphics_draw_text((uint16_t) (window->x + 22), (uint16_t) (window->y + 187), "<", 0x0017232E);
            for (uint32_t i = 0; i < g_player_item_count && i < 6; i++) {
                uint16_t row_y = (uint16_t) (window->y + 182 + i * 22);
                uint16_t row_x = (uint16_t) (window->x + 44);
                uint16_t row_w = (uint16_t) (window->width - 58);
                uint32_t row_fill = i == g_player_selected_index ? 0x00D8E9FA : 0x00F7FBFE;
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];

                strcpy(path, g_player_current_path);
                graphics_append_path_component(path, sizeof(path), g_player_items[i].name);
                graphics_fill_rect(row_x, row_y, row_w, 20, row_fill);
                graphics_draw_rect_outline(row_x, row_y, row_w, 20, 0x00A7BBD1);
                graphics_draw_file_icon((uint16_t) (row_x + 4), (uint16_t) (row_y + 1),
                                        path, g_player_items[i].is_dir, true);
                graphics_draw_text((uint16_t) (row_x + 28), (uint16_t) (row_y + 5), g_player_items[i].name, 0x0017232E);
                if (g_player_items[i].is_dir) {
                    graphics_draw_text_aligned((uint16_t) (window->x + window->width - 76), (uint16_t) (row_y + 5), 54, "\u6587\u4ef6\u5939", 0x002A6CC8);
                }
            }
        }
        return;
    }
    if (window->kind == UI_WINDOW_NOTEPAD) {
        /* Menu bar: 文件 | 编辑 */
        graphics_fill_rect((uint16_t) (window->x + 8), (uint16_t) (window->y + 26), (uint16_t) (window->width - 16), 18, 0x00F0F4F8);
        graphics_draw_rect_outline((uint16_t) (window->x + 8), (uint16_t) (window->y + 26), (uint16_t) (window->width - 16), 18, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 30), "\u6587\u4ef6", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 66), (uint16_t) (window->y + 30), "\u7f16\u8f91", 0x0017232E);
        /* Text area */
        graphics_fill_rect((uint16_t) (window->x + 8), (uint16_t) (window->y + 48), (uint16_t) (window->width - 16), (uint16_t) (window->height - 58), 0x00FFFFFF);
        graphics_draw_rect_outline((uint16_t) (window->x + 8), (uint16_t) (window->y + 48), (uint16_t) (window->width - 16), (uint16_t) (window->height - 58), 0x008AA1B8);
        {
            const char *cursor = g_notepad_text;
            uint16_t draw_x = (uint16_t) (window->x + 14);
            uint32_t row = 0;
            uint16_t right = (uint16_t) (window->x + window->width - 14);

            while (cursor != NULL && *cursor != '\0' && row < 16) {
                uint32_t codepoint = font_utf8_next(&cursor);
                uint32_t advance;

                if (codepoint == '\r') {
                    continue;
                }
                if (codepoint == '\n') {
                    row++;
                    draw_x = (uint16_t) (window->x + 14);
                    continue;
                }
                advance = font_codepoint_advance(codepoint);
                if ((uint32_t) draw_x + advance > right) {
                    row++;
                    draw_x = (uint16_t) (window->x + 14);
                    if (row >= 16) {
                        break;
                    }
                }
                graphics_draw_codepoint(draw_x,
                                        (uint16_t) (window->y + 54 + row * UI_FONT_HEIGHT),
                                        codepoint,
                                        0x0017232E);
                draw_x = (uint16_t) (draw_x + advance);
            }
        }
        return;
    }
    if (window->kind == UI_WINDOW_TASKMGR) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "任务管理器", 0x0017232E);
        if (task_count() > 0) {
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 66), "任务", 0x0017232E);
            graphics_draw_text((uint16_t) (window->x + 70), (uint16_t) (window->y + 66), "运行中", 0x0017232E);
        }
        if (net_info()->present) {
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 90), ui_network_label(), 0x0017232E);
        }
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 114), audio_current_track()[0] ? audio_current_track() : "音频已停止", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 138), "图形", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 48), (uint16_t) (window->y + 138), graphics_gpu_pending_count() ? "等待中" : "空闲", 0x0017232E);
        return;
    }
    if (window->kind == UI_WINDOW_CUBE3D) {
        graphics_draw_cube3d_window(window);
        return;
    }
    if (window->kind == UI_WINDOW_UAC) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 38),
                           g_uac_privilege_level == EXEC_PRIV_R0 ? "\u8be5\u7a0b\u5e8f\u9700\u8981 R0 \u6743\u9650" : "\u8be5\u7a0b\u5e8f\u9700\u8981 R2 \u6743\u9650",
                           0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 60), g_uac_program[0] ? g_uac_program : "\u672a\u77e5\u7a0b\u5e8f", 0x004A6278);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 82), g_uac_reason[0] ? g_uac_reason : "\u8bf7\u8f93\u5165\u5bc6\u7801", 0x004A6278);
        graphics_fill_rect((uint16_t) (window->x + 16), (uint16_t) (window->y + 108), (uint16_t) (window->width - 32), 24, 0x00FFFFFF);
        graphics_draw_rect_outline((uint16_t) (window->x + 16), (uint16_t) (window->y + 108), (uint16_t) (window->width - 32), 24, g_uac_input_focus ? 0x002A6CC8 : 0x008AA1B8);
        for (uint32_t i = 0; g_uac_password[i] != '\0'; i++) {
            graphics_plot((uint16_t) (window->x + 24 + i * 8), (uint16_t) (window->y + 115), 0x0017232E);
            graphics_plot((uint16_t) (window->x + 28 + i * 8), (uint16_t) (window->y + 115), 0x0017232E);
        }
        if (g_uac_error) {
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 138), "\u5bc6\u7801\u9519\u8bef", 0x00C0392B);
        }
        graphics_fill_rect((uint16_t) (window->x + 200), (uint16_t) (window->y + 150), 72, 22, 0x00256EC8);
        graphics_draw_rect_outline((uint16_t) (window->x + 200), (uint16_t) (window->y + 150), 72, 22, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 200), (uint16_t) (window->y + 157), 72, "\u63d0\u6743", 0x00FFFFFF);
        graphics_fill_rect((uint16_t) (window->x + 282), (uint16_t) (window->y + 150), 72, 22, 0x00D96464);
        graphics_draw_rect_outline((uint16_t) (window->x + 282), (uint16_t) (window->y + 150), 72, 22, 0x00CDE4FF);
        graphics_draw_text_aligned((uint16_t) (window->x + 282), (uint16_t) (window->y + 157), 72, "\u53d6\u6d88", 0x00FFFFFF);
        return;
    }
    if (window->kind == UI_WINDOW_CONTROL_PANEL) {
        graphics_draw_control_panel_window(window);
        return;
    }
    if (window->kind == UI_WINDOW_POWER) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "关机", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 68), "重启", 0x0017232E);
    }
}

static void graphics_draw_window(const ui_window_t *window)
{
    if (!window->visible || window->minimized) {
        return;
    }

    if (window->kind == UI_WINDOW_LOGON) {
        graphics_draw_logon_window(window);
        return;
    }

    graphics_draw_shadow(window->x, window->y, window->width, window->height);
    graphics_fill_soft_rect(window->x, window->y, window->width, window->height, UI_COLOR_WINDOW_BG);
    graphics_draw_soft_rect_outline(window->x, window->y, window->width, window->height, UI_COLOR_WINDOW_EDGE);
    graphics_fill_rect_gradient((uint16_t) (window->x + 2), (uint16_t) (window->y + 2), (uint16_t) (window->width - 4), (uint16_t) (UI_TITLEBAR_H - 2), 0x00FFFFFF, UI_COLOR_TITLE_BG);
    graphics_fill_rect((uint16_t) (window->x + 2), (uint16_t) (window->y + UI_TITLEBAR_H), (uint16_t) (window->width - 4), 1, 0x00DFE7F0);
    graphics_fill_soft_rect((uint16_t) (window->x + 10), (uint16_t) (window->y + 8), 10, 10, UI_COLOR_ACCENT);
    graphics_fill_rect((uint16_t) (window->x + 15), (uint16_t) (window->y + 8), 5, 10, 0x004DB5FF);
    graphics_draw_text_clipped((uint16_t) (window->x + 28),
                               (uint16_t) (window->y + 7),
                               window->width > 112 ? (uint16_t) (window->width - 112) : 1,
                               window->title,
                               UI_COLOR_TITLE_TEXT);
    graphics_fill_soft_rect((uint16_t) (window->x + window->width - 66), (uint16_t) (window->y + 5), 16, 18, 0x00F5F8FC);
    graphics_draw_text((uint16_t) (window->x + window->width - 62), (uint16_t) (window->y + 6), "-", 0x004B5C70);
    graphics_fill_soft_rect((uint16_t) (window->x + window->width - 46), (uint16_t) (window->y + 5), 16, 18, window->maximized ? 0x00E7F0FF : 0x00F5F8FC);
    graphics_draw_text((uint16_t) (window->x + window->width - 43), (uint16_t) (window->y + 6), "[]", 0x004B5C70);
    graphics_fill_soft_rect((uint16_t) (window->x + window->width - 26), (uint16_t) (window->y + 5), 18, 18, 0x00FFF5F6);
    graphics_draw_text((uint16_t) (window->x + window->width - 22), (uint16_t) (window->y + 6), "X", 0x00A82636);
    graphics_draw_window_content(window);
}

static void graphics_draw_power_overlay(void)
{
    if (!g_power_menu_open) {
        return;
    }

    graphics_fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 0x000F141C);
    graphics_fill_rect(350, 180, 324, 180, 0x00171F2A);
    graphics_draw_rect_outline(350, 180, 324, 180, 0x00A0C8F4);
    graphics_draw_text(392, 210, "电源选项", 0x00FFFFFF);
    graphics_draw_text(392, 252, "关机", 0x00D6E8FF);
    graphics_draw_text(392, 282, "重启", 0x00D6E8FF);
    graphics_draw_text(392, 312, "取消", 0x00D6E8FF);
}

void graphics_draw_shell(void)
{
    if (!g_graphics_active) {
        return;
    }

    if (!g_session_logged_in && !g_installer_mode && graphics_find_window(UI_WINDOW_LOGON) < 0) {
        graphics_open_window(UI_WINDOW_LOGON);
    }
    graphics_draw_desktop();
    graphics_load_desktop_entries();
    graphics_draw_desktop_icons();
    graphics_draw_taskbar();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        graphics_draw_window(&g_windows[i]);
    }
    graphics_draw_start_menu();
    graphics_draw_context_menu();
    graphics_draw_power_menu();
    graphics_draw_power_overlay();
    if (g_rainbow_cat_open) {
        graphics_draw_rainbow_cat();
    }
    graphics_present();
    g_cursor_drawn = false;
    graphics_mouse_redraw(g_cursor_x, g_cursor_y);
}

void graphics_refresh_desktop_entries(void)
{
    graphics_draw_shell();
}

static bool graphics_should_refresh_desktop(void)
{
    return false;
}

static bool graphics_set_resolution(uint16_t width, uint16_t height)
{
    int32_t mode_index = graphics_find_mode_index(width, height);
    uint16_t prev_width = g_graphics_width;
    uint16_t prev_height = g_graphics_height;
    uint8_t prev_mode_index = g_graphics_mode_index;

    if (mode_index < 0) {
        return false;
    }
    if (g_graphics_width == width && g_graphics_height == height) {
        return true;
    }
    g_graphics_width = width;
    g_graphics_height = height;
    g_graphics_mode_index = (uint8_t) mode_index;
    if (g_graphics_active) {
        g_graphics_fast_mode_switch = true;
        graphics_leave_mode();
        graphics_enter_mode();
        g_graphics_fast_mode_switch = false;
        if (!g_graphics_active) {
            g_graphics_width = prev_width;
            g_graphics_height = prev_height;
            g_graphics_mode_index = prev_mode_index;
            g_graphics_fast_mode_switch = true;
            graphics_enter_mode();
            g_graphics_fast_mode_switch = false;
            return false;
        }
        graphics_reflow_windows();
        graphics_draw_shell();
    }
    return true;
}

void graphics_activate_primary_button(void)
{
    if (g_start_menu_open) {
        graphics_reset_start_menu();
    } else {
        graphics_open_start_menu_view(UI_START_MENU_ROOT);
    }
    graphics_draw_shell();
}

void graphics_handle_click(uint16_t x, uint16_t y)
{
    if (g_power_menu_open) {
        if (graphics_point_in_rect(x, y, (uint16_t) (g_power_menu_x + 8), (uint16_t) (g_power_menu_y + 8), 140, 22)) {
            kernel_request_shutdown();
            return;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (g_power_menu_x + 8), (uint16_t) (g_power_menu_y + 32), 140, 22)) {
            kernel_request_reboot();
            return;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (g_power_menu_x + 8), (uint16_t) (g_power_menu_y + 56), 140, 22)) {
            kernel_request_sleep();
            g_power_menu_open = false;
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (g_power_menu_x + 8), (uint16_t) (g_power_menu_y + 80), 140, 22)) {
            g_session_logged_in = false;
            memset(g_windows, 0, sizeof(g_windows));
            graphics_open_window(UI_WINDOW_LOGON);
            g_power_menu_open = false;
            graphics_draw_shell();
            return;
        }
        g_power_menu_open = false;
        graphics_draw_shell();
        return;
    }

    if (g_rainbow_cat_open) {
        graphics_draw_shell();
        return;
    }

    if (!g_graphics_active) {
        return;
    }
    if (g_sleeping) {
        g_sleeping = false;
        graphics_draw_shell();
        return;
    }
    if (!g_session_logged_in) {
        for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
            ui_window_t *window = &g_windows[i];
            if (!window->visible || window->kind != UI_WINDOW_LOGON) {
                continue;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 98), (uint16_t) (window->y + 142), (uint16_t) (window->width - 196), 26)) {
                g_login_field = 0;
                g_login_error = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 62), (uint16_t) (window->y + 178), (uint16_t) (window->width - 124), 30)) {
                g_login_field = 1;
                g_login_error = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + (window->width - 128) / 2), (uint16_t) (window->y + 222), 128, 30)) {
                if (graphics_attempt_login()) {
                    graphics_draw_shell();
                    return;
                }
                graphics_draw_shell();
                return;
            }
        }
        return;
    }
    if (y >= BUTTON_Y && y < BUTTON_Y + BUTTON_HEIGHT) {
        uint32_t window_index;
        uint16_t start_x = graphics_taskbar_pinned_start_x();
        if (x >= start_x && x < start_x + START_BUTTON_WIDTH) {
            if (g_start_menu_open) {
                graphics_reset_start_menu();
            } else {
                graphics_open_start_menu_view(UI_START_MENU_ROOT);
            }
            graphics_draw_shell();
            return;
        }

        for (uint32_t i = 0; i < sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]); i++) {
            uint16_t button_x = (uint16_t) (start_x + START_BUTTON_WIDTH + 10 + i * (BUTTON_WIDTH + BUTTON_GAP));
            if (x >= button_x && x < button_x + BUTTON_WIDTH) {
                g_active_button_index = i;
                g_start_menu_open = false;
                g_start_menu_view = UI_START_MENU_ROOT;
                g_context_menu_open = false;
                if (i == 0) graphics_open_window(UI_WINDOW_FILES);
                if (i == 1) graphics_open_window(UI_WINDOW_TERMINAL);
                if (i == 2) graphics_open_window(UI_WINDOW_CONTROL_PANEL);
                if (i == 3) graphics_open_window(UI_WINDOW_ABOUT);
                graphics_draw_shell();
                return;
            }
        }
        if (graphics_taskbar_window_at(x, y, &window_index)) {
            graphics_bring_window_to_front(window_index);
            graphics_draw_shell();
            return;
        }
    }

    if (g_start_menu_open) {
        if (graphics_handle_start_menu_click(x, y)) {
            graphics_draw_shell();
            return;
        }
        graphics_reset_start_menu();
    }

    for (uint32_t desktop_index = 0; desktop_index < g_desktop_entry_count; desktop_index++) {
        uint16_t icon_x;
        uint16_t icon_y;

        graphics_desktop_icon_position(desktop_index, &icon_x, &icon_y);
        if (graphics_point_in_rect(x, y, icon_x, icon_y, DESKTOP_ICON_W, DESKTOP_ICON_H)) {
            char path[GRAPHICS_CLIPBOARD_PATH_MAX];
            bool same_item = g_last_click_source == UI_CLICK_SOURCE_DESKTOP && g_last_click_index == desktop_index;
            uint64_t now = timer_ticks();

            graphics_build_desktop_entry_path(desktop_index, path);
            g_last_click_source = UI_CLICK_SOURCE_DESKTOP;
            g_last_click_index = desktop_index;
            if (same_item && now - g_last_click_tick < timer_hz()) {
                graphics_open_path(path);
            }
            g_last_click_tick = now;
            graphics_draw_shell();
            return;
        }
    }

    if (g_context_menu_open) {
        if (g_context_menu_mode == UI_CONTEXT_MENU_FILES) {
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 8), 140, 22)) {
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_build_file_entry_path(g_file_selected_index, path);
                graphics_clipboard_set(path, g_file_items[g_file_selected_index].is_dir, UI_CLIPBOARD_COPY);
                g_context_menu_open = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 32), 140, 22)) {
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_build_file_entry_path(g_file_selected_index, path);
                graphics_clipboard_set(path, g_file_items[g_file_selected_index].is_dir, UI_CLIPBOARD_CUT);
                g_context_menu_open = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 56), 140, 22)) {
                char target[GRAPHICS_CLIPBOARD_PATH_MAX];
                if (g_clipboard_mode != UI_CLIPBOARD_NONE && !g_clipboard_is_dir) {
                    strcpy(target, g_file_current_path);
                    graphics_append_path_component(target, sizeof(target), graphics_path_basename(g_clipboard_path));
                    if (graphics_copy_file_path(g_clipboard_path, target) && g_clipboard_mode == UI_CLIPBOARD_CUT) {
                        file_delete(g_clipboard_path);
                        graphics_clipboard_set(NULL, false, UI_CLIPBOARD_NONE);
                    }
                    graphics_fill_file_browser();
                }
                g_context_menu_open = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 80), 140, 22)) {
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_build_file_entry_path(g_file_selected_index, path);
                g_context_menu_open = false;
                graphics_create_desktop_shortcut(path);
                graphics_refresh_desktop_entries();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 104), 140, 22)) {
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_build_file_entry_path(g_file_selected_index, path);
                g_context_menu_open = false;
                graphics_open_path(path);
                graphics_draw_shell();
                return;
            }
            g_context_menu_open = false;
            graphics_draw_shell();
            return;
        } else {
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 8), 140, 22)) {
                g_context_menu_open = false;
                graphics_refresh_desktop_entries();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 32), 140, 22)) {
                g_context_menu_open = false;
                graphics_open_window(UI_WINDOW_RUN);
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 56), 140, 22)) {
                g_context_menu_open = false;
                graphics_open_window(UI_WINDOW_CONTROL_PANEL);
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 80), 140, 22)) {
                g_context_menu_open = false;
                g_session_logged_in = false;
                memset(g_windows, 0, sizeof(g_windows));
                graphics_open_window(UI_WINDOW_LOGON);
                graphics_draw_shell();
                return;
            }
            g_context_menu_open = false;
            graphics_draw_shell();
            return;
        }
    }

    for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
        ui_window_t *window = &g_windows[i];
        if (!window->visible) {
            continue;
        }
        if (!graphics_point_in_rect(x, y, window->x, window->y, window->width, window->height)) {
            continue;
        }
        graphics_bring_window_to_front((uint32_t) i);
        window = &g_windows[UI_WINDOW_MAX - 1];
        if (graphics_point_in_rect(x, y, (uint16_t) (window->x + window->width - 62), (uint16_t) (window->y + 4), 14, 16)) {
            graphics_minimize_window(UI_WINDOW_MAX - 1);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (window->x + window->width - 44), (uint16_t) (window->y + 4), 14, 16)) {
            graphics_toggle_maximize_window(UI_WINDOW_MAX - 1);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, (uint16_t) (window->x + window->width - 26), (uint16_t) (window->y + 4), 18, 16)) {
            graphics_close_window(UI_WINDOW_MAX - 1);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, window->x, window->y, window->width, UI_TITLEBAR_H)) {
            g_dragging_window = true;
            g_drag_window_index = UI_WINDOW_MAX - 1;
            g_drag_offset_x = (int32_t) x - (int32_t) g_windows[UI_WINDOW_MAX - 1].x;
            g_drag_offset_y = (int32_t) y - (int32_t) g_windows[UI_WINDOW_MAX - 1].y;
            return;
        }
        if (window->kind == UI_WINDOW_FILES) {
            uint32_t visible_rows = graphics_file_visible_rows(window);
            uint16_t list_top = (uint16_t) (window->y + 66);
            uint16_t list_height = window->height > 78 ? (uint16_t) (window->height - 78) : 1;
            uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);

            graphics_set_terminal_focus(false);
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 12), (uint16_t) (window->y + 66), 18, 18)) {
                graphics_file_browser_go_up();
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, scrollbar_x, list_top, GRAPHICS_SCROLLBAR_W, list_height)) {
                uint32_t max_scroll = graphics_file_max_scroll(window);
                if (max_scroll > 0) {
                    uint32_t relative = y > list_top ? (uint32_t) (y - list_top) : 0;
                    g_file_scroll_offset = (relative * max_scroll) / list_height;
                    if (g_file_scroll_offset > max_scroll) {
                        g_file_scroll_offset = max_scroll;
                    }
                }
                graphics_draw_shell();
                return;
            }
            for (uint32_t row_index = 0; row_index < visible_rows; row_index++) {
                uint32_t entry_index = g_file_scroll_offset + row_index;
                uint16_t row_y = (uint16_t) (list_top + row_index * 22);
                if (entry_index < g_file_item_count &&
                    graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), row_y,
                                           (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W), 20)) {
                    bool same_item = g_file_selected_index == entry_index;
                    g_file_selected_index = entry_index;
                    graphics_file_ensure_selected_visible(window);
                    if (same_item) {
                        char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                        graphics_build_file_entry_path(entry_index, path);
                        if (g_file_items[entry_index].is_dir) {
                            graphics_file_browser_enter_selected();
                        } else {
                            graphics_open_path(path);
                        }
                    }
                    graphics_draw_shell();
                    return;
                }
            }
        }
        if (window->kind == UI_WINDOW_PLAYER) {
            /* Play/Pause button */
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 20), (uint16_t) (window->y + 82), 78, 34)) {
                g_player_button_pressed = true;
                if (!audio_is_playing()) {
                    if (g_player_selected_path[0] != '\0') {
                        graphics_player_try_play(g_player_selected_path);
                    } else {
                        graphics_player_try_play(PATH_ROOT "music.wav");
                    }
                } else {
                    audio_toggle_pause();
                }
                graphics_draw_shell();
                return;
            }
            /* Stop button */
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 110), (uint16_t) (window->y + 82), 78, 34)) {
                audio_shutdown();
                strcpy(g_player_status, "stopped");
                graphics_draw_shell();
                return;
            }
            /* Open music file button */
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 200), (uint16_t) (window->y + 82), 136, 34)) {
                graphics_player_open_browser();
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 246), (uint16_t) (window->y + 123), 34, 22)) {
                uint8_t volume = audio_volume();

                audio_set_volume(volume > 10u ? (uint8_t) (volume - 10u) : 0);
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 292), (uint16_t) (window->y + 123), 34, 22)) {
                uint8_t volume = audio_volume();

                audio_set_volume(volume < 90u ? (uint8_t) (volume + 10u) : 100u);
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 92), (uint16_t) (window->y + 126), 142, 16)) {
                uint16_t rel = (uint16_t) (x - (window->x + 92));

                audio_set_volume((uint8_t) ((rel * 100u) / 142u));
                graphics_draw_shell();
                return;
            }
            if (g_player_browser_open) {
                if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 14), (uint16_t) (window->y + 182), 24, 20)) {
                    if (graphics_pop_path_component(g_player_current_path)) {
                        g_player_selected_index = 0;
                        graphics_fill_player_browser();
                    }
                    graphics_draw_shell();
                    return;
                }
                for (uint32_t entry_index = 0; entry_index < g_player_item_count && entry_index < 6; entry_index++) {
                    uint16_t row_y = (uint16_t) (window->y + 182 + entry_index * 22);
                    if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 44), row_y, (uint16_t) (window->width - 58), 20)) {
                        char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                        g_player_selected_index = entry_index;
                        strcpy(path, g_player_current_path);
                        graphics_append_path_component(path, sizeof(path), g_player_items[entry_index].name);
                        if (g_player_items[entry_index].is_dir) {
                            strcpy(g_player_current_path, path);
                            g_player_selected_index = 0;
                            graphics_fill_player_browser();
                        } else {
                            graphics_player_try_play(path);
                        }
                        graphics_draw_shell();
                        return;
                    }
                }
            }
        }
        if (graphics_window_is_terminal(window)) {
            uint16_t content_left = (uint16_t) (window->x + 12);
            uint16_t content_top = (uint16_t) (window->y + window->height - 40);
            uint16_t content_width = 96;
            uint16_t content_height = 32;
            uint16_t list_top = (uint16_t) (window->y + 34);
            uint16_t list_height = window->height > 46 ? (uint16_t) (window->height - 46) : 1;
            uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);
            uint64_t now = timer_ticks();

            if (graphics_point_in_rect(x, y, scrollbar_x, list_top, GRAPHICS_SCROLLBAR_W, list_height)) {
                uint32_t max_scroll = graphics_terminal_max_scroll(window);

                graphics_set_terminal_focus(true);
                if (max_scroll > 0) {
                    uint32_t relative = y > list_top ? (uint32_t) (y - list_top) : 0;
                    window->console_scroll_offset = (relative * max_scroll) / list_height;
                    if (window->console_scroll_offset > max_scroll) {
                        window->console_scroll_offset = max_scroll;
                    }
                    window->console_scroll_manual = window->console_scroll_offset < max_scroll;
                } else {
                    window->console_scroll_offset = 0;
                    window->console_scroll_manual = false;
                }
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, content_left, content_top, content_width, content_height)) {
                if (now - g_rainbow_cat_last_click_tick < timer_hz()) {
                    g_rainbow_cat_click_count++;
                } else {
                    g_rainbow_cat_click_count = 1;
                }
                g_rainbow_cat_last_click_tick = now;
                if (g_rainbow_cat_click_count >= 2) {
                    g_rainbow_cat_open = true;
                    g_rainbow_cat_click_count = 0;
                    graphics_draw_shell();
                    return;
                }
            }
            graphics_set_terminal_focus(true);
            g_run_input_focus = false;
            g_notepad_focus = false;
            graphics_draw_shell();
            return;
        }
        if (window->kind == UI_WINDOW_NOTEPAD && graphics_point_in_rect(x, y, (uint16_t) (window->x + 8), (uint16_t) (window->y + 48), (uint16_t) (window->width - 16), (uint16_t) (window->height - 58))) {
            g_notepad_focus = true;
            graphics_set_terminal_focus(false);
            g_run_input_focus = false;
            graphics_bring_window_to_front((uint32_t) i);
            graphics_draw_shell();
            return;
        }
        /* Notepad menu bar: 文件 */
        if (window->kind == UI_WINDOW_NOTEPAD && graphics_point_in_rect(x, y, (uint16_t) (window->x + 8), (uint16_t) (window->y + 26), 50, 18)) {
            graphics_notepad_save_file();
            graphics_draw_shell();
            return;
        }
        if (window->kind == UI_WINDOW_RUN && graphics_point_in_rect(x, y, (uint16_t) (window->x + 116), (uint16_t) (window->y + 36), 170, 24)) {
            g_run_input_focus = true;
            graphics_bring_window_to_front((uint32_t) i);
            graphics_draw_shell();
            return;
        }
        if (window->kind == UI_WINDOW_RUN && graphics_point_in_rect(x, y, (uint16_t) (window->x + 204), (uint16_t) (window->y + 78), 72, 22)) {
            char command[sizeof(g_run_input)];
            strcpy(command, g_run_input);
            graphics_close_window(UI_WINDOW_MAX - 1);
            graphics_run_command_text(command);
            graphics_draw_shell();
            return;
        }
        if (window->kind == UI_WINDOW_UAC) {
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 16), (uint16_t) (window->y + 108), (uint16_t) (window->width - 32), 24)) {
                g_uac_input_focus = true;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 200), (uint16_t) (window->y + 150), 72, 22)) {
                bool ok = session_verify_password(g_uac_password);

                if (ok) {
                    g_uac_pending = false;
                    g_uac_result = true;
                    g_uac_result_ready = true;
                    graphics_close_window(UI_WINDOW_MAX - 1);
                    graphics_draw_shell();
                    return;
                }
                g_uac_error = true;
                g_uac_password_len = 0;
                g_uac_password[0] = '\0';
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 282), (uint16_t) (window->y + 150), 72, 22)) {
                g_uac_pending = false;
                g_uac_result = false;
                g_uac_result_ready = true;
                graphics_close_window(UI_WINDOW_MAX - 1);
                graphics_draw_shell();
                return;
            }
        }
        if (window->kind == UI_WINDOW_CONTROL_PANEL) {
            for (uint32_t mode = 0; mode < sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]); mode++) {
                uint16_t row = (uint16_t) (window->y + 70 + mode * 32);
                if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 18), row, 128, 24)) {
                    graphics_set_resolution(g_graphics_modes[mode].width, g_graphics_modes[mode].height);
                    return;
                }
            }
            for (uint32_t style = 0; style < graphics_cursor_style_count(); style++) {
                uint16_t col = (uint16_t) (style & 1u);
                uint16_t row = (uint16_t) (style >> 1);
                uint16_t button_x = (uint16_t) (window->x + 286 + col * 64);
                uint16_t button_y = (uint16_t) (window->y + 142 + row * 28);

                if (graphics_point_in_rect(x, y, button_x, button_y, 58, 22)) {
                    graphics_set_cursor_style((uint8_t) style);
                    return;
                }
            }
        }
        if (window->kind == UI_WINDOW_POWER) {
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 12), (uint16_t) (window->y + 34), 110, 22)) {
                kernel_request_shutdown();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 12), (uint16_t) (window->y + 60), 90, 22)) {
                kernel_request_reboot();
                return;
            }
        }
        graphics_draw_shell();
        return;
    }

    g_start_menu_open = false;
    g_last_click_source = UI_CLICK_SOURCE_NONE;
    graphics_draw_shell();
}

void graphics_handle_right_click(uint16_t x, uint16_t y)
{
    if (!g_graphics_active || !g_session_logged_in) {
        return;
    }
    if (g_sleeping) {
        g_sleeping = false;
    }

    g_context_menu_mode = UI_CONTEXT_MENU_DESKTOP;
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        ui_window_t *window = &g_windows[i];
        if (!window->visible || window->kind != UI_WINDOW_FILES) {
            continue;
        }
        if (graphics_point_in_rect(x, y, window->x, window->y, window->width, window->height)) {
            uint32_t visible_rows = graphics_file_visible_rows(window);
            uint16_t list_top = (uint16_t) (window->y + 66);

            for (uint32_t row_index = 0; row_index < visible_rows; row_index++) {
                uint32_t entry_index = g_file_scroll_offset + row_index;
                uint16_t row_y = (uint16_t) (list_top + row_index * 22);
                if (entry_index < g_file_item_count &&
                    graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), row_y,
                                           (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W), 20)) {
                    g_file_selected_index = entry_index;
                    g_context_menu_mode = UI_CONTEXT_MENU_FILES;
                    break;
                }
            }
            break;
        }
    }
    if (g_context_menu_mode == UI_CONTEXT_MENU_DESKTOP) {
        for (uint32_t desktop_index = 0; desktop_index < g_desktop_entry_count; desktop_index++) {
            uint16_t icon_x;
            uint16_t icon_y;

            graphics_desktop_icon_position(desktop_index, &icon_x, &icon_y);
            if (graphics_point_in_rect(x, y, icon_x, icon_y, DESKTOP_ICON_W, DESKTOP_ICON_H)) {
                g_last_click_source = UI_CLICK_SOURCE_DESKTOP;
                g_last_click_index = desktop_index;
                break;
            }
        }
    }
    graphics_context_open(x, y, g_context_menu_mode);
    graphics_draw_shell();
}

void graphics_handle_mouse_move(uint16_t x, uint16_t y, uint8_t buttons)
{
    if (!g_graphics_active) {
        return;
    }
    if (g_start_menu_open) {
        g_power_menu_open = false;
    }

    if (g_dragging_window && (buttons & MOUSE_BUTTON_LEFT) != 0 && g_drag_window_index < UI_WINDOW_MAX) {
        ui_window_t *window = &g_windows[g_drag_window_index];
        int32_t new_x = (int32_t) x - g_drag_offset_x;
        int32_t new_y = (int32_t) y - g_drag_offset_y;

        if (new_x < 8) new_x = 8;
        if (new_y < 8) new_y = 8;
        if (new_x > (int32_t) FB_WIDTH - (int32_t) window->width - 8) new_x = (int32_t) FB_WIDTH - (int32_t) window->width - 8;
        if (new_y > (int32_t) FB_HEIGHT - TASKBAR_HEIGHT - (int32_t) window->height - 8) new_y = (int32_t) FB_HEIGHT - TASKBAR_HEIGHT - (int32_t) window->height - 8;

        window->x = (uint16_t) new_x;
        window->y = (uint16_t) new_y;
        graphics_draw_shell();
    }

    if ((buttons & MOUSE_BUTTON_LEFT) == 0) {
        g_dragging_window = false;
        g_player_button_pressed = false;
    }
    g_prev_mouse_buttons = buttons;
}

void graphics_handle_mouse_wheel(uint16_t x, uint16_t y, int32_t delta)
{
    if (!g_graphics_active || delta == 0) {
        return;
    }
    for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
        ui_window_t *window = &g_windows[i];

        if (!window->visible || window->minimized ||
            !graphics_point_in_rect(x, y, window->x, window->y, window->width, window->height)) {
            continue;
        }
        if (window->kind == UI_WINDOW_FILES) {
            graphics_file_scroll_by(delta);
            graphics_draw_shell();
        } else if (graphics_window_is_terminal(window)) {
            graphics_terminal_scroll_by(delta, window);
            graphics_draw_shell();
        }
        return;
    }
}

void graphics_handle_alt_f4(void)
{
    for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
        if (g_windows[i].visible && g_windows[i].kind != UI_WINDOW_LOGON) {
            graphics_close_window((uint32_t) i);
            graphics_draw_shell();
            return;
        }
    }
    if (g_start_menu_open || g_context_menu_open || g_power_menu_open) {
        g_start_menu_open = false;
        g_context_menu_open = false;
        g_power_menu_open = false;
        graphics_draw_shell();
    }
}

void graphics_handle_key_event(const key_event_t *event)
{
    if (!g_graphics_active || event == NULL) {
        return;
    }
    if (g_installer_mode) {
        return;
    }

    if (g_sleeping) {
        g_sleeping = false;
        graphics_draw_shell();
        return;
    }

    if (g_rainbow_cat_open) {
        if (event->type == KEY_EVENT_ESC) {
            g_rainbow_cat_open = false;
            graphics_draw_shell();
        }
        return;
    }

    if (event->type == KEY_EVENT_CHAR && event->status.win_down && (event->ch == 'r' || event->ch == 'R')) {
        graphics_open_window(UI_WINDOW_RUN);
        graphics_draw_shell();
        return;
    }

    if (!g_session_logged_in) {
        if (event->type == KEY_EVENT_TAB) {
            g_login_field = g_login_field == 0 ? 1u : 0u;
            g_login_error = false;
            graphics_draw_shell();
            return;
        }
        if (event->type != KEY_EVENT_CHAR) {
            return;
        }
        if (event->ch == '\n') {
            graphics_attempt_login();
            graphics_draw_shell();
            return;
        }
        if (event->ch == '\b') {
            char *field = g_login_field == 0 ? g_login_username : g_login_password;
            uint32_t len = (uint32_t) strlen(field);
            if (len > 0) {
                field[len - 1] = '\0';
            }
            g_login_error = false;
            graphics_draw_shell();
            return;
        }
        if (event->ch >= 32 && event->ch <= 126) {
            char *field = g_login_field == 0 ? g_login_username : g_login_password;
            uint32_t max_len = g_login_field == 0 ? sizeof(g_login_username) : sizeof(g_login_password);
            uint32_t len = (uint32_t) strlen(field);
            if (len + 1 < max_len) {
                field[len] = event->ch;
                field[len + 1] = '\0';
            }
            g_login_error = false;
            graphics_draw_shell();
        }
        return;
    }

    if (g_uac_pending) {
        if (event->type == KEY_EVENT_ESC) {
            int32_t index = graphics_find_window(UI_WINDOW_UAC);

            g_uac_result = false;
            g_uac_result_ready = true;
            g_uac_pending = false;
            if (index >= 0) {
                graphics_close_window((uint32_t) index);
            }
            graphics_draw_shell();
            return;
        }
        if (event->type != KEY_EVENT_CHAR) {
            return;
        }
        if (event->ch == '\b') {
            if (g_uac_password_len > 0) {
                g_uac_password[--g_uac_password_len] = '\0';
            }
        } else if (event->ch == '\n') {
            int32_t index = graphics_find_window(UI_WINDOW_UAC);

            if (session_verify_password(g_uac_password)) {
                g_uac_result = true;
                g_uac_result_ready = true;
                g_uac_pending = false;
                if (index >= 0) {
                    graphics_close_window((uint32_t) index);
                }
            } else {
                g_uac_error = true;
                g_uac_password_len = 0;
                g_uac_password[0] = '\0';
            }
        } else if (event->ch >= 32 && event->ch <= 126 && g_uac_password_len + 1 < sizeof(g_uac_password)) {
            g_uac_password[g_uac_password_len++] = event->ch;
            g_uac_password[g_uac_password_len] = '\0';
            g_uac_error = false;
        }
        graphics_draw_shell();
        return;
    }

    if (event->type != KEY_EVENT_CHAR) {
        int32_t focused_window_index = -1;

        for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
            if (g_windows[i].visible && !g_windows[i].minimized) {
                focused_window_index = i;
                break;
            }
        }
        if (focused_window_index >= 0 &&
            g_windows[focused_window_index].kind == UI_WINDOW_FILES) {
            ui_window_t *window = &g_windows[focused_window_index];

            if (event->type == KEY_EVENT_UP && g_file_selected_index > 0) {
                g_file_selected_index--;
            } else if (event->type == KEY_EVENT_DOWN &&
                       g_file_selected_index + 1 < g_file_item_count) {
                g_file_selected_index++;
            } else if (event->type == KEY_EVENT_HOME) {
                g_file_selected_index = 0;
            } else if (event->type == KEY_EVENT_END && g_file_item_count > 0) {
                g_file_selected_index = g_file_item_count - 1;
            } else {
                return;
            }
            graphics_file_ensure_selected_visible(window);
            graphics_draw_shell();
            return;
        }
        if (g_terminal_input_focus) {
            if (event->type == KEY_EVENT_UP) shell_handle_navigation_key(KEY_EVENT_UP);
            if (event->type == KEY_EVENT_DOWN) shell_handle_navigation_key(KEY_EVENT_DOWN);
            if (event->type == KEY_EVENT_LEFT) shell_handle_navigation_key(KEY_EVENT_LEFT);
            if (event->type == KEY_EVENT_RIGHT) shell_handle_navigation_key(KEY_EVENT_RIGHT);
            if (event->type == KEY_EVENT_HOME) shell_handle_special_key(KEY_EVENT_HOME);
            if (event->type == KEY_EVENT_END) shell_handle_special_key(KEY_EVENT_END);
            if (event->type == KEY_EVENT_DELETE) shell_handle_special_key(KEY_EVENT_DELETE);
            if (event->type == KEY_EVENT_TAB || event->type == KEY_EVENT_CTRL_C) {
                shell_handle_key_event(event);
            }
            graphics_draw_shell();
        }
        return;
    }

    if (g_terminal_input_focus) {
        shell_handle_key_event(event);
        graphics_draw_shell();
        return;
    }

    if (g_notepad_focus) {
        if (event->ch == '\b') {
            if (g_notepad_len > 0) {
                g_notepad_text[--g_notepad_len] = '\0';
            }
        } else if (event->ch == '\n') {
            if (g_notepad_len + 1 < sizeof(g_notepad_text)) {
                g_notepad_text[g_notepad_len++] = '\n';
                g_notepad_text[g_notepad_len] = '\0';
            }
        } else if (event->ch >= 32 && event->ch <= 126 && g_notepad_len + 1 < sizeof(g_notepad_text)) {
            g_notepad_text[g_notepad_len++] = event->ch;
            g_notepad_text[g_notepad_len] = '\0';
        }
        graphics_draw_shell();
        return;
    }

    if (g_run_input_focus) {
        if (event->ch == '\b') {
            if (g_run_input_len > 0) {
                g_run_input[--g_run_input_len] = '\0';
            }
        } else if (event->ch == '\n') {
            char command[sizeof(g_run_input)];
            strcpy(command, g_run_input);
            for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
                if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_RUN) {
                    graphics_close_window(i);
                    break;
                }
            }
            graphics_run_command_text(command);
        } else if (event->ch >= 32 && event->ch <= 126 && g_run_input_len + 1 < sizeof(g_run_input)) {
            g_run_input[g_run_input_len++] = event->ch;
            g_run_input[g_run_input_len] = '\0';
        }
        graphics_draw_shell();
    }
}

void graphics_periodic_update(uint64_t now_ticks)
{
    if (!g_graphics_active) {
        return;
    }

    ui_network_update(now_ticks);

    if (!font_ready()) {
        if (font_init_step(128u * 1024u)) {
            graphics_draw_shell();
            return;
        }
    }

    if (g_session_logged_in && g_cube3d_open) {
        uint64_t cube_step = timer_hz() / 30u;
        if (cube_step == 0) {
            cube_step = 1;
        }
        if (g_cube3d_last_tick == 0 || now_ticks - g_cube3d_last_tick >= cube_step) {
            g_cube3d_last_tick = now_ticks;
            g_cube3d_angle_x += 0.020f;
            g_cube3d_angle_y += 0.018f;
            g_cube3d_angle_z += 0.014f;
            graphics_draw_shell();
            return;
        }
    }

    if (g_session_logged_in && (g_last_status_tick == 0 || now_ticks - g_last_status_tick >= timer_hz())) {
        g_last_status_tick = now_ticks;
        graphics_draw_shell();
        return;
    }

    if (g_session_logged_in && now_ticks - g_last_desktop_scan_tick >= UI_DESKTOP_POLL_TICKS) {
        g_last_desktop_scan_tick = now_ticks;
        if (graphics_should_refresh_desktop()) {
            graphics_refresh_desktop_entries();
        }
    }
}

static void graphics_restore_cursor(void)
{
    if (!g_graphics_active || !g_cursor_drawn) {
        return;
    }

    for (uint16_t row = 0; row < g_cursor_draw_height; row++) {
        for (uint16_t col = 0; col < g_cursor_draw_width; col++) {
            uint16_t px = g_cursor_x + col;
            uint16_t py = g_cursor_y + row;
            if (px < FB_WIDTH && py < FB_HEIGHT) {
                g_framebuffer[graphics_framebuffer_index(px, py)] =
                    g_cursor_saved[(uint32_t) row * GRAPHICS_CURSOR_MAX_WIDTH + col];
            }
        }
    }
    g_cursor_drawn = false;
    g_cursor_draw_width = 0;
    g_cursor_draw_height = 0;
}

void graphics_mouse_redraw(uint16_t x, uint16_t y)
{
    const graphics_cursor_style_t *style;
    bool use_windows_cursor;
    uint16_t cursor_width;
    uint16_t cursor_height;

    if (!g_graphics_active) {
        return;
    }

    use_windows_cursor = graphics_cursor_use_windows_asset();
    cursor_width = use_windows_cursor ? g_windows_cursor_width : 16;
    cursor_height = use_windows_cursor ? g_windows_cursor_height : 16;
    if (cursor_width == 0 || cursor_width > GRAPHICS_CURSOR_MAX_WIDTH) {
        cursor_width = 16;
        use_windows_cursor = false;
    }
    if (cursor_height == 0 || cursor_height > GRAPHICS_CURSOR_MAX_HEIGHT) {
        cursor_height = 16;
        use_windows_cursor = false;
    }

    if (FB_WIDTH <= cursor_width) {
        x = 0;
    } else if (x > FB_WIDTH - cursor_width) {
        x = (uint16_t) (FB_WIDTH - cursor_width);
    }
    if (FB_HEIGHT <= cursor_height) {
        y = 0;
    } else if (y > FB_HEIGHT - cursor_height) {
        y = (uint16_t) (FB_HEIGHT - cursor_height);
    }

    graphics_restore_cursor();
    g_cursor_x = x;
    g_cursor_y = y;
    style = &g_cursor_styles[g_cursor_style_index];

    for (uint16_t row = 0; row < cursor_height; row++) {
        for (uint16_t col = 0; col < cursor_width; col++) {
            uint16_t px = x + col;
            uint16_t py = y + row;

            g_cursor_saved[(uint32_t) row * GRAPHICS_CURSOR_MAX_WIDTH + col] =
                g_framebuffer[graphics_framebuffer_index(px, py)];
            if (use_windows_cursor) {
                uint32_t color = g_windows_cursor_pixels[(uint32_t) row * GRAPHICS_CURSOR_MAX_WIDTH + col];

                if ((color & 0xFF000000U) != 0) {
                    g_framebuffer[graphics_framebuffer_index(px, py)] = color & 0x00FFFFFFU;
                }
            } else {
                uint16_t mask = (uint16_t) (0x8000 >> col);

                if ((style->shape[row] & mask) != 0) {
                    g_framebuffer[graphics_framebuffer_index(px, py)] =
                        (style->fill[row] & mask) != 0 ? style->fill_color : style->outline_color;
                }
            }
        }
    }

    g_cursor_draw_width = cursor_width;
    g_cursor_draw_height = cursor_height;
    g_cursor_drawn = true;
    graphics_flush_gpu();
}

void graphics_init(void)
{
    memset(g_windows, 0, sizeof(g_windows));
    memset(g_backbuffer, 0, sizeof(g_backbuffer));
    g_graphics_active = false;
    g_graphics_vmware_backend = false;
    g_graphics_boot_animation_mode = false;
    g_session_logged_in = false;
    g_selected_login_user = 0;
    g_cursor_drawn = false;
    g_framebuffer = NULL;
    g_cursor_x = 24;
    g_cursor_y = 24;
    g_cursor_draw_width = 0;
    g_cursor_draw_height = 0;
    g_windows_cursor_attempted = false;
    g_windows_cursor_loaded = false;
    g_windows_cursor_width = 0;
    g_windows_cursor_height = 0;
    g_windows_cursor_hotspot_x = 0;
    g_windows_cursor_hotspot_y = 0;
    memset(g_windows_cursor_pixels, 0, sizeof(g_windows_cursor_pixels));
    g_start_menu_open = false;
    g_start_menu_view = UI_START_MENU_ROOT;
    g_context_menu_open = false;
    g_power_menu_open = false;
    g_dragging_window = false;
    g_active_button_index = 0;
    g_prev_mouse_buttons = 0;
    g_last_desktop_scan_tick = 0;
    g_framebuffer_pitch_bytes = FB_WIDTH * sizeof(uint32_t);
    g_framebuffer_pitch_pixels = FB_WIDTH;
    g_gpu_present_pending = false;
    g_gpu_submit_count = 0;
    g_gpu_present_count = 0;
    g_power_menu_x = 0;
    g_power_menu_y = 0;
    g_run_input_len = 0;
    g_run_input[0] = '\0';
    g_run_input_focus = false;
    g_file_scroll_offset = 0;
    graphics_set_terminal_focus(false);
    g_sleeping = false;
    g_uac_program[0] = '\0';
    g_uac_reason[0] = '\0';
    g_uac_password[0] = '\0';
    g_uac_password_len = 0;
    g_uac_pending = false;
    g_uac_result_ready = false;
    g_uac_result = false;
    g_uac_error = false;
    g_uac_input_focus = false;
    g_uac_last_buttons = 0;
    g_uac_privilege_level = EXEC_PRIV_R3;
    graphics_reset_login();
    graphics_reset_file_browser();
    g_player_button_pressed = false;
    g_player_browser_open = false;
    strcpy(g_player_current_path, UI_ROOT_DESKTOP);
    g_player_item_count = 0;
    g_player_selected_index = 0;
    g_player_selected_path[0] = '\0';
    g_wallpaper_attempted = false;
    g_wallpaper_loaded = false;
    g_wallpaper_width = 0;
    g_wallpaper_height = 0;
    g_boot_image_attempted = false;
    g_boot_image_loaded = false;
    g_boot_image_width = 0;
    g_boot_image_height = 0;
    memset(g_boot_image_pixels, 0, sizeof(g_boot_image_pixels));
    strcpy(g_player_status, "ready");
}

void graphics_notify_process_output(void)
{
    if (g_graphics_active && !g_installer_mode) {
        graphics_draw_shell();
        graphics_flush_gpu();
    }
}

bool graphics_terminal_has_focus(void)
{
    return g_terminal_input_focus;
}

void graphics_close_all_programs(void)
{
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible && g_windows[i].kind != UI_WINDOW_LOGON && g_windows[i].kind != UI_WINDOW_POWER) {
            graphics_close_window(i);
        }
    }
    g_start_menu_open = false;
    g_context_menu_open = false;
    g_power_menu_open = false;
    g_dragging_window = false;
    log_write("graphics: programs closed");
}

uint32_t graphics_gpu_submit_count(void)
{
    return g_gpu_submit_count;
}

uint32_t graphics_gpu_present_count(void)
{
    return g_gpu_present_count;
}

uint32_t graphics_gpu_pending_count(void)
{
    return g_gpu_present_pending ? 1u : 0u;
}

uint32_t graphics_window_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible) {
            count++;
        }
    }
    return count;
}

uint32_t graphics_focused_window_index(void)
{
    for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
        if (g_windows[i].visible && !g_windows[i].minimized) {
            return (uint32_t) i;
        }
    }
    return 0xFFFFFFFFu;
}

uint32_t graphics_framebuffer_address(void)
{
    return g_graphics_framebuffer_addr;
}

uint32_t graphics_framebuffer_pitch_bytes(void)
{
    return g_framebuffer_pitch_bytes;
}

uint32_t graphics_framebuffer_width(void)
{
    return FB_WIDTH;
}

uint32_t graphics_framebuffer_height(void)
{
    return FB_HEIGHT;
}

const char *graphics_backend_name(void)
{
    if (!g_graphics_active) {
        return "framebuffer-idle";
    }
    return g_graphics_vmware_backend ? "vmware-svga" : "bochs-bga";
}

bool graphics_driver_init(void)
{
    log_write("graphics driver: registered");
    return true;
}

bool graphics_active(void)
{
    return g_graphics_active;
}

void graphics_set_installer_mode(bool enabled)
{
    g_installer_mode = enabled;
}

void graphics_set_boot_animation_mode(bool enabled)
{
    g_graphics_boot_animation_mode = enabled;
}

void graphics_reload_cursor_style(void)
{
    if (g_graphics_active) {
        graphics_load_cursor_style();
    }
}

void graphics_open_task_manager(void)
{
    if (!g_graphics_active) {
        return;
    }
    graphics_open_window(UI_WINDOW_TASKMGR);
    if (graphics_find_window(UI_WINDOW_TASKMGR) >= 0) {
        graphics_draw_shell();
    }
}

bool graphics_open_notepad_window(void)
{
    if (!g_graphics_active) {
        return false;
    }
    graphics_open_window(UI_WINDOW_NOTEPAD);
    if (graphics_find_window(UI_WINDOW_NOTEPAD) >= 0) {
        graphics_draw_shell();
        return true;
    }
    return false;
}

bool graphics_open_cube3d_window(void)
{
    if (!g_graphics_active) {
        return false;
    }
    graphics_open_window(UI_WINDOW_CUBE3D);
    if (graphics_find_window(UI_WINDOW_CUBE3D) >= 0) {
        graphics_draw_shell();
        return true;
    }
    return false;
}

void graphics_leave_mode(void)
{
    if (!g_graphics_active) {
        return;
    }

    if (g_graphics_vmware_backend) {
        svga_write(SVGA_REG_ENABLE, 0);
    } else {
        bga_write(BGA_ENABLE, BGA_DISABLED);
    }

    g_graphics_active = false;
    g_cursor_drawn = false;
    g_start_menu_open = false;
    g_context_menu_open = false;
    g_power_menu_open = false;
    g_dragging_window = false;
    g_run_input_focus = false;
    graphics_set_terminal_focus(false);
}

void graphics_shutdown(void)
{
    if (g_graphics_active) {
        graphics_flush_gpu();
    }
    memset(g_windows, 0, sizeof(g_windows));
    log_write("graphics: shutdown");
}

void graphics_shutdown_animation(void)
{
    if (!g_graphics_active) {
        return;
    }

    /* Black screen with centered shutdown message */
    graphics_fill(0x00000000);
    graphics_present();

    /* Draw "\u6b63\u5728\u5173\u673a" (\u6b63\u5728\u5173\u673a = \u6b63\u5728\u5173\u673a) centered */
    graphics_draw_text_aligned(0, (uint16_t) (FB_HEIGHT / 2 - 9), FB_WIDTH, "\u6b63\u5728\u5173\u673a\u2026", 0x00FFFFFF);
    graphics_present();

    /* Small delay so user sees the message */
    for (volatile uint32_t d = 0; d < 30000000; d++) {}

    /* Fade to black by gradually darkening */
    for (int step = 0; step < 8; step++) {
        uint32_t pixels = (uint32_t) FB_WIDTH * (uint32_t) FB_HEIGHT;

        for (uint32_t i = 0; i < pixels; i++) {
            uint32_t c = g_backbuffer[i];
            uint8_t r = (uint8_t) ((c >> 16) & 0xFF);
            uint8_t g = (uint8_t) ((c >> 8) & 0xFF);
            uint8_t b = (uint8_t) (c & 0xFF);
            r = (uint8_t) (r >> 1);
            g = (uint8_t) (g >> 1);
            b = (uint8_t) (b >> 1);
            g_backbuffer[i] = ((uint32_t) r << 16) | ((uint32_t) g << 8) | b;
        }
        graphics_present();
        for (volatile uint32_t d = 0; d < 5000000; d++) {}
    }

    /* Final black screen */
    graphics_fill(0x00000000);
    graphics_present();
}

void graphics_enter_mode(void)
{
    char msg[40] = "graphics fb: 0x";
    char hex[9];
    graphics_pci_device_t dev;
    uint32_t addr;
    uint64_t framebuffer_map_length;

    if (g_graphics_active) {
        return;
    }

    dev = graphics_find_display_device();
    graphics_log_device_info(&dev);
    addr = graphics_find_framebuffer_address(&dev);
    if (!graphics_framebuffer_address_valid(addr)) {
        log_write("graphics: invalid framebuffer address");
        g_graphics_active = false;
        g_graphics_vmware_backend = false;
        return;
    }
    /* Ensure kernel page-tables map enough framebuffer space for the
     * selected mode before we switch to the kernel CR3.
     */
    framebuffer_map_length = (uint64_t) FB_PIXELS * sizeof(uint32_t) + 0x400000ULL;
    mmu_map_device_identity((uint64_t)addr, framebuffer_map_length);
    /* If kernel page-tables are not yet active (we're still on loader/boot
     * page-tables), avoid performing writes to high MMIO framebuffer
     * addresses which will cause page faults. Defer graphics activation
     * until kernel page-tables are active.
     */
    if (!mmu_is_active()) {
        log_write("graphics: kernel page-tables not active; deferring framebuffer init");
        g_graphics_active = false;
        g_graphics_vmware_backend = false;
        return;
    }
    g_framebuffer = (volatile uint32_t *) (uint64_t) addr;
    g_graphics_framebuffer_addr = addr;

    if (dev.vendor == PCI_VENDOR_VMWARE && dev.device == PCI_DEVICE_VMWARE_SVGA2 && (dev.bar0 & 0x1u) != 0) {
        uint32_t vmware_fb_addr;
        uint32_t vmware_fb_offset;
        uint32_t vmware_pitch_bytes;
        char fb_msg[64] = "graphics: vmw bar1=0x";
        char bar1_hex[9];
        char reg_hex[9];

        g_graphics_vmware_backend = true;
        g_svga_io_base = (uint16_t) (dev.bar0 & 0xFFF0u);
        svga_write(SVGA_REG_ID, SVGA_ID_2);
        svga_write(SVGA_REG_ENABLE, 0);
        svga_write(SVGA_REG_WIDTH, FB_WIDTH);
        svga_write(SVGA_REG_HEIGHT, FB_HEIGHT);
        svga_write(SVGA_REG_BITS_PER_PIXEL, 32);
        svga_write(SVGA_REG_ENABLE, SVGA_ENABLE_ENABLE);
        vmware_fb_addr = svga_read(SVGA_REG_FB_START);
        vmware_fb_offset = svga_read(SVGA_REG_FB_OFFSET);
        vmware_pitch_bytes = svga_read(SVGA_REG_BYTES_PER_LINE);
        graphics_u32_to_hex8(dev.bar1 & 0xFFFFFFF0u, bar1_hex);
        graphics_u32_to_hex8(vmware_fb_addr, reg_hex);
        memcpy(fb_msg + 20, bar1_hex, 8);
        memcpy(fb_msg + 28, " reg=0x", 7);
        memcpy(fb_msg + 35, reg_hex, 8);
        fb_msg[43] = '\0';
        log_write(fb_msg);
        if (graphics_framebuffer_address_valid(vmware_fb_addr)) {
            addr = vmware_fb_addr + vmware_fb_offset;
        } else if (graphics_framebuffer_address_valid(addr)) {
            addr += vmware_fb_offset;
        }
        if (!graphics_framebuffer_address_valid(addr)) {
            log_write("graphics: vmware framebuffer rejected");
            svga_write(SVGA_REG_ENABLE, 0);
            g_graphics_vmware_backend = false;
            return;
        }
        if (vmware_pitch_bytes >= FB_WIDTH * sizeof(uint32_t)) {
            g_framebuffer_pitch_bytes = vmware_pitch_bytes;
            g_framebuffer_pitch_pixels = vmware_pitch_bytes / sizeof(uint32_t);
        } else {
            g_framebuffer_pitch_bytes = FB_WIDTH * sizeof(uint32_t);
            g_framebuffer_pitch_pixels = FB_WIDTH;
        }
        g_framebuffer = (volatile uint32_t *) (uint64_t) addr;
        g_graphics_framebuffer_addr = addr;
        log_write("graphics: using vmware svga ii");
    } else {
        g_graphics_vmware_backend = false;
        /*
         * QEMU's std VGA exposes the Bochs VBE registers, but some firmware
         * paths leave the controller on an old VBE version. Select the
         * highest common BGA version before programming the mode.
         */
        bga_write(BGA_ID, BGA_ID_5);
        bga_write(BGA_ENABLE, BGA_DISABLED);
        bga_write(BGA_XRES, FB_WIDTH);
        bga_write(BGA_YRES, FB_HEIGHT);
        bga_write(BGA_BPP, 32);
        bga_write(BGA_VIRT_WIDTH, FB_WIDTH);
        bga_write(BGA_VIRT_HEIGHT, FB_HEIGHT);
        bga_write(BGA_X_OFFSET, 0);
        bga_write(BGA_Y_OFFSET, 0);
        bga_write(BGA_ENABLE, BGA_ENABLED | BGA_LFB);
        g_graphics_framebuffer_addr = addr;
        g_framebuffer_pitch_bytes = FB_WIDTH * sizeof(uint32_t);
        g_framebuffer_pitch_pixels = FB_WIDTH;
        graphics_log_bga_mode();
        log_write("graphics: using bochs/qemu bga");
    }

    for (uint8_t i = 0; i < 8; i++) {
        uint8_t nibble = (uint8_t) ((addr >> ((7 - i) * 4)) & 0xF);
        hex[i] = (char) (nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
    }
    hex[8] = '\0';
    memcpy(msg + 15, hex, 9);
    log_write(msg);
    graphics_u32_to_hex8(g_graphics_framebuffer_addr, hex);
    memcpy(msg, "graphics fb final: 0x", 21);
    memcpy(msg + 21, hex, 8);
    msg[29] = '\0';
    log_write(msg);

    if (mmu_is_active()) {
        g_graphics_active = true;
        g_cursor_drawn = false;
        if (!g_graphics_fast_mode_switch && !g_graphics_boot_animation_mode) {
            graphics_load_cursor_style();
        }
        if (!g_session_logged_in && !g_installer_mode && !g_graphics_boot_animation_mode) {
            graphics_open_window(UI_WINDOW_LOGON);
        }
        graphics_reflow_windows();
        if (g_graphics_boot_animation_mode) {
            graphics_show_boot_loading_screen();
        } else if (g_installer_mode) {
            graphics_fill(0x00182A3A);
            graphics_present();
        } else {
            graphics_draw_shell();
        }
        /*
         * Boot animation uses fallback glyphs until the filesystem stage
         * explicitly loads the UI font. Do not start font I/O here because
         * graphics_enter_mode() may run before file_auto_mount().
         */
        if (!font_ready() && !g_graphics_boot_animation_mode) {
            font_init_step(128u * 1024u);
        }
        log_write(g_graphics_boot_animation_mode ?
                  "graphics: boot animation ready" :
                  "graphics: desktop drawn");
    } else {
        log_write("graphics: deferring full desktop draw; mmu not active");
        g_graphics_active = false;
    }
}
