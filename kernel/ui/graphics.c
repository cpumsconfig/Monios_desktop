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
#include "session.h"
#include "shell.h"
#include "task.h"
#include "terminal.h"

#define BGA_INDEX_PORT 0x01CE
#define BGA_DATA_PORT  0x01CF
#define BGA_ID         0x0
#define BGA_XRES       0x1
#define BGA_YRES       0x2
#define BGA_BPP        0x3
#define BGA_ENABLE     0x4

#define BGA_DISABLED   0x00
#define BGA_ENABLED    0x01
#define BGA_LFB        0x40

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

#define FB_WIDTH  GRAPHICS_WIDTH
#define FB_HEIGHT GRAPHICS_HEIGHT
#define TASKBAR_HEIGHT 48
#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 30
#define BUTTON_GAP 12
#define BUTTON_Y (FB_HEIGHT - TASKBAR_HEIGHT + 9)
#define DESKTOP_LABEL_MAX 8
#define START_BUTTON_WIDTH 96
#define DESKTOP_ICON_W 74
#define DESKTOP_ICON_H 82
#define DESKTOP_ICON_GAP_Y 14
#define DESKTOP_ICON_GAP_X 18
#define DESKTOP_ICON_START_X 22
#define DESKTOP_ICON_START_Y 22
#define UI_WINDOW_MAX 8
#define MONIOS_VERSION "1.0"
#define UI_MENU_ITEM_H 24
#define UI_DESKTOP_POLL_TICKS 30
#define UI_TITLEBAR_H 24
#define UI_TASK_BUTTON_W 108
#define FB_PIXELS (FB_WIDTH * FB_HEIGHT)
#define GRAPHICS_LOGIN_USERNAME_MAX 16
#define GRAPHICS_LOGIN_PASSWORD_MAX 64
#define GRAPHICS_FILE_NAME_MAX 16
#define GRAPHICS_FILE_PATH_MAX 64
#define GRAPHICS_CLIPBOARD_PATH_MAX 256
#define GRAPHICS_NOTEPAD_TEXT_MAX 2048
#define GRAPHICS_UAC_TEXT_MAX 96

typedef struct {
    const char *label;
    uint32_t color;
} graphics_button_t;

typedef enum {
    UI_WINDOW_NONE = 0,
    UI_WINDOW_LOGON,
    UI_WINDOW_FILES,
    UI_WINDOW_TERMINAL,
    UI_WINDOW_RUN,
    UI_WINDOW_SHELL,
    UI_WINDOW_ABOUT,
    UI_WINDOW_PLAYER,
    UI_WINDOW_NOTEPAD,
    UI_WINDOW_TASKMGR,
    UI_WINDOW_CUBE3D,
    UI_WINDOW_CONTEXT,
    UI_WINDOW_POWER,
    UI_WINDOW_UAC
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

typedef struct {
    bool visible;
    ui_window_kind_t kind;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    char title[24];
} ui_window_t;

typedef struct {
    char name[GRAPHICS_FILE_NAME_MAX];
    bool is_dir;
} graphics_file_item_t;

static const graphics_button_t g_taskbar_buttons[] = {
    { "\u6587\u4ef6", 0x002F7FD3 },
    { "\u7ec8\u7aef", 0x004A8A44 },
    { "\u5173\u4e8e", 0x00C06C2B }
};

static char g_desktop_entries[DESKTOP_LABEL_MAX][16];
static uint32_t g_desktop_entry_count;
static uint32_t g_active_button_index;
static bool g_start_menu_open;
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
static graphics_file_item_t g_file_items[DESKTOP_LABEL_MAX];
static uint32_t g_file_item_count;
static uint32_t g_file_selected_index;
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

static const uint16_t g_cursor_shape[16] = {
    0x8000, 0xC000, 0xE000, 0xF000,
    0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFFE0, 0xFC00, 0xCC00, 0x8C00,
    0x0600, 0x0600, 0x0300, 0x0000
};

static const uint16_t g_cursor_fill[16] = {
    0x0000, 0x4000, 0x6000, 0x7000,
    0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7FE0, 0x6C00, 0x4C00, 0x0C00,
    0x0400, 0x0000, 0x0000, 0x0000
};

static bool g_graphics_active;
static bool g_graphics_vmware_backend;
static uint32_t g_graphics_framebuffer_addr;
static bool g_session_logged_in;
static bool g_double_buffer_enabled = true;
static bool g_vsync_enabled = false;
static uint32_t g_fps_value = 0;
static uint64_t g_last_fps_tick = 0;
static uint32_t g_present_snapshot = 0;
static uint32_t g_selected_login_user;
static uint32_t g_cursor_saved[16 * 16];
static uint16_t g_cursor_x;
static uint16_t g_cursor_y;
static bool g_cursor_drawn;
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
static void graphics_open_path(const char *path);
static void graphics_run_command_text(const char *command);
static int32_t graphics_find_window(ui_window_kind_t kind);
static void graphics_bring_window_to_front(uint32_t index);
static bool graphics_path_has_suffix(const char *path, const char *suffix);
static void graphics_draw_text(uint16_t x, uint16_t y, const char *text, uint32_t color);
static void graphics_draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color);
static void graphics_draw_cube3d_window(const ui_window_t *window);
static void graphics_draw_taskbar_status(void);
static void graphics_append_path_component(char *path, uint32_t path_size, const char *component);
static void graphics_fill_player_browser(void);
static void graphics_player_open_browser(void);
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

static bool graphics_is_separator(char ch)
{
    return ch == '/' || ch == '\\';
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
    strcpy(g_file_current_path, "/");
    g_file_item_count = 0;
    g_file_selected_index = 0;
}

static void graphics_current_desktop_path(char output[GRAPHICS_FILE_PATH_MAX])
{
    const session_user_t *user = session_current_user();

    strcpy(output, "/home/root/desktop");
    if (user != NULL && user->home[0] != '\0') {
        strcpy(output, user->home);
        graphics_append_path_component(output, GRAPHICS_FILE_PATH_MAX, "desktop");
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

static void graphics_append_path_component(char *path, uint32_t path_size, const char *component)
{
    uint32_t len = (uint32_t) strlen(path);

    if (len > 1 && len + 1 < path_size) {
        path[len++] = '/';
        path[len] = '\0';
    }
    if (len + strlen(component) < path_size) {
        strcpy(path + len, component);
    }
}

static bool graphics_pop_path_component(char *path)
{
    uint32_t len;

    if (path == NULL || path[0] == '\0' || strcmp(path, "/") == 0) {
        return false;
    }
    len = (uint32_t) strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    while (len > 0 && path[len - 1] != '/') {
        len--;
    }
    if (len == 0) {
        path[0] = '/';
        path[1] = '\0';
        return true;
    }
    path[len] = '\0';
    if (path[0] == '\0') {
        path[0] = '/';
        path[1] = '\0';
    }
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
    char buffer[512];

    g_file_item_count = 0;
    if (!file_list_dir(g_file_current_path, buffer, sizeof(buffer))) {
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
        strcpy(g_player_current_path, "/");
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
        return false;
    }
    g_login_error = false;
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
        strncpy(g_uac_program, program_path, sizeof(g_uac_program) - 1);
    }
    if (reason != NULL) {
        strncpy(g_uac_reason, reason, sizeof(g_uac_reason) - 1);
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
    uint32_t path_len;

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
    path_len = (uint32_t) strlen(path);
    if ((path_len >= 4 && strcasecmp(path + path_len - 4, ".elf") == 0) ||
        (path_len >= 4 && strcasecmp(path + path_len - 4, ".exe") == 0) ||
        (path_len >= 4 && strcasecmp(path + path_len - 4, ".rzs") == 0)) {
        graphics_open_path(path);
    }
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
    graphics_append_path_component(out_path, GRAPHICS_CLIPBOARD_PATH_MAX, g_desktop_entries[index]);
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
        if (*path == '/') {
            last = path + 1;
        }
        path++;
    }
    return last;
}

static void graphics_context_open(uint16_t x, uint16_t y, ui_context_menu_mode_t mode)
{
    g_start_menu_open = false;
    g_context_menu_open = true;
    g_context_menu_mode = mode;
    g_context_menu_x = x > FB_WIDTH - 170 ? (uint16_t) (FB_WIDTH - 170) : x;
    g_context_menu_y = y > FB_HEIGHT - TASKBAR_HEIGHT - 90 ? (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 90) : y;
}

static void graphics_open_path(const char *path)
{
    uint32_t image_flags;

    if (path == NULL || path[0] == '\0') {
        return;
    }
    image_flags = exec_image_flags_for_path(path);
    if (graphics_path_has_suffix(path, ".elf") || graphics_path_has_suffix(path, ".ELF") ||
        graphics_path_has_suffix(path, ".exe") || graphics_path_has_suffix(path, ".EXE") ||
        graphics_path_has_suffix(path, ".rzs") || graphics_path_has_suffix(path, ".RZS")) {
        if (strcasecmp(graphics_path_basename(path), "player.elf") == 0) {
            graphics_open_window(UI_WINDOW_PLAYER);
        } else if (strcasecmp(graphics_path_basename(path), "notepad.elf") == 0) {
            graphics_open_window(UI_WINDOW_NOTEPAD);
        } else if (strcasecmp(graphics_path_basename(path), "taskmgr.elf") == 0) {
            graphics_open_window(UI_WINDOW_TASKMGR);
        } else if (strcasecmp(graphics_path_basename(path), "cube3d.elf") == 0) {
            graphics_open_cube3d_window();
        } else if ((image_flags & EXEC_IMAGE_FLAG_GUI) != 0) {
            shell_exec_path(path);
        } else {
            if (graphics_find_window(UI_WINDOW_TERMINAL) < 0) {
                graphics_open_window(UI_WINDOW_TERMINAL);
            }
            shell_exec_path(path);
        }
    }
}

static void graphics_run_command_text(const char *command)
{
    if (command == NULL || command[0] == '\0') {
        return;
    }
    if (strcasecmp(command, "command") == 0 || strcasecmp(command, "cmd") == 0 || strcasecmp(command, "terminal") == 0) {
        graphics_open_window(UI_WINDOW_TERMINAL);
    } else if (strcasecmp(command, "explorar") == 0 || strcasecmp(command, "explorar.exe") == 0) {
        graphics_open_window(UI_WINDOW_FILES);
    } else if (strcasecmp(command, "player") == 0 || strcasecmp(command, "player.elf") == 0) {
        graphics_open_window(UI_WINDOW_PLAYER);
    } else if (strcasecmp(command, "notepad") == 0 || strcasecmp(command, "notepad.elf") == 0) {
        graphics_open_window(UI_WINDOW_NOTEPAD);
    } else if (strcasecmp(command, "taskmgr") == 0 || strcasecmp(command, "taskmgr.elf") == 0) {
        graphics_open_window(UI_WINDOW_TASKMGR);
    } else if (strcasecmp(command, "cube3d") == 0 || strcasecmp(command, "cube3d.elf") == 0) {
        graphics_open_cube3d_window();
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
    while (text != NULL && *text != '\0') {
        uint32_t codepoint = font_utf8_next(&text);
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

static void graphics_window_set(ui_window_t *window, ui_window_kind_t kind, uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *title)
{
    window->visible = true;
    window->kind = kind;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    memset(window->title, 0, sizeof(window->title));
    strcpy(window->title, title);
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

static void graphics_open_window(ui_window_kind_t kind)
{
    ui_window_t *window = NULL;
    uint32_t window_index = 0;

    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (g_windows[i].visible && g_windows[i].kind == kind) {
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
        graphics_bring_window_to_front(window_index);
        graphics_draw_shell();
        return;
    }

    switch (kind) {
    case UI_WINDOW_LOGON:
        graphics_window_set(window, kind, 320, 150, 380, 260, "\u767b\u5f55");
        graphics_reset_login();
        break;
    case UI_WINDOW_FILES:
        graphics_window_set(window, kind, 260, 120, 360, 250, "\u6587\u4ef6");
        graphics_fill_file_browser();
        break;
    case UI_WINDOW_TERMINAL:
        graphics_window_set(window, kind, 300, 150, 420, 260, "\u7ec8\u7aef");
        graphics_set_terminal_focus(true);
        break;
    case UI_WINDOW_RUN:
        graphics_window_set(window, kind, 350, 240, 320, 120, "\u8fd0\u884c");
        g_run_input_len = 0;
        g_run_input[0] = '\0';
        g_run_input_focus = true;
        break;
    case UI_WINDOW_SHELL:
        graphics_window_set(window, kind, 280, 120, 460, 280, "\u547d\u4ee4");
        break;
    case UI_WINDOW_ABOUT:
        graphics_window_set(window, kind, 340, 180, 300, 180, "\u5173\u4e8e");
        break;
    case UI_WINDOW_PLAYER:
        graphics_window_set(window, kind, 330, 140, 360, 320, "\u64ad\u653e\u5668");
        if (g_player_current_path[0] == '\0') {
            strcpy(g_player_current_path, "/");
        }
        if (g_player_status[0] == '\0') {
            strcpy(g_player_status, "ready");
        }
        break;
    case UI_WINDOW_NOTEPAD:
        graphics_window_set(window, kind, 220, 90, 520, 340, "\u8bb0\u4e8b\u672c");
        g_notepad_focus = true;
        if (g_notepad_path[0] == '\0') {
            strcpy(g_notepad_path, "/home/root/desktop/note.txt");
            g_notepad_text[0] = '\0';
            g_notepad_len = 0;
        }
        break;
    case UI_WINDOW_TASKMGR:
        graphics_window_set(window, kind, 300, 130, 380, 260, "\u4efb\u52a1\u7ba1\u7406\u5668");
        break;
    case UI_WINDOW_CUBE3D:
        graphics_window_set(window, kind, 280, 110, 480, 360, "Cube3D");
        g_cube3d_open = true;
        if (g_cube3d_last_tick == 0) {
            g_cube3d_angle_x = 0.45f;
            g_cube3d_angle_y = 0.15f;
            g_cube3d_angle_z = 0.0f;
        }
        break;
    case UI_WINDOW_POWER:
        graphics_window_set(window, kind, 380, 210, 220, 140, "\u7535\u6e90");
        break;
    case UI_WINDOW_UAC:
        graphics_window_set(window, kind, 312, 206, 400, 210, "UAC");
        g_uac_input_focus = true;
        break;
    default:
        break;
    }
    graphics_bring_window_to_front(window_index);
}

static void graphics_close_window(uint32_t index)
{
    ui_window_kind_t kind;

    if (index >= UI_WINDOW_MAX) {
        return;
    }
    if (!g_windows[index].visible) {
        return;
    }
    kind = g_windows[index].kind;
    g_windows[index].visible = false;
    if (g_dragging_window && g_drag_window_index == index) {
        g_dragging_window = false;
    }
    if (kind == UI_WINDOW_RUN) {
        g_run_input_focus = false;
        g_run_input_len = 0;
        g_run_input[0] = '\0';
    }
    if (kind == UI_WINDOW_TERMINAL) {
        graphics_set_terminal_focus(false);
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
    for (uint32_t i = 0; i < FB_PIXELS; i++) {
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

static void graphics_draw_boot_frame(uint32_t progress, bool show_text)
{
    const uint16_t bar_w = 264;
    const uint16_t bar_h = 16;
    const uint16_t bar_x = (FB_WIDTH - bar_w) / 2;
    const uint16_t bar_y = FB_HEIGHT - 94;
    const uint16_t block_w = 42;
    const uint16_t block_gap = 16;
    uint32_t phase;

    (void) show_text;
    if (progress > 100) {
        progress = 100;
    }

    graphics_fill(0x00000000);
    graphics_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x00111111);
    graphics_draw_rect_outline(bar_x, bar_y, bar_w, bar_h, 0x00686868);
    graphics_fill_rect((uint16_t) (bar_x + 1), (uint16_t) (bar_y + 1), (uint16_t) (bar_w - 2), 1, 0x00FFFFFF);
    graphics_fill_rect((uint16_t) (bar_x + 1), (uint16_t) (bar_y + bar_h - 2), (uint16_t) (bar_w - 2), 1, 0x00303030);

    phase = (progress * 5u) % (bar_w + block_w + block_gap);
    for (uint16_t i = 0; i < 3; i++) {
        int32_t left = (int32_t) bar_x + 2 + (int32_t) ((phase + i * (block_w + block_gap)) % (bar_w + block_w + block_gap)) - block_w;
        int32_t right = left + block_w;
        uint16_t draw_x;
        uint16_t draw_w;

        if (right <= (int32_t) bar_x + 2 || left >= (int32_t) bar_x + bar_w - 2) {
            continue;
        }
        if (left < (int32_t) bar_x + 2) {
            left = (int32_t) bar_x + 2;
        }
        if (right > (int32_t) bar_x + bar_w - 2) {
            right = (int32_t) bar_x + bar_w - 2;
        }
        draw_x = (uint16_t) left;
        draw_w = (uint16_t) (right - left);
        graphics_fill_rect(draw_x, (uint16_t) (bar_y + 3), draw_w, 10, 0x001B4FBA);
        if (draw_w > 4) {
            graphics_fill_rect(draw_x, (uint16_t) (bar_y + 3), draw_w, 2, 0x004FA3FF);
            graphics_fill_rect(draw_x, (uint16_t) (bar_y + 11), draw_w, 2, 0x000E2F80);
        }
    }
}

static void graphics_show_boot_loading_screen(void)
{
    graphics_draw_boot_frame(8, false);
    graphics_present();
}

static void graphics_play_boot_animation(void)
{
    uint32_t hz = timer_hz();
    uint64_t duration = hz == 0 ? 84u : (uint64_t) ((hz * 7u) / 6u);
    uint64_t frame_ticks = hz == 0 ? 3u : (uint64_t) (hz / 36u == 0 ? 1u : hz / 36u);
    uint64_t start = timer_ticks();
    uint64_t next_tick = start;

    if (duration == 0) {
        duration = 84;
    }

    while (1) {
        uint64_t now = timer_ticks();
        uint64_t elapsed = now - start;
        uint32_t progress = (uint32_t) (8u + (elapsed >= duration ? 92u : (elapsed * 92u) / duration));

        graphics_draw_boot_frame(progress, true);
        graphics_present();
        if (elapsed >= duration) {
            break;
        }
        next_tick += frame_ticks;
        while (timer_ticks() < next_tick) {
        }
    }
    graphics_wait_ticks(hz == 0 ? 10u : hz / 10u);
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

static void graphics_draw_desktop(void)
{
    graphics_fill(0x001A3A6E);
    for (uint16_t y = FB_HEIGHT - TASKBAR_HEIGHT; y < FB_HEIGHT; y++) {
        for (uint16_t x = 0; x < FB_WIDTH; x++) {
            graphics_plot(x, y, 0x004A4A4A);
        }
    }
    for (uint16_t x = 0; x < FB_WIDTH; x++) {
        graphics_plot(x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), 0x008AA1B8);
    }
}

static void graphics_load_desktop_entries(void)
{
    char buffer[256];
    char desktop_path[GRAPHICS_FILE_PATH_MAX];
    uint32_t start = 0;
    uint32_t index = 0;

    g_desktop_entry_count = 0;
    graphics_current_desktop_path(desktop_path);
    if (!file_exists(desktop_path)) {
        file_mkdir(desktop_path);
    }
    if (!file_list_dir(desktop_path, buffer, sizeof(buffer))) {
        return;
    }

    for (uint32_t i = 0; buffer[i] != '\0' && index < DESKTOP_LABEL_MAX; i++) {
        uint32_t length = 0;
        if (buffer[i] != '\n') {
            continue;
        }
        while (start + length < i && length < sizeof(g_desktop_entries[index]) - 1) {
            g_desktop_entries[index][length] = buffer[start + length];
            length++;
        }
        g_desktop_entries[index][length] = '\0';
        if (length > 0) {
            g_desktop_entry_count++;
            index++;
        }
        start = i + 1;
    }
}

static void graphics_draw_desktop_icons(void)
{
    if (!g_session_logged_in) {
        return;
    }
    for (uint32_t i = 0; i < g_desktop_entry_count; i++) {
        uint16_t x = (uint16_t) (DESKTOP_ICON_START_X + (i % 2) * (DESKTOP_ICON_W + DESKTOP_ICON_GAP_X));
        uint16_t y = (uint16_t) (DESKTOP_ICON_START_Y + (i / 2) * (DESKTOP_ICON_H + DESKTOP_ICON_GAP_Y));

        graphics_fill_rect(x + 17, y, 36, 36, 0x00D5E5F6);
        graphics_draw_rect_outline(x + 17, y, 36, 36, 0x00203B5A);
        graphics_fill_rect(x + 24, y + 8, 22, 16, 0x0098BADB);
        graphics_draw_text(x + 4, (uint16_t) (y + 50), g_desktop_entries[i], 0x00F7F7F7);
    }
}

static void graphics_draw_taskbar(void)
{
    if (!g_session_logged_in) {
        return;
    }
    uint16_t task_x;
    uint32_t start_fill = g_start_menu_open ? 0x00E2C35A : 0x00256EC8;
    uint32_t start_border = g_start_menu_open ? 0x00FFF4BE : 0x00CDE4FF;

    graphics_fill_rect(12, BUTTON_Y, START_BUTTON_WIDTH, BUTTON_HEIGHT, start_fill);
    graphics_draw_rect_outline(12, BUTTON_Y, START_BUTTON_WIDTH, BUTTON_HEIGHT, start_border);
    graphics_draw_text(36, (uint16_t) (BUTTON_Y + 11), "开始", 0x000C1622);

    task_x = (uint16_t) (28 + START_BUTTON_WIDTH);
    for (uint32_t i = 0; i < sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]); i++) {
        uint16_t x = (uint16_t) (task_x + i * (BUTTON_WIDTH + BUTTON_GAP));
        uint32_t fill = i == g_active_button_index ? 0x00F0D36A : g_taskbar_buttons[i].color;
        uint32_t border = i == g_active_button_index ? 0x00FFF7D1 : 0x00D8E6F2;

        graphics_fill_rect(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, fill);
        graphics_draw_rect_outline(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, border);
        graphics_draw_text((uint16_t) (x + 14), (uint16_t) (BUTTON_Y + 11), g_taskbar_buttons[i].label, 0x000F1620);
    }

    task_x = (uint16_t) (task_x + sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]) * (BUTTON_WIDTH + BUTTON_GAP) + 12);
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible || g_windows[i].kind == UI_WINDOW_LOGON || g_windows[i].kind == UI_WINDOW_POWER) {
            continue;
        }
        if (task_x + UI_TASK_BUTTON_W >= FB_WIDTH - 184) {
            break;
        }
        graphics_fill_rect(task_x, BUTTON_Y, UI_TASK_BUTTON_W, BUTTON_HEIGHT, 0x00394A5E);
        graphics_draw_rect_outline(task_x, BUTTON_Y, UI_TASK_BUTTON_W, BUTTON_HEIGHT, 0x00AFC9E4);
        graphics_draw_text((uint16_t) (task_x + 10), (uint16_t) (BUTTON_Y + 11), g_windows[i].title, 0x00F3F7FB);
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
    task_x = (uint16_t) (28 + START_BUTTON_WIDTH + sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]) * (BUTTON_WIDTH + BUTTON_GAP) + 12);
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

    cmos_read_time(&now);
    graphics_fill_rect(panel_x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 5), 162, 38, 0x00303B48);
    graphics_draw_rect_outline(panel_x, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 5), 162, 38, 0x006C7F92);

    graphics_draw_text((uint16_t) (panel_x + 10), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 12), net_connected() ? "OK" : "/", net_connected() ? 0x0084F0A2 : 0x00F0D36A);

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

    graphics_draw_text((uint16_t) (panel_x + 48), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 9), time_text, 0x00F3F7FB);
    graphics_draw_text((uint16_t) (panel_x + 48), (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 25), date_text, 0x00D7E1EA);
}

static void graphics_draw_start_menu(void)
{
    if (!g_session_logged_in || !g_start_menu_open) {
        return;
    }

    graphics_fill_rect(12, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 188), 220, 176, 0x0014202C);
    graphics_draw_rect_outline(12, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 188), 220, 176, 0x009BC2F3);
    graphics_draw_text(28, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 168), "MONIOS", 0x00FFFFFF);
    graphics_draw_text(28, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 138), "\u6587\u4ef6", 0x00CDE4FF);
    graphics_draw_text(28, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 112), "\u7ec8\u7aef", 0x00CDE4FF);
    graphics_draw_text(28, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 86), "\u8fd0\u884c", 0x00CDE4FF);
    graphics_draw_text(28, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 60), "\u7535\u6e90", 0x00CDE4FF);
}

static void graphics_draw_context_menu(void)
{
    if (!g_session_logged_in || !g_context_menu_open) {
        return;
    }

    graphics_fill_rect(g_context_menu_x, g_context_menu_y, 170, 104, 0x0018222E);
    graphics_draw_rect_outline(g_context_menu_x, g_context_menu_y, 170, 104, 0x009BC2F3);
    if (g_context_menu_mode == UI_CONTEXT_MENU_FILES) {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u590d\u5236", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u526a\u5207", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), g_clipboard_mode != UI_CLIPBOARD_NONE ? "\u7c98\u8d34" : "-", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u6253\u5f00", 0x00FFFFFF);
    } else {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u5237\u65b0", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u8fd0\u884c", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), "\u7535\u6e90", 0x00FFFFFF);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u6ce8\u9500", 0x00FFFFFF);
    }
}

static void graphics_draw_power_menu(void)
{
    if (!g_power_menu_open) {
        return;
    }

    graphics_fill_rect(g_power_menu_x, g_power_menu_y, 170, 104, 0x0018222E);
    graphics_draw_rect_outline(g_power_menu_x, g_power_menu_y, 170, 104, 0x009BC2F3);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 14), "\u5173\u673a", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 38), "\u91cd\u542f", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 62), "\u4f11\u7720", 0x00FFFFFF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 86), "\u6ce8\u9500", 0x00FFFFFF);
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
        graphics_fill_rect((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x00DCE8F4);
        graphics_draw_rect_outline((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 18), (uint16_t) (window->y + 37), g_file_current_path, 0x0017232E);
        graphics_fill_rect((uint16_t) (window->x + 12), (uint16_t) (window->y + 66), 18, 18, 0x00D8E9FA);
        graphics_draw_rect_outline((uint16_t) (window->x + 12), (uint16_t) (window->y + 66), 18, 18, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 17), (uint16_t) (window->y + 69), "<", 0x0017232E);
        for (uint32_t i = 0; i < g_file_item_count; i++) {
            uint16_t row_y = (uint16_t) (window->y + 66 + i * 22);
            uint32_t fill = i == g_file_selected_index ? 0x00D8E9FA : 0x00F7FBFE;
            graphics_fill_rect((uint16_t) (window->x + 36), row_y, (uint16_t) (window->width - 48), 20, fill);
            graphics_draw_rect_outline((uint16_t) (window->x + 36), row_y, (uint16_t) (window->width - 48), 20, 0x00A7BBD1);
            graphics_draw_text((uint16_t) (window->x + 46), (uint16_t) (row_y + 5), g_file_items[i].name, 0x0017232E);
            if (g_file_items[i].is_dir) {
                graphics_draw_text_aligned((uint16_t) (window->x + window->width - 64), (uint16_t) (row_y + 5), 40, "\u6587\u4ef6\u5939", 0x002A6CC8);
            }
        }
        return;
    }
    if (window->kind == UI_WINDOW_TERMINAL) {
        const uint16_t *buffer = console_buffer();
        uint16_t start_row = 0;
        uint16_t cursor_row = console_cursor_row();
        graphics_fill_rect((uint16_t) (window->x + 12), (uint16_t) (window->y + 34), (uint16_t) (window->width - 24), (uint16_t) (window->height - 46), 0x00161D27);
        if (cursor_row > 12) {
            start_row = (uint16_t) (cursor_row - 12);
        }
        for (uint16_t row = 0; row < 13; row++) {
            for (uint16_t col = 0; col < 38; col++) {
                uint16_t cell = buffer[(start_row + row) * 80 + col];
                char ch = (char) (cell & 0xFF);
                if (ch == ' ') {
                    continue;
                }
                graphics_draw_char((uint16_t) (window->x + 16 + col * 9), (uint16_t) (window->y + 40 + row * 14), ch, 0x00E8EEF5);
            }
        }
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
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 64), "使用文本模式终端", 0x0017232E);
        return;
    }
    if (window->kind == UI_WINDOW_ABOUT) {
        char version_content[2048];
        char line[256];
        uint32_t line_num = 0;
        uint32_t pos = 0;
        
        if (file_read("/version.txt", version_content, sizeof(version_content)) > 0) {
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
                graphics_fill_rect(row_x, row_y, row_w, 20, row_fill);
                graphics_draw_rect_outline(row_x, row_y, row_w, 20, 0x00A7BBD1);
                graphics_draw_text((uint16_t) (row_x + 8), (uint16_t) (row_y + 5), g_player_items[i].name, 0x0017232E);
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
        for (uint32_t i = 0, row = 0, col = 0; g_notepad_text[i] != '\0' && row < 16; i++) {
            if (g_notepad_text[i] == '\n') {
                row++;
                col = 0;
                continue;
            }
            graphics_draw_char((uint16_t) (window->x + 14 + col * 9), (uint16_t) (window->y + 54 + row * 16), g_notepad_text[i], 0x0017232E);
            col++;
            if (col >= 52) {
                row++;
                col = 0;
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
            graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 90), net_status(), 0x0017232E);
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
    if (window->kind == UI_WINDOW_POWER) {
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 42), "关机", 0x0017232E);
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 68), "重启", 0x0017232E);
    }
}

static void graphics_draw_window(const ui_window_t *window)
{
    if (!window->visible) {
        return;
    }

    graphics_fill_rect(window->x, window->y, window->width, window->height, 0x00E8EEF5);
    graphics_draw_rect_outline(window->x, window->y, window->width, window->height, 0x001E2A36);
    graphics_fill_rect(window->x, window->y, window->width, UI_TITLEBAR_H, 0x002A6CC8);
    graphics_draw_text((uint16_t) (window->x + 12), (uint16_t) (window->y + 3), window->title, 0x00FFFFFF);
    graphics_fill_rect((uint16_t) (window->x + window->width - 29), (uint16_t) (window->y + 3), 16, 14, 0x00D96464);
    graphics_draw_text((uint16_t) (window->x + window->width - 27), (uint16_t) (window->y + 1), "X", 0x00FFFFFF);
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

    graphics_draw_desktop();
    graphics_load_desktop_entries();
    graphics_draw_desktop_icons();
    graphics_draw_taskbar();
    graphics_draw_start_menu();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        graphics_draw_window(&g_windows[i]);
    }
    graphics_draw_context_menu();
    graphics_draw_power_menu();
    graphics_draw_power_overlay();
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

void graphics_activate_primary_button(void)
{
    g_start_menu_open = !g_start_menu_open;
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
            g_sleeping = true;
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
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 16), (uint16_t) (window->y + 56), (uint16_t) (window->width - 32), 24)) {
                g_login_field = 0;
                g_login_error = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 16), (uint16_t) (window->y + 110), (uint16_t) (window->width - 32), 24)) {
                g_login_field = 1;
                g_login_error = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 16), (uint16_t) (window->y + window->height - 42), 92, 22)) {
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
        if (x >= 12 && x < 12 + START_BUTTON_WIDTH) {
            g_context_menu_open = false;
            g_start_menu_open = !g_start_menu_open;
            graphics_draw_shell();
            return;
        }

        for (uint32_t i = 0; i < sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]); i++) {
            uint16_t button_x = (uint16_t) (28 + START_BUTTON_WIDTH + i * (BUTTON_WIDTH + BUTTON_GAP));
                if (x >= button_x && x < button_x + BUTTON_WIDTH) {
                    g_active_button_index = i;
                    g_start_menu_open = false;
                    g_context_menu_open = false;
                    if (i == 0) graphics_open_window(UI_WINDOW_FILES);
                    if (i == 1) graphics_open_window(UI_WINDOW_TERMINAL);
                if (i == 2) graphics_open_window(UI_WINDOW_ABOUT);
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
        uint16_t menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 188);
        if (graphics_point_in_rect(x, y, 24, (uint16_t) (menu_y + 126), 150, 22)) {
            g_start_menu_open = false;
            graphics_open_window(UI_WINDOW_FILES);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, 24, (uint16_t) (menu_y + 152), 150, 22)) {
            g_start_menu_open = false;
            graphics_open_window(UI_WINDOW_TERMINAL);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, 24, (uint16_t) (menu_y + 178), 150, 22)) {
            g_start_menu_open = false;
            graphics_open_window(UI_WINDOW_RUN);
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, 24, (uint16_t) (menu_y + 204), 150, 22)) {
            g_power_menu_x = 232;
            g_power_menu_y = (uint16_t) (menu_y + 190);
            g_power_menu_open = true;
            graphics_draw_shell();
            return;
        }
    }

    for (uint32_t desktop_index = 0; desktop_index < g_desktop_entry_count; desktop_index++) {
        uint16_t icon_x = (uint16_t) (DESKTOP_ICON_START_X + (desktop_index % 2) * (DESKTOP_ICON_W + DESKTOP_ICON_GAP_X));
        uint16_t icon_y = (uint16_t) (DESKTOP_ICON_START_Y + (desktop_index / 2) * (DESKTOP_ICON_H + DESKTOP_ICON_GAP_Y));
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
                g_power_menu_x = (uint16_t) (g_context_menu_x + 170);
                g_power_menu_y = (uint16_t) (g_context_menu_y + 48);
                g_power_menu_open = true;
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
        if (graphics_point_in_rect(x, y, (uint16_t) (window->x + window->width - 29), (uint16_t) (window->y + 3), 18, 16)) {
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
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 12), (uint16_t) (window->y + 66), 18, 18)) {
                graphics_file_browser_go_up();
                graphics_draw_shell();
                return;
            }
            for (uint32_t entry_index = 0; entry_index < g_file_item_count; entry_index++) {
                uint16_t row_y = (uint16_t) (window->y + 66 + entry_index * 22);
                if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), row_y, (uint16_t) (window->width - 48), 20)) {
                    bool same_item = g_file_selected_index == entry_index;
                    g_file_selected_index = entry_index;
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
                        graphics_player_try_play("/music.wav");
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
        if (window->kind == UI_WINDOW_TERMINAL) {
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
            for (uint32_t entry_index = 0; entry_index < g_file_item_count; entry_index++) {
                uint16_t row_y = (uint16_t) (window->y + 66 + entry_index * 22);
                if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), row_y, (uint16_t) (window->width - 48), 20)) {
                    g_file_selected_index = entry_index;
                    g_context_menu_mode = UI_CONTEXT_MENU_FILES;
                    break;
                }
            }
            break;
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
        uint16_t menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 188);
        bool over_power = graphics_point_in_rect(x, y, 24, (uint16_t) (menu_y + 204), 110, 18);
        if (over_power) {
            g_power_menu_x = 232;
            g_power_menu_y = (uint16_t) (menu_y + 190);
            g_power_menu_open = true;
        } else if (!g_context_menu_open) {
            g_power_menu_open = false;
        }
    }
    if (g_context_menu_open) {
        bool over_power = graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 56), 90, 18);
        if (over_power) {
            g_power_menu_x = (uint16_t) (g_context_menu_x + 170);
            g_power_menu_y = (uint16_t) (g_context_menu_y + 48);
            g_power_menu_open = true;
        }
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

    if (g_sleeping) {
        g_sleeping = false;
        graphics_draw_shell();
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

    for (uint16_t row = 0; row < 16; row++) {
        for (uint16_t col = 0; col < 16; col++) {
            uint16_t px = g_cursor_x + col;
            uint16_t py = g_cursor_y + row;
            if (px < FB_WIDTH && py < FB_HEIGHT) {
                g_framebuffer[graphics_framebuffer_index(px, py)] = g_cursor_saved[row * 16 + col];
            }
        }
    }
    g_cursor_drawn = false;
}

void graphics_mouse_redraw(uint16_t x, uint16_t y)
{
    if (!g_graphics_active) {
        return;
    }

    if (x > FB_WIDTH - 16) x = FB_WIDTH - 16;
    if (y > FB_HEIGHT - 16) y = FB_HEIGHT - 16;

    graphics_restore_cursor();
    g_cursor_x = x;
    g_cursor_y = y;

    for (uint16_t row = 0; row < 16; row++) {
        for (uint16_t col = 0; col < 16; col++) {
            uint16_t px = x + col;
            uint16_t py = y + row;
            uint16_t mask = (uint16_t) (0x8000 >> col);

            g_cursor_saved[row * 16 + col] = g_framebuffer[graphics_framebuffer_index(px, py)];
            if ((g_cursor_shape[row] & mask) != 0) {
                g_framebuffer[graphics_framebuffer_index(px, py)] = (g_cursor_fill[row] & mask) != 0 ? 0x00FFFFFF : 0x00000000;
            }
        }
    }

    g_cursor_drawn = true;
    graphics_flush_gpu();
}

void graphics_init(void)
{
    memset(g_windows, 0, sizeof(g_windows));
    memset(g_backbuffer, 0, sizeof(g_backbuffer));
    g_graphics_active = false;
    g_graphics_vmware_backend = false;
    g_session_logged_in = false;
    g_selected_login_user = 0;
    g_cursor_drawn = false;
    g_framebuffer = NULL;
    g_cursor_x = 24;
    g_cursor_y = 24;
    g_start_menu_open = false;
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
    strcpy(g_player_current_path, "/");
    g_player_item_count = 0;
    g_player_selected_index = 0;
    g_player_selected_path[0] = '\0';
    strcpy(g_player_status, "ready");
}

void graphics_notify_process_output(void)
{
    if (g_graphics_active) {
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
        if (g_windows[i].visible) {
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

void graphics_open_task_manager(void)
{
    if (!g_graphics_active) {
        shell_exec_path("/taskmgr.elf");
        return;
    }
    if (g_session_logged_in) {
        graphics_open_window(UI_WINDOW_TASKMGR);
        graphics_draw_shell();
    }
}

void graphics_open_cube3d_window(void)
{
    if (!g_graphics_active) {
        graphics_enter_mode();
    }
    if (g_graphics_active && g_session_logged_in) {
        graphics_open_window(UI_WINDOW_CUBE3D);
        graphics_draw_shell();
    }
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
        for (uint32_t i = 0; i < FB_PIXELS; i++) {
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
    mmu_map_identity((uint64_t)addr, framebuffer_map_length);
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
        bga_write(BGA_ENABLE, BGA_DISABLED);
        bga_write(BGA_XRES, FB_WIDTH);
        bga_write(BGA_YRES, FB_HEIGHT);
        bga_write(BGA_BPP, 32);
        bga_write(BGA_ENABLE, BGA_ENABLED | BGA_LFB);
        g_graphics_framebuffer_addr = addr;
        g_framebuffer_pitch_bytes = FB_WIDTH * sizeof(uint32_t);
        g_framebuffer_pitch_pixels = FB_WIDTH;
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
        graphics_show_boot_loading_screen();
    } else {
        log_write("graphics: skipping boot frame; mmu not active");
    }

    font_init();

    if (mmu_is_active()) {
        graphics_play_boot_animation();
        if (!g_session_logged_in) {
            graphics_open_window(UI_WINDOW_LOGON);
        }
        graphics_draw_shell();
        log_write("graphics: desktop drawn");
    } else {
        log_write("graphics: deferring full desktop draw; mmu not active");
        g_graphics_active = false;
    }
}
