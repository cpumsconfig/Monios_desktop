#include "audio.h"
#include "bluetooth.h"
#include "cmos.h"
#include "common.h"
#include "console.h"
#include "exec.h"
#include "file.h"
#include "font.h"
#include "interrupt.h"
#include "graphics.h"
#include "icons_data.h"
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
#define CONTEXT_MENU_DESKTOP_H 210
#define CONTEXT_MENU_FILES_H 280
#define UI_FILES_DRIVE_BTN_W 40
#define UI_FILES_DRIVE_BTN_H 20
#define UI_FILES_DRIVE_BAR_Y 58
#define UI_FILES_LIST_TOP_OFFSET 84
#define UI_FILES_LIST_HEIGHT_OFFSET 96
#define UI_PROP_DIALOG_W 360
#define UI_PROP_DIALOG_H 290
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
#define START_MENU_X ((FB_WIDTH - START_MENU_W) / 2)
#define FB_PIXELS (GRAPHICS_MAX_WIDTH * GRAPHICS_MAX_HEIGHT)
#define GRAPHICS_LOGIN_USERNAME_MAX 16
#define GRAPHICS_LOGIN_PASSWORD_MAX 64
#define GRAPHICS_FILE_NAME_MAX 32
#define GRAPHICS_FILE_PATH_MAX 128
#define GRAPHICS_FILE_ITEM_MAX 64
#define GRAPHICS_FILE_LIST_BUFFER 2048
#define GRAPHICS_CLIPBOARD_PATH_MAX 256
#define GRAPHICS_ICON_CACHE_MAX 48
#define GRAPHICS_ICON_BITMAP_SIZE 24
#define GRAPHICS_ICON_BITMAP_PIXELS (GRAPHICS_ICON_BITMAP_SIZE * GRAPHICS_ICON_BITMAP_SIZE)
#define GRAPHICS_ICON_FLAG_SHORTCUT 0x00000001U
#define GRAPHICS_ICON_FLAG_UAC      0x00000002U
#define GRAPHICS_NOTEPAD_TEXT_MAX (64U * 1024U)
#define GRAPHICS_NOTEPAD_TABS_MAX 8
#define GRAPHICS_NOTEPAD_DLG_BUF 64
#define GRAPHICS_NOTEPAD_TAB_TITLE_W 118
#define GRAPHICS_NOTEPAD_OPEN_MAX 48
/* syntax highlight colors (dark editor theme) */
#define NP_COLOR_DEFAULT 0x00E6EDF3u
#define NP_COLOR_KEYWORD 0x004FA8FFu
#define NP_COLOR_COMMENT 0x0057AB5Au
#define NP_COLOR_STRING  0x00F47067u
#define NP_COLOR_NUMBER  0x00D19A66u
#define NP_COLOR_BG      0x001E1E2Eu
#define NP_COLOR_LINEHL  0x002A3A4Fu
#define GRAPHICS_UAC_TEXT_MAX 96
#define GRAPHICS_SCROLLBAR_W 12
#define GRAPHICS_CONSOLE_ROWS CONSOLE_ROWS
#define GRAPHICS_WINDOW_TITLE_MAX TERMINAL_WINDOW_TITLE_MAX
#define GRAPHICS_CURSOR_CONFIG_PATH UI_CURSOR_CONFIG_PATH
#define GRAPHICS_CURSOR_MAX_WIDTH 32
#define GRAPHICS_CURSOR_MAX_HEIGHT 32
#define GRAPHICS_CURSOR_MAX_PIXELS (GRAPHICS_CURSOR_MAX_WIDTH * GRAPHICS_CURSOR_MAX_HEIGHT)
#define GRAPHICS_CURSOR_ASSET_MAX_BYTES (512U * 1024U)
#define GRAPHICS_WALLPAPER_MAX_WIDTH 4096
#define GRAPHICS_WALLPAPER_MAX_HEIGHT 2160
#define GRAPHICS_WALLPAPER_MAX_PIXELS (5U * 1024U * 1024U)
#define GRAPHICS_WALLPAPER_MAX_BYTES (16U * 1024U * 1024U)
#define GRAPHICS_JPEG_MAX_COMPONENTS 3
#define GRAPHICS_JPEG_MAX_SAMPLING 4
#define GRAPHICS_JPEG_MAX_MCU_SAMPLES (GRAPHICS_JPEG_MAX_SAMPLING * 8U * GRAPHICS_JPEG_MAX_SAMPLING * 8U)
#define GRAPHICS_BOOT_IMAGE_MAX_WIDTH 400
#define GRAPHICS_BOOT_IMAGE_MAX_HEIGHT 225
#define GRAPHICS_BOOT_IMAGE_MAX_PIXELS (GRAPHICS_BOOT_IMAGE_MAX_WIDTH * GRAPHICS_BOOT_IMAGE_MAX_HEIGHT)
#define GRAPHICS_BOOT_IMAGE_MAX_BYTES (512U * 1024U)
#define GRAPHICS_BOOT_FADE_STEPS 8U
#define GRAPHICS_BOOT_FADE_DELAY 30000U
#define GRAPHICS_LOGIN_CARD_MAX_W 680U
#define GRAPHICS_LOGIN_CARD_MAX_H 444U
#define GRAPHICS_LOGIN_CARD_MIN_W 480U
#define GRAPHICS_LOGIN_CARD_MIN_H 396U
#define GRAPHICS_LOGIN_BRAND_W 210U
#define GRAPHICS_LOGIN_USER_TILE_H 48U
#define GRAPHICS_LOGIN_POWER_SIZE 36U
#define UI_COLOR_WINDOW_BG 0x00FFFFFFu
#define UI_COLOR_WINDOW_BG_DARK 0x00202020u
#define UI_COLOR_WINDOW_EDGE 0x00BFCEDF
#define UI_COLOR_TITLE_BG 0x00F0F4F9
#define UI_COLOR_TITLE_TEXT 0x001C2430
#define UI_COLOR_ACCENT 0x000078D4u
#define UI_COLOR_ACCENT_DARK 0x00005A9Eu
#define UI_COLOR_WARN 0x00D94C5A
#define UI_COLOR_TASKBAR 0x00F3F6FA
#define UI_COLOR_TASKBAR_EDGE 0x00D5DEE9
/* WinUI 3 acrylic palettes (opaque RGB; alpha applied at blit time). */
#define UI_TASKBAR_ALPHA   178u
#define UI_MENU_ALPHA      220u
#define UI_LIGHT_TASKBAR_RGB 0x00F3F3F3u
#define UI_DARK_TASKBAR_RGB  0x00202020u
#define UI_LIGHT_MENU_RGB    0x00F9F9F9u
#define UI_DARK_MENU_RGB     0x001F1F1Fu
#define UI_LIGHT_TEXT_RGB    0x00000000u
#define UI_DARK_TEXT_RGB     0x00FFFFFFu
#define UI_LIGHT_SUBTEXT_RGB 0x00616161u
#define UI_DARK_SUBTEXT_RGB  0x00C8C8C8u

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
    UI_CLIPBOARD_CUT,
    UI_CLIPBOARD_TEXT   /* in-kernel global text clipboard (cross-app copy/paste) */
} ui_clipboard_mode_t;

/* Submenu shown cascading from the file/desktop context menu. */
typedef enum {
    UI_CTX_SUB_NONE = 0,
    UI_CTX_SUB_NEW,        /* 新建: 文本文档 / 文件夹 */
    UI_CTX_SUB_OPENWITH,   /* 打开方式 */
    UI_CTX_SUB_SENDTO      /* 发送到 */
} ui_ctx_submenu_t;

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
    GRAPHICS_ICON_DEFAULT_PROGRAM,
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
    GRAPHICS_ICON_NETWORK_OFFLINE,
    GRAPHICS_ICON_TEXT,
    GRAPHICS_ICON_IMAGE,
    GRAPHICS_ICON_DRIVE,
    GRAPHICS_ICON_VIDEO,
    GRAPHICS_ICON_TEXT_EDITOR
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
    bool bitmap_valid;
    char path[GRAPHICS_CLIPBOARD_PATH_MAX];
    uint32_t image_flags;
    uint32_t bitmap[GRAPHICS_ICON_BITMAP_PIXELS];
} graphics_icon_cache_entry_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    const char *label;
} graphics_mode_t;

typedef struct {
    uint16_t card_x;
    uint16_t card_y;
    uint16_t card_width;
    uint16_t card_height;
    uint16_t brand_width;
    uint16_t form_x;
    uint16_t form_width;
    uint16_t users_y;
    uint16_t user_tile_width;
    uint16_t account_y;
    uint16_t password_y;
    uint16_t button_y;
    uint16_t footer_y;
    uint16_t power_x;
    uint16_t power_y;
} graphics_login_layout_t;

typedef struct {
    bool valid;
    uint16_t values[64];
} graphics_jpeg_quant_table_t;

typedef struct {
    bool valid;
    uint8_t counts[17];
    uint8_t symbols[256];
    int32_t first_code[17];
    uint16_t first_symbol[17];
} graphics_jpeg_huffman_table_t;

typedef struct {
    uint8_t id;
    uint8_t h;
    uint8_t v;
    uint8_t quant_table;
    uint8_t dc_table;
    uint8_t ac_table;
    int32_t dc_pred;
} graphics_jpeg_component_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t bit_buffer;
    uint8_t bits_left;
    bool failed;
} graphics_jpeg_bit_reader_t;

typedef struct {
    graphics_jpeg_quant_table_t quant[4];
    graphics_jpeg_huffman_table_t huffman[2][4];
    graphics_jpeg_component_t components[GRAPHICS_JPEG_MAX_COMPONENTS];
    uint8_t scan_order[GRAPHICS_JPEG_MAX_COMPONENTS];
    uint16_t width;
    uint16_t height;
    uint16_t restart_interval;
    uint8_t component_count;
    uint8_t scan_count;
    uint8_t max_h;
    uint8_t max_v;
} graphics_jpeg_decoder_t;

typedef struct {
    uint32_t *pixels;
    uint16_t dest_width;
    uint16_t dest_height;
    uint16_t source_width;
    uint16_t source_height;
    uint32_t scaled_width;
    uint32_t scaled_height;
    uint32_t crop_x;
    uint32_t crop_y;
} graphics_wallpaper_target_t;

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
static bool g_desktop_recycle_flag[DESKTOP_LABEL_MAX];
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
static bool g_ux_dark;
static uint32_t g_ux_accent;
/* WinUI 3 hover / press interaction state (single active element at a time) */
typedef struct {
    int8_t   pinned;       /* taskbar pinned button index, -1 none */
    int32_t  running;      /* taskbar running window index, -1 none */
    int8_t   root;         /* start menu left nav row 0..3, -1 none */
    int8_t   content;      /* start menu content row index, -1 none */
    uint8_t  content_right;/* 1 = right column (display), 0 = mid column */
    uint8_t  theme;        /* start menu sun/moon toggle 0/1 */
    uint8_t  win_btn;      /* titlebar control: 0 none, 1 min, 2 max, 3 close */
    uint32_t win_index;    /* window owning the hovered titlebar control */
    uint8_t  pressed;      /* 1 = left button physically held over element */
} g_ux_hover_t;
static g_ux_hover_t g_ux_hover = { -1, -1, -1, -1, 0u, 0u, 0u, 0xFFFFFFFFu, 0u };
static ui_window_t g_windows[UI_WINDOW_MAX];
static uint64_t g_ux_win_open_tick[UI_WINDOW_MAX];
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
static bool g_uac_signed;
static bool g_uac_signature_valid;
static bool g_uac_publisher_trusted;
static bool g_secure_desktop;
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
static bool g_file_selected[GRAPHICS_FILE_ITEM_MAX];
static uint32_t g_file_anchor_index;
static bool g_properties_open;
static char g_properties_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static char g_properties_name[GRAPHICS_FILE_NAME_MAX];
static bool g_properties_is_dir;
/* ---- Enhanced properties dialog state ---- */
static char g_properties_edit_name[GRAPHICS_FILE_NAME_MAX]; /* editable filename buffer */
static bool g_properties_editing;                            /* filename text field has focus */
static bool g_properties_readonly;                          /* read-only attribute checkbox */
static bool g_properties_hidden;                             /* hidden attribute checkbox */
static uint8_t g_properties_orig_attr;                       /* original attr bits on open */
static char g_properties_orig_name[GRAPHICS_FILE_NAME_MAX]; /* original filename for rename compare */
static uint64_t g_properties_ctime;                          /* cached create time (unix s) */
static uint64_t g_properties_mtime;                           /* cached modify time */
static uint64_t g_properties_atime;                           /* cached access time */
static uint64_t g_properties_size;                           /* cached size */static ui_context_menu_mode_t g_context_menu_mode;
static ui_clipboard_mode_t g_clipboard_mode;
static char g_clipboard_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static bool g_clipboard_is_dir;
/* ---- Global text clipboard (cross-app copy/paste) ---- */
static char g_clipboard_text[GRAPHICS_CLIPBOARD_PATH_MAX];

/* ---- File drag-and-drop state ---- */
static bool g_dragging_file;                 /* a file item is being dragged */
static uint32_t g_drag_file_index;          /* index into g_file_items being dragged */
static char g_drag_file_path[GRAPHICS_CLIPBOARD_PATH_MAX]; /* absolute path being dragged */
static char g_drag_file_name[GRAPHICS_FILE_NAME_MAX];       /* display name */
static bool g_drag_file_is_dir;
static uint16_t g_drag_file_x;              /* current cursor x for the drag ghost */
static uint16_t g_drag_file_y;
static bool g_drag_pending;                  /* left button down on a file item, not yet a drag */
static uint16_t g_drag_start_x;
static uint16_t g_drag_start_y;

/* ---- Rubber-band (marquee) selection in the file list ---- */
static bool g_rubber_active;
static uint16_t g_rubber_start_x;
static uint16_t g_rubber_start_y;
static uint16_t g_rubber_cur_x;
static uint16_t g_rubber_cur_y;

/* ---- Cascading context-menu submenu ---- */
static ui_ctx_submenu_t g_ctx_submenu;

/* ---- "New file/folder" inline rename dialog ---- */
static bool g_newitem_open;
static bool g_newitem_is_dir;
static char g_newitem_name[GRAPHICS_FILE_NAME_MAX];
static char g_newitem_parent[GRAPHICS_CLIPBOARD_PATH_MAX];

/* ---- File search in the explorer toolbar ---- */
static bool g_file_search_open;              /* search box focused / active */
static char g_file_search_query[GRAPHICS_FILE_NAME_MAX];
static char g_search_results[GRAPHICS_FILE_ITEM_MAX][GRAPHICS_CLIPBOARD_PATH_MAX];
static uint32_t g_search_result_count;
static bool g_file_searching;               /* g_file_items currently shows search results */

/* ---- Pinyin input method ---- */
static bool g_ime_on;                       /* Chinese input enabled (Ctrl+Space) */
static char g_ime_pinyin[32];               /* as-yet-uncommitted pinyin buffer */
static char g_ime_cands[9][8];              /* up to 9 candidate hanzi (UTF-8) */
static uint32_t g_ime_cand_count;
static uint16_t g_ime_caret_x;              /* where the candidate window anchors */
static uint16_t g_ime_caret_y;
static bool g_player_button_pressed;
static bool g_player_browser_open;
static char g_player_current_path[GRAPHICS_FILE_PATH_MAX];
static graphics_file_item_t g_player_items[DESKTOP_LABEL_MAX];
static uint32_t g_player_item_count;
static uint32_t g_player_selected_index;
static char g_player_selected_path[GRAPHICS_CLIPBOARD_PATH_MAX];
static char g_player_status[64];
/* ---- Notepad multi-tab editor state ---- */
typedef enum {
    NP_DLG_NONE = 0,
    NP_DLG_FIND,
    NP_DLG_REPLACE,
    NP_DLG_OPEN
} np_dialog_t;

typedef struct {
    bool used;
    bool modified;
    char path[GRAPHICS_CLIPBOARD_PATH_MAX];
    char text[GRAPHICS_NOTEPAD_TEXT_MAX];
    uint32_t len;
    uint32_t cursor;      /* caret byte offset */
    uint32_t scroll_row; /* first visible line index */
} np_tab_t;

static np_tab_t g_np_tabs[GRAPHICS_NOTEPAD_TABS_MAX];
static int32_t g_np_active = -1;
static bool g_notepad_focus;
static np_dialog_t g_np_dlg = NP_DLG_NONE;
static char g_np_find[GRAPHICS_NOTEPAD_DLG_BUF];
static char g_np_replace[GRAPHICS_NOTEPAD_DLG_BUF];
static uint32_t g_np_dlg_pos;
static bool g_np_dlg_replace_field; /* find field (false) / replace field (true) */
static uint32_t g_np_find_next;
static bool g_np_find_valid;
static uint32_t g_np_find_match_start;
static uint32_t g_np_find_match_len;
/* open-file dialog */
static char g_np_open_dir[GRAPHICS_CLIPBOARD_PATH_MAX];
static char g_np_open_names[GRAPHICS_NOTEPAD_OPEN_MAX][GRAPHICS_FILE_NAME_MAX];
static bool g_np_open_isdir[GRAPHICS_NOTEPAD_OPEN_MAX];
static uint32_t g_np_open_count;
static uint32_t g_np_open_sel;
static uint32_t g_np_colors[GRAPHICS_NOTEPAD_TEXT_MAX];
static bool g_cube3d_open;
static float g_cube3d_angle_x;
static float g_cube3d_angle_y;
static float g_cube3d_angle_z;
static uint64_t g_cube3d_last_tick;
static uint32_t *g_wallpaper_pixels;
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
static void graphics_open_item(const char *path, bool is_dir);
static void graphics_current_desktop_path(char output[GRAPHICS_FILE_PATH_MAX]);
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
static void graphics_login_layout(graphics_login_layout_t *layout);
static void graphics_select_login_user(uint32_t index);
static void graphics_draw_login_avatar(uint16_t x, uint16_t y, uint16_t size,
                                       const char *name, uint32_t fill, uint32_t text);
static void graphics_fill_soft_rect(uint16_t x, uint16_t y, uint16_t width,
                                    uint16_t height, uint32_t color);
static void graphics_draw_soft_rect_outline(uint16_t x, uint16_t y, uint16_t width,
                                            uint16_t height, uint32_t color);
static void graphics_blit_icon(uint32_t icon_id, uint16_t x, uint16_t y, uint16_t size);
static void graphics_fill_rect_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                     uint32_t fg, uint32_t alpha);
static void graphics_fill_soft_rect_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                          uint32_t fg, uint32_t alpha);
static void graphics_fill_rounded_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color, uint16_t radius);
static void graphics_draw_rounded_rect_outline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color, uint16_t radius);
static void graphics_fill_rounded_rect_alpha(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t fg, uint32_t alpha, uint16_t radius);
static void graphics_fill_disc(uint16_t cx, uint16_t cy, uint16_t r, uint32_t color);
static uint32_t ui_color(uint32_t light_color, uint32_t dark_color);
static uint32_t graphics_brighten_color(uint32_t color, uint32_t percent);
static uint32_t graphics_darken_color(uint32_t color, uint32_t percent);
static bool graphics_update_hover(uint16_t x, uint16_t y, uint8_t buttons);
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
static void graphics_drop_commit(uint16_t x, uint16_t y);
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
    return ch == '/' || ch == PATH_SEPARATOR;
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

static void graphics_login_layout(graphics_login_layout_t *layout)
{
    uint16_t usable_width;
    uint16_t usable_height;
    uint32_t user_count;
    uint16_t user_gap = 8;

    if (layout == NULL) {
        return;
    }
    memset(layout, 0, sizeof(*layout));
    usable_width = FB_WIDTH > 48 ? (uint16_t) (FB_WIDTH - 48) : FB_WIDTH;
    usable_height = FB_HEIGHT > 48 ? (uint16_t) (FB_HEIGHT - 48) : FB_HEIGHT;
    layout->card_width = usable_width > GRAPHICS_LOGIN_CARD_MAX_W ?
                         GRAPHICS_LOGIN_CARD_MAX_W : usable_width;
    layout->card_height = usable_height > GRAPHICS_LOGIN_CARD_MAX_H ?
                          GRAPHICS_LOGIN_CARD_MAX_H : usable_height;
    if (layout->card_width < GRAPHICS_LOGIN_CARD_MIN_W) {
        layout->card_width = usable_width;
    }
    if (layout->card_height < GRAPHICS_LOGIN_CARD_MIN_H) {
        layout->card_height = usable_height;
    }
    layout->card_x = (uint16_t) ((FB_WIDTH - layout->card_width) / 2);
    layout->card_y = (uint16_t) ((FB_HEIGHT - layout->card_height) / 2);
    layout->brand_width = layout->card_width > 580 ? GRAPHICS_LOGIN_BRAND_W : 168;
    if (layout->brand_width + 48 >= layout->card_width) {
        layout->brand_width = 0;
    }
    layout->form_x = (uint16_t) (layout->card_x + layout->brand_width + (layout->brand_width > 0 ? 34 : 24));
    layout->form_width = layout->card_width - layout->brand_width - (layout->brand_width > 0 ? 58 : 48);
    layout->users_y = (uint16_t) (layout->card_y + 104);
    layout->account_y = (uint16_t) (layout->users_y + GRAPHICS_LOGIN_USER_TILE_H + 16);
    layout->password_y = (uint16_t) (layout->account_y + 56);
    layout->button_y = (uint16_t) (layout->password_y + 54);
    layout->footer_y = (uint16_t) (layout->button_y + 58);

    user_count = session_user_count();
    if (user_count > SESSION_USER_MAX) {
        user_count = SESSION_USER_MAX;
    }
    if (user_count == 0) {
        layout->user_tile_width = layout->form_width;
    } else if (layout->form_width > (user_count - 1U) * user_gap) {
        layout->user_tile_width = (uint16_t) ((layout->form_width - (user_count - 1U) * user_gap) / user_count);
    } else {
        layout->user_tile_width = 1;
    }
    layout->power_x = FB_WIDTH > GRAPHICS_LOGIN_POWER_SIZE + 16 ?
                      (uint16_t) (FB_WIDTH - GRAPHICS_LOGIN_POWER_SIZE - 16) : 8;
    layout->power_y = FB_HEIGHT > GRAPHICS_LOGIN_POWER_SIZE + 16 ?
                      (uint16_t) (FB_HEIGHT - GRAPHICS_LOGIN_POWER_SIZE - 16) : 8;
}

static void graphics_select_login_user(uint32_t index)
{
    const session_user_t *user = session_user_at(index);

    if (user == NULL) {
        return;
    }
    g_selected_login_user = index;
    strlcpy(g_login_username, user->name, sizeof(g_login_username));
    g_login_password[0] = '\0';
    g_login_field = 1;
    g_login_error = false;
}

static void graphics_draw_login_avatar(uint16_t x, uint16_t y, uint16_t size,
                                       const char *name, uint32_t fill, uint32_t text)
{
    uint16_t center_x;
    uint16_t head_radius;
    uint16_t head_y;
    uint16_t shoulder_y;
    uint16_t shoulder_radius_x;
    uint16_t shoulder_radius_y;

    if (size == 0) {
        return;
    }
    (void) name;
    graphics_fill_soft_rect(x, y, size, size, fill);
    graphics_draw_soft_rect_outline(x, y, size, size, 0x00FFFFFF);

    /*
     * Draw a small OS-style user glyph directly into the framebuffer. Using
     * integer ellipses keeps the login page independent from icon assets and
     * still looks clean at both tile and brand sizes.
     */
    center_x = (uint16_t) (x + size / 2);
    head_radius = size >= 20 ? (uint16_t) (size / 6) : 2;
    head_y = (uint16_t) (y + size / 3);
    for (int32_t dy = -(int32_t) head_radius; dy <= (int32_t) head_radius; dy++) {
        for (int32_t dx = -(int32_t) head_radius; dx <= (int32_t) head_radius; dx++) {
            if (dx * dx + dy * dy <= (int32_t) head_radius * (int32_t) head_radius) {
                graphics_plot((uint16_t) (center_x + dx), (uint16_t) (head_y + dy), text);
            }
        }
    }

    shoulder_y = (uint16_t) (y + size * 3 / 4);
    shoulder_radius_x = size >= 24 ? (uint16_t) (size * 3 / 10) : 6;
    shoulder_radius_y = size >= 24 ? (uint16_t) (size / 5) : 4;
    for (int32_t dy = -(int32_t) shoulder_radius_y; dy <= (int32_t) shoulder_radius_y; dy++) {
        for (int32_t dx = -(int32_t) shoulder_radius_x; dx <= (int32_t) shoulder_radius_x; dx++) {
            int32_t scaled_x = dx * (int32_t) shoulder_radius_y;
            int32_t scaled_y = dy * (int32_t) shoulder_radius_x;

            if (scaled_x * scaled_x + scaled_y * scaled_y <=
                (int32_t) shoulder_radius_x * (int32_t) shoulder_radius_x *
                (int32_t) shoulder_radius_y * (int32_t) shoulder_radius_y) {
                int32_t px = (int32_t) center_x + dx;
                int32_t py = (int32_t) shoulder_y + dy;

                if (px >= (int32_t) x && px < (int32_t) x + size &&
                    py >= (int32_t) y && py < (int32_t) y + size) {
                    graphics_plot((uint16_t) px, (uint16_t) py, text);
                }
            }
        }
    }
}

static void graphics_reset_login(void)
{
    uint32_t user_count = session_user_count();

    if (user_count > 0) {
        if (g_selected_login_user >= user_count) {
            g_selected_login_user = 0;
        }
        graphics_select_login_user(g_selected_login_user);
    } else {
        strcpy(g_login_username, "root");
        g_login_password[0] = '\0';
        g_login_field = 1;
        g_login_error = false;
    }
}

static void __attribute__((unused)) graphics_copy_uac_text(char *dst, uint32_t dst_size, const char *src)
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
    /* The file manager opens on the user's Desktop folder, mirroring the
     * desktop view: the desktop IS the file browser for the Desktop dir. */
    graphics_current_desktop_path(g_file_current_path);
    g_file_item_count = 0;
    g_file_selected_index = 0;
    g_file_anchor_index = 0;
    g_file_scroll_offset = 0;
    memset(g_file_selected, 0, sizeof(g_file_selected));
}

static uint32_t graphics_file_visible_rows(const ui_window_t *window)
{
    uint32_t content_height;

    if (window == NULL || window->height <= UI_FILES_LIST_HEIGHT_OFFSET) {
        return 1;
    }
    content_height = window->height - UI_FILES_LIST_HEIGHT_OFFSET;
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

static uint32_t __attribute__((unused)) graphics_file_selected_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < g_file_item_count && i < GRAPHICS_FILE_ITEM_MAX; i++) {
        if (g_file_selected[i]) {
            count++;
        }
    }
    return count;
}

static void graphics_file_clear_selection(void)
{
    memset(g_file_selected, 0, sizeof(g_file_selected));
}

static void graphics_format_file_size(int32_t size, char *out, uint32_t out_size)
{
    char num[12];
    char fbuf[8];
    uint32_t major;
    uint32_t frac;
    const char *unit;
    uint32_t pos = 0;

    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (size < 0) {
        strlcpy(out, "Unknown", out_size);
        return;
    }
    if (size < 1024) {
        graphics_u32_to_dec(num, (uint32_t) size);
        strlcpy(out, num, out_size);
        pos = (uint32_t) strlen(out);
        if (pos + 2 < out_size) {
            out[pos++] = ' ';
            out[pos++] = 'B';
            out[pos] = '\0';
        }
        return;
    }
    if (size < 1024 * 1024) {
        major = (uint32_t) size / 1024u;
        frac = ((uint32_t) size % 1024u) * 100u / 1024u;
        unit = " KB";
    } else {
        major = (uint32_t) size / (1024u * 1024u);
        frac = ((uint32_t) size % (1024u * 1024u)) * 100u / (1024u * 1024u);
        unit = " MB";
    }
    graphics_u32_to_dec(num, major);
    strlcpy(out, num, out_size);
    pos = (uint32_t) strlen(out);
    if (pos + 1 < out_size) {
        out[pos++] = '.';
        out[pos] = '\0';
    }
    if (frac < 10u && pos + 1 < out_size) {
        out[pos++] = '0';
        out[pos] = '\0';
    }
    graphics_u32_to_dec(fbuf, frac);
    {
        uint32_t fi = 0;
        while (fbuf[fi] != '\0' && pos + 1 < out_size) {
            out[pos++] = fbuf[fi++];
            out[pos] = '\0';
        }
    }
    {
        uint32_t ui = 0;
        while (unit[ui] != '\0' && pos + 1 < out_size) {
            out[pos++] = unit[ui++];
            out[pos] = '\0';
        }
    }
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
    uint32_t hist = console_history_rows_for_pid(owner_pid);

    if (rows == 0) {
        rows = 1;
    }
    if (rows > GRAPHICS_CONSOLE_ROWS) {
        rows = GRAPHICS_CONSOLE_ROWS;
    }
    return hist + rows;
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

static uint32_t __attribute__((unused)) graphics_file_name_from_line(const char *line, char *name, uint32_t name_size)
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

/* Unified directory scanner: parses a file_list_dir() listing into an array of
 * graphics_file_item_t. Shared by the desktop (which is just a grid view of the
 * Desktop folder) and the file-manager window, so both stay in lock-step. */
static uint32_t graphics_scan_directory(const char *path, graphics_file_item_t *items, uint32_t max_items)
{
    char buffer[GRAPHICS_FILE_LIST_BUFFER];
    uint32_t count = 0;
    uint32_t i;

    if (path == NULL || items == NULL || max_items == 0) {
        return 0;
    }
    if (!file_list_dir(path, buffer, sizeof(buffer))) {
        return 0;
    }
    for (i = 0; buffer[i] != '\0' && count < max_items; i++) {
        uint32_t line_start = i;
        uint32_t line_len = 0;

        while (buffer[i] != '\0' && buffer[i] != '\n') {
            i++;
        }
        line_len = i - line_start;
        if (line_len == 0) {
            continue;
        }
        if (line_len >= sizeof(items[count].name)) {
            line_len = sizeof(items[count].name) - 1;
        }
        memcpy(items[count].name, &buffer[line_start], line_len);
        items[count].name[line_len] = '\0';
        items[count].is_dir = line_len > 0 && graphics_is_separator(items[count].name[line_len - 1]);
        if (items[count].is_dir) {
            items[count].name[line_len - 1] = '\0';
        }
        count++;
    }
    return count;
}

static void graphics_fill_file_browser(void)
{
    g_file_item_count = graphics_scan_directory(g_file_current_path, g_file_items,
                                                GRAPHICS_FILE_ITEM_MAX);
    memset(g_file_selected, 0, sizeof(g_file_selected));
    if (g_file_selected_index >= g_file_item_count && g_file_item_count > 0) {
        g_file_selected_index = g_file_item_count - 1;
    }
    if (g_file_item_count > 0) {
        g_file_selected[g_file_selected_index] = true;
        g_file_anchor_index = g_file_selected_index;
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
            graphics_path_has_suffix(g_player_items[count].name, ".WAV")) {
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
    strcpy(g_player_status, "select WAV file");
    graphics_fill_player_browser();
}

static bool graphics_player_path_is_audio(const char *path)
{
    return graphics_path_has_suffix(path, ".wav") ||
           graphics_path_has_suffix(path, ".WAV");
}

typedef struct {
    uint16_t format_tag;
    uint16_t channels;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
} graphics_wav_pcm_fmt_t;

static bool graphics_read_wav_header(const uint8_t *data, uint32_t size,
                                     graphics_wav_pcm_fmt_t *fmt, uint32_t *data_offset)
{
    uint32_t pos = 0;

    if (size < 44 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }
    pos = 12;
    while (pos + 8 <= size) {
        uint32_t chunk_size = *(const uint32_t *) (data + pos + 4);

        if (memcmp(data + pos, "fmt ", 4) == 0) {
            if (chunk_size < 16 || pos + 8 + chunk_size > size) {
                return false;
            }
            memcpy(fmt, data + pos + 8, sizeof(*fmt));
        } else if (memcmp(data + pos, "data", 4) == 0) {
            *data_offset = pos + 8;
            return true;
        }
        pos += 8 + chunk_size + (chunk_size & 1u);
    }
    return false;
}

static bool graphics_player_try_play(const char *path)
{
    graphics_wav_pcm_fmt_t fmt;
    uint8_t *file_data;
    int32_t file_bytes;
    uint32_t data_offset = 0;
    uint32_t pcm_bytes;

    if (path == NULL || path[0] == '\0' || !graphics_player_path_is_audio(path)) {
        strcpy(g_player_status, "only WAV supported");
        return false;
    }
    file_bytes = file_size(path);
    if (file_bytes <= 44 || (uint32_t) file_bytes > AUDIO_PCM_MAX_BYTES + 0x1000u) {
        strcpy(g_player_status, "file too large");
        return false;
    }
    file_data = (uint8_t *) kmalloc((uint32_t) file_bytes);
    if (file_data == NULL) {
        strcpy(g_player_status, "alloc failed");
        return false;
    }
    if (file_read(path, file_data, (uint32_t) file_bytes) != file_bytes) {
        kfree(file_data);
        strcpy(g_player_status, "read failed");
        return false;
    }
    if (!graphics_read_wav_header(file_data, (uint32_t) file_bytes, &fmt, &data_offset) ||
        fmt.format_tag != 1 || fmt.channels != 2 || fmt.bits_per_sample != 16 ||
        data_offset >= (uint32_t) file_bytes) {
        kfree(file_data);
        strcpy(g_player_status, "wav unsupported");
        return false;
    }
    pcm_bytes = (uint32_t) file_bytes - data_offset;
    if (pcm_bytes == 0 || pcm_bytes > AUDIO_PCM_MAX_BYTES) {
        kfree(file_data);
        strcpy(g_player_status, "wav too large");
        return false;
    }
    strcpy(g_player_selected_path, path);
    if (audio_play_pcm(file_data + data_offset, pcm_bytes, fmt.samples_per_sec,
                       fmt.channels, fmt.bits_per_sample)) {
        kfree(file_data);
        strcpy(g_player_status, "playing");
        g_player_browser_open = false;
        return true;
    }
    kfree(file_data);
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
    for (uint32_t i = 0; i < session_user_count(); i++) {
        const session_user_t *user = session_user_at(i);

        if (user != NULL && strcmp(user->name, g_login_username) == 0) {
            g_selected_login_user = i;
            break;
        }
    }
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
    g_secure_desktop = false;
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
    exec_signature_status_for_path(program_path,
                                   &g_uac_signed,
                                   &g_uac_signature_valid,
                                   &g_uac_publisher_trusted);
    g_secure_desktop = true;
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
    uint32_t path_len, suffix_len;

    if (path == NULL || suffix == NULL) {
        return false;
    }
    path_len = (uint32_t) strlen(path);
    suffix_len = (uint32_t) strlen(suffix);

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
    if (index < g_desktop_entry_count && g_desktop_recycle_flag[index]) {
        out_path[0] = desktop_path[0];
        out_path[1] = ':';
        out_path[2] = '\\';
        strlcpy(out_path + 3, "$RECYCLE.BIN", GRAPHICS_CLIPBOARD_PATH_MAX - 3);
        return;
    }
    strcpy(out_path, desktop_path);
    graphics_append_path_component(out_path, GRAPHICS_CLIPBOARD_PATH_MAX, g_desktop_entries[index].name);
}

static void graphics_build_file_entry_path(uint32_t index, char out_path[GRAPHICS_CLIPBOARD_PATH_MAX])
{
    strcpy(out_path, g_file_current_path);
    graphics_append_path_component(out_path, GRAPHICS_CLIPBOARD_PATH_MAX, g_file_items[index].name);
}

static void ux_clip_push(const char *text);
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
    ux_clip_push(path);
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

static bool graphics_context_text_enabled(const char *text)
{
    const char *line;
    char value[32];
    uint32_t length = 0;

    if (text == NULL) {
        return false;
    }
    line = strchr(text, '\n');
    if (line == NULL) {
        return false;
    }
    while (*line == '\n' || *line == '\r' || *line == ' ' || *line == '\t') {
        line++;
    }
    while (line[length] != '\0' && line[length] != '\r' &&
           line[length] != '\n' && length + 1 < sizeof(value)) {
        value[length] = line[length];
        length++;
    }
    value[length] = '\0';
    return strcasecmp(value, "context=1") == 0 ||
           strcasecmp(value, "context=enabled") == 0 ||
           strcasecmp(value, "context-menu=1") == 0 ||
           strcasecmp(value, "context-menu=enabled") == 0;
}

static bool graphics_context_menu_enabled(const char *path)
{
    char metadata[192];
    char context_path[GRAPHICS_CLIPBOARD_PATH_MAX + 8];
    int32_t size;

    if (path == NULL || path[0] == '\0' || file_is_dir(path)) {
        return false;
    }
    if (graphics_path_has_suffix(path, ".lnk") ||
        graphics_path_has_suffix(path, ".LNK")) {
        size = file_read(path, metadata, sizeof(metadata) - 1);
        if (size > 0) {
            metadata[size] = '\0';
            if (graphics_context_text_enabled(metadata)) {
                return true;
            }
        }
    }
    if (strlen(path) + 5 >= sizeof(context_path)) {
        return false;
    }
    strcpy(context_path, path);
    strcpy(context_path + strlen(context_path), ".ctx");
    size = file_read(context_path, metadata, sizeof(metadata) - 1);
    if (size <= 0) {
        return false;
    }
    graphics_trim_shortcut_target(metadata);
    return strcasecmp(metadata, "1") == 0 ||
           strcasecmp(metadata, "enabled") == 0 ||
           strcasecmp(metadata, "context=1") == 0 ||
           strcasecmp(metadata, "context=enabled") == 0;
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
        { "browser", UI_BROWSER_PATH },
        { "browser.exe", UI_BROWSER_PATH },
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

    /* theme toggle button in header (sun/moon) */
    if (graphics_point_in_rect(x, y, (uint16_t)(menu_x + START_MENU_W - 38), (uint16_t)(menu_y + 6), 26, 24)) {
        graphics_theme_set(!g_ux_dark, g_ux_accent);
        return true;
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

/* Unified "open an entry" handler shared by the desktop and the file-manager
 * window (double-click, context-menu Open, drag-drop onto a folder). is_dir is
 * informational only: graphics_open_path re-resolves the kind of the target. */
static void graphics_open_item(const char *path, bool is_dir)
{
    (void)is_dir;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    graphics_open_path(path);
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

/* ==================== Notepad multi-tab editor ==================== */
static void graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color);
static void graphics_draw_codepoint(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color);
static np_tab_t *np_active_tab(void)
{
    if (g_np_active < 0 || !g_np_tabs[g_np_active].used) {
        return NULL;
    }
    return &g_np_tabs[g_np_active];
}

static bool np_is_c_source(const char *path)
{
    return graphics_path_has_suffix(path, ".c")   || graphics_path_has_suffix(path, ".h")  ||
           graphics_path_has_suffix(path, ".cpp") || graphics_path_has_suffix(path, ".cc") ||
           graphics_path_has_suffix(path, ".cxx")|| graphics_path_has_suffix(path, ".hpp");
}

static void np_tab_reset(np_tab_t *t)
{
    t->used = true;
    t->modified = false;
    t->path[0] = '\0';
    t->text[0] = '\0';
    t->len = 0;
    t->cursor = 0;
    t->scroll_row = 0;
}

static int32_t np_free_tab(void)
{
    for (int32_t i = 0; i < GRAPHICS_NOTEPAD_TABS_MAX; i++) {
        if (!g_np_tabs[i].used) {
            return i;
        }
    }
    return -1;
}

static bool np_load_into(np_tab_t *t, const char *path)
{
    int32_t size;

    if (path == NULL || file_is_dir(path)) {
        return false;
    }
    size = file_read(path, t->text, GRAPHICS_NOTEPAD_TEXT_MAX - 1u);
    if (size < 0) {
        return false;
    }
    t->len = (uint32_t) size;
    t->text[t->len] = '\0';
    strcpy(t->path, path);
    t->cursor = t->len;
    t->scroll_row = 0;
    t->modified = false;
    return true;
}

static bool np_save(np_tab_t *t)
{
    if (t == NULL || t->path[0] == '\0') {
        return false;
    }
    if (file_write(t->path, t->text, t->len) < 0) {
        return false;
    }
    t->modified = false;
    return true;
}

static uint32_t np_line_count(const np_tab_t *t)
{
    uint32_t n = 1u;
    for (uint32_t i = 0; i < t->len; i++) {
        if (t->text[i] == '\n') {
            n++;
        }
    }
    return n;
}

static uint32_t np_line_start(const np_tab_t *t, uint32_t line)
{
    uint32_t l = 0, pos = 0;
    while (pos < t->len && l < line) {
        if (t->text[pos] == '\n') {
            l++;
        }
        pos++;
    }
    return pos;
}

static uint32_t np_line_end(const np_tab_t *t, uint32_t line)
{
    uint32_t pos = np_line_start(t, line);
    while (pos < t->len && t->text[pos] != '\n') {
        pos++;
    }
    return pos;
}

static void np_offset_linecol(const np_tab_t *t, uint32_t off, uint32_t *line, uint32_t *col)
{
    uint32_t l = 0, c = 0;
    for (uint32_t i = 0; i < off && i < t->len; i++) {
        if (t->text[i] == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

static void np_rehighlight(np_tab_t *t)
{
    static const char *kw[] = {
        "int","char","if","else","for","while","return","struct","void",
        "const","static","unsigned","signed","long","short","float","double",
        "switch","case","break","continue","do","goto","sizeof","typedef",
        "enum","union","extern","register","volatile","inline"
    };
    enum { S_NORMAL, S_STR, S_CHR, S_LINE, S_BLOCK };
    const char *s = t->text;
    bool hl = np_is_c_source(t->path);
    uint32_t i = 0;

    for (uint32_t k = 0; k < t->len; k++) {
        g_np_colors[k] = NP_COLOR_DEFAULT;
    }
    if (!hl) {
        return;
    }
    while (i < t->len) {
        char c = s[i];
        char n = (i + 1u < t->len) ? s[i + 1u] : 0;
        if (i >= t->len) { break; }
        if (c == '/' && n == '/') {
            g_np_colors[i] = g_np_colors[i + 1u] = NP_COLOR_COMMENT;
            i += 2u;
            while (i < t->len && s[i] != '\n') { g_np_colors[i++] = NP_COLOR_COMMENT; }
            continue;
        }
        if (c == '/' && n == '*') {
            g_np_colors[i] = g_np_colors[i + 1u] = NP_COLOR_COMMENT;
            i += 2u;
            while (i + 1u < t->len && !(s[i] == '*' && s[i + 1u] == '/')) { g_np_colors[i++] = NP_COLOR_COMMENT; }
            if (i + 1u < t->len) { g_np_colors[i] = g_np_colors[i + 1u] = NP_COLOR_COMMENT; i += 2u; }
            continue;
        }
        if (c == '"') {
            g_np_colors[i++] = NP_COLOR_STRING;
            while (i < t->len && s[i] != '"') {
                if (s[i] == '\\' && i + 1u < t->len) { g_np_colors[i++] = NP_COLOR_STRING; }
                g_np_colors[i++] = NP_COLOR_STRING;
            }
            if (i < t->len) { g_np_colors[i++] = NP_COLOR_STRING; }
            continue;
        }
        if (c == '\'') {
            g_np_colors[i++] = NP_COLOR_STRING;
            while (i < t->len && s[i] != '\'') {
                if (s[i] == '\\' && i + 1u < t->len) { g_np_colors[i++] = NP_COLOR_STRING; }
                g_np_colors[i++] = NP_COLOR_STRING;
            }
            if (i < t->len) { g_np_colors[i++] = NP_COLOR_STRING; }
            continue;
        }
        if (c >= '0' && c <= '9') {
            while (i < t->len && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) {
                g_np_colors[i++] = NP_COLOR_NUMBER;
            }
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            uint32_t st = i;
            bool iskw = false;
            while (i < t->len && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                   (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) {
                i++;
            }
            for (uint32_t k = 0; k < sizeof(kw) / sizeof(kw[0]); k++) {
                uint32_t wl = strlen(kw[k]);
                if (i - st == wl && strncmp(s + st, kw[k], wl) == 0) { iskw = true; break; }
            }
            {
                uint32_t colr = iskw ? NP_COLOR_KEYWORD : NP_COLOR_DEFAULT;
                for (uint32_t j = st; j < i; j++) { g_np_colors[j] = colr; }
            }
            continue;
        }
        g_np_colors[i++] = NP_COLOR_DEFAULT;
    }
}

static void np_insert_at(np_tab_t *t, uint32_t at, char ch)
{
    if (t->len + 1u >= GRAPHICS_NOTEPAD_TEXT_MAX) {
        return;
    }
    if (at > t->len) {
        at = t->len;
    }
    memmove(t->text + at + 1u, t->text + at, t->len - at);
    t->text[at] = ch;
    t->len++;
    t->text[t->len] = '\0';
    t->cursor = at + 1u;
    t->modified = true;
}

static void np_erase_at(np_tab_t *t, uint32_t at)
{
    if (at >= t->len) {
        return;
    }
    memmove(t->text + at, t->text + at + 1u, t->len - at - 1u);
    t->len--;
    t->text[t->len] = '\0';
    if (t->cursor > at) {
        t->cursor--;
    }
    t->modified = true;
}

static void np_cursor_left(np_tab_t *t)  { if (t->cursor > 0u) t->cursor--; }
static void np_cursor_right(np_tab_t *t) { if (t->cursor < t->len) t->cursor++; }
static void np_cursor_home(np_tab_t *t)
{
    uint32_t line, col; np_offset_linecol(t, t->cursor, &line, &col);
    t->cursor = np_line_start(t, line);
}
static void np_cursor_end(np_tab_t *t)
{
    uint32_t line, col; np_offset_linecol(t, t->cursor, &line, &col);
    t->cursor = np_line_end(t, line);
}
static void np_cursor_up(np_tab_t *t)
{
    uint32_t line, col; np_offset_linecol(t, t->cursor, &line, &col);
    if (line > 0u) {
        uint32_t ns = np_line_start(t, line - 1u);
        uint32_t ne = np_line_end(t, line - 1u);
        uint32_t off = ns + col;
        t->cursor = off > ne ? ne : off;
    }
}
static void np_cursor_down(np_tab_t *t)
{
    uint32_t line, col; np_offset_linecol(t, t->cursor, &line, &col);
    uint32_t lc = np_line_count(t);
    if (line + 1u < lc) {
        uint32_t ns = np_line_start(t, line + 1u);
        uint32_t ne = np_line_end(t, line + 1u);
        uint32_t off = ns + col;
        t->cursor = off > ne ? ne : off;
    } else {
        t->cursor = t->len;
    }
}

static int np_np_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static bool np_find_from(np_tab_t *t, const char *needle, uint32_t from,
                         uint32_t *out_pos, uint32_t *out_len)
{
    uint32_t nlen = strlen(needle);
    if (nlen == 0u || from > t->len) {
        return false;
    }
    for (uint32_t i = from; i + nlen <= t->len; i++) {
        uint32_t k = 0;
        while (k < nlen && np_np_tolower((unsigned char)t->text[i + k]) ==
                          np_np_tolower((unsigned char)needle[k])) {
            k++;
        }
        if (k == nlen) {
            *out_pos = i;
            *out_len = nlen;
            return true;
        }
    }
    return false;
}

static void np_do_find_next(np_tab_t *t)
{
    uint32_t pos, len;
    if (g_np_find[0] == '\0') {
        g_np_find_valid = false;
        return;
    }
    uint32_t start = g_np_find_valid ? g_np_find_next : 0u;
    if (np_find_from(t, g_np_find, start, &pos, &len)) {
        g_np_find_match_start = pos;
        g_np_find_match_len = len;
        g_np_find_next = pos + len;
        g_np_find_valid = true;
    } else if (np_find_from(t, g_np_find, 0u, &pos, &len)) {
        g_np_find_match_start = pos;
        g_np_find_match_len = len;
        g_np_find_next = pos + len;
        g_np_find_valid = true;
    } else {
        g_np_find_valid = false;
    }
    if (g_np_find_valid) {
        uint32_t line, cc;
        np_offset_linecol(t, g_np_find_match_start, &line, &cc);
        t->scroll_row = line;
    }
}

static np_tab_t *np_open_blank(const char *dir)
{
    int32_t idx = np_free_tab();
    char tmp[GRAPHICS_CLIPBOARD_PATH_MAX];

    if (idx < 0) {
        return NULL;
    }
    np_tab_t *t = &g_np_tabs[idx];
    np_tab_reset(t);
    strcpy(tmp, dir);
    graphics_append_path_component(tmp, sizeof(tmp), "untitled.txt");
    if (file_exists(tmp)) {
        strcpy(tmp, dir);
        graphics_append_path_component(tmp, sizeof(tmp), "untitled1.txt");
    }
    strcpy(t->path, tmp);
    g_np_active = idx;
    g_np_dlg = NP_DLG_NONE;
    g_np_find_valid = false;
    return t;
}

static void np_open_refresh(void)
{
    char buffer[GRAPHICS_FILE_LIST_BUFFER];
    uint32_t i = 0;

    g_np_open_count = 0u;
    g_np_open_sel = 0u;
    if (!file_list_dir(g_np_open_dir, buffer, sizeof(buffer))) {
        return;
    }
    while (buffer[i] != '\0' && g_np_open_count < GRAPHICS_NOTEPAD_OPEN_MAX) {
        uint32_t ls = i, ll = 0;
        while (buffer[i] != '\0' && buffer[i] != '\n') { i++; }
        ll = i - ls;
        if (ll == 0u) { continue; }
        bool isdir = (i > ls) && (buffer[i - 1] == '\\' || buffer[i - 1] == '/');
        if (isdir) { ll--; }
        if (ll >= GRAPHICS_FILE_NAME_MAX) { ll = GRAPHICS_FILE_NAME_MAX - 1u; }
        memcpy(g_np_open_names[g_np_open_count], &buffer[ls], ll);
        g_np_open_names[g_np_open_count][ll] = '\0';
        g_np_open_isdir[g_np_open_count] = isdir;
        g_np_open_count++;
    }
}

static void np_cursor_reveal(np_tab_t *t, uint32_t vrows)
{
    uint32_t line, cc;
    np_offset_linecol(t, t->cursor, &line, &cc);
    if (line < t->scroll_row) {
        t->scroll_row = line;
    } else if (line >= t->scroll_row + vrows) {
        t->scroll_row = line - vrows + 1u;
    }
}

static void graphics_notepad_render(const ui_window_t *window)
{
    np_tab_t *t = np_active_tab();
    uint16_t bx = (uint16_t)(window->x + 8);
    uint16_t by = (uint16_t)(window->y + 24);
    uint16_t ty = (uint16_t)(window->y + 44);
    uint16_t ax = bx;
    uint16_t ay = (uint16_t)(window->y + 66);
    uint16_t aw = (uint16_t)(window->width - 16);
    uint16_t ah = (uint16_t)(window->height - 72);
    uint32_t vrows = ah / UI_FONT_HEIGHT;

    /* tab bar */
    graphics_fill_rect(bx, by, (uint16_t)(window->width - 16), 20, 0x00DDE5EEu);
    for (int32_t i = 0; i < GRAPHICS_NOTEPAD_TABS_MAX; i++) {
        if (!g_np_tabs[i].used) { continue; }
        uint16_t tx = (uint16_t)(bx + i * GRAPHICS_NOTEPAD_TAB_TITLE_W);
        bool active = (i == g_np_active);
        const char *base = g_np_tabs[i].path;
        const char *slash = base;
        char label[40];
        uint32_t j = 0;
        for (uint32_t k = 0; base[k] != '\0'; k++) {
            if (base[k] == '\\' || base[k] == '/') { slash = base + k + 1; }
        }
        while (slash[j] != '\0' && j < 30u) { label[j] = slash[j]; j++; }
        if (g_np_tabs[i].modified) { label[j++] = '*'; }
        label[j] = '\0';
        graphics_fill_rect(tx, by, (uint16_t)(GRAPHICS_NOTEPAD_TAB_TITLE_W - 2), 20,
                           active ? 0x001E1E2Eu : 0x00B8C4D2u);
        graphics_draw_rect_outline(tx, by, (uint16_t)(GRAPHICS_NOTEPAD_TAB_TITLE_W - 2), 20, 0x008AA1B8u);
        graphics_draw_text((uint16_t)(tx + 6), (uint16_t)(by + 4), label,
                           active ? 0x00E6EDF3u : 0x0022303Eu);
    }
    /* toolbar */
    graphics_fill_rect(bx, ty, (uint16_t)(window->width - 16), 18, 0x00F0F4F8u);
    graphics_draw_text((uint16_t)(bx + 6), (uint16_t)(ty + 2),
                       "Ctrl+S Save  Ctrl+F Find  Ctrl+H Replace  Ctrl+O Open  Ctrl+N New  Ctrl+W Close",
                       0x004A6278u);
    /* text area */
    graphics_fill_rect(ax, ay, aw, ah, NP_COLOR_BG);
    graphics_draw_rect_outline(ax, ay, aw, ah, 0x008AA1B8u);
    if (t == NULL) { return; }

    np_rehighlight(t);
    np_cursor_reveal(t, vrows);
    {
        uint32_t lc = np_line_count(t);
        uint16_t right = (uint16_t)(ax + aw - 10);
        uint32_t clen = t->len;
        const char *s = t->text;
        uint32_t cur_line, cur_col;
        np_offset_linecol(t, t->cursor, &cur_line, &cur_col);

        for (uint32_t r = 0; r < vrows; r++) {
            uint32_t line = t->scroll_row + r;
            uint32_t ls, le, p;
            uint16_t dy, dx;
            if (line >= lc) { break; }
            ls = np_line_start(t, line);
            le = np_line_end(t, line);
            dy = (uint16_t)(ay + 4 + r * UI_FONT_HEIGHT);
            if (line == cur_line) {
                graphics_fill_rect((uint16_t)(ax + 2), dy, (uint16_t)(aw - 4), UI_FONT_HEIGHT, NP_COLOR_LINEHL);
            }
            dx = (uint16_t)(ax + 6);
            p = ls;
            while (p < le) {
                const char *pp = s + p;
                uint32_t cp = font_utf8_next(&pp);
                uint32_t adv = font_codepoint_advance(cp);
                if ((uint32_t)dx + adv >= (uint32_t)right) { break; }
                if (g_np_find_valid && p >= g_np_find_match_start &&
                    p < g_np_find_match_start + g_np_find_match_len) {
                    graphics_fill_rect(dx, (uint16_t)(dy + 1), (uint16_t)adv, (uint16_t)(UI_FONT_HEIGHT - 2), 0x005A3A9Fu);
                }
                graphics_draw_codepoint(dx, dy, cp, (p < clen) ? g_np_colors[p] : NP_COLOR_DEFAULT);
                dx = (uint16_t)(dx + adv);
                p = (uint32_t)(pp - s);
            }
            if (line == cur_line) {
                uint16_t cx = (uint16_t)(ax + 6);
                uint32_t p2 = ls;
                while (p2 < t->cursor && p2 < le) {
                    const char *pp = s + p2;
                    uint32_t cp = font_utf8_next(&pp);
                    cx = (uint16_t)(cx + font_codepoint_advance(cp));
                    p2 = (uint32_t)(pp - s);
                }
                graphics_fill_rect(cx, dy, 2, UI_FONT_HEIGHT, 0x00E6EDF3u);
            }
        }
    }
    /* dialog overlays */
    if (g_np_dlg != NP_DLG_NONE) {
        uint16_t dw = 300;
        uint16_t dh = (g_np_dlg == NP_DLG_OPEN) ? 200u : 90u;
        uint16_t dx = (uint16_t)(window->x + (window->width - dw) / 2u);
        uint16_t dyy = (uint16_t)(window->y + (window->height - dh) / 2u);
        graphics_fill_rect(dx, dyy, dw, dh, 0x00F0F4F9u);
        graphics_draw_rect_outline(dx, dyy, dw, dh, 0x003C6FEAu);
        if (g_np_dlg == NP_DLG_FIND) {
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 8), "Find:", 0x0017232Eu);
            graphics_fill_rect((uint16_t)(dx + 60), (uint16_t)(dyy + 6), 220, 20, 0x00FFFFFFu);
            graphics_draw_rect_outline((uint16_t)(dx + 60), (uint16_t)(dyy + 6), 220, 20, 0x003C6FEAu);
            graphics_draw_text((uint16_t)(dx + 64), (uint16_t)(dyy + 9), g_np_find, 0x0017232Eu);
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 36), "Enter=next  F3=again  Esc=close", 0x004A6278u);
        } else if (g_np_dlg == NP_DLG_REPLACE) {
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 8), "Find:", 0x0017232Eu);
            graphics_fill_rect((uint16_t)(dx + 60), (uint16_t)(dyy + 6), 220, 18, 0x00FFFFFFu);
            graphics_draw_text((uint16_t)(dx + 64), (uint16_t)(dyy + 9), g_np_find, 0x0017232Eu);
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 32), "Rep:", 0x0017232Eu);
            graphics_fill_rect((uint16_t)(dx + 60), (uint16_t)(dyy + 30), 220, 18, 0x00FFFFFFu);
            graphics_draw_text((uint16_t)(dx + 64), (uint16_t)(dyy + 33), g_np_replace, 0x0017232Eu);
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 58), "Tab=switch  Ctrl+R=replace  Ctrl+A=all", 0x004A6278u);
        } else {
            uint32_t i;
            graphics_draw_text((uint16_t)(dx + 10), (uint16_t)(dyy + 6), g_np_open_dir, 0x0017232Eu);
            for (i = 0; i < g_np_open_count && i < 10u; i++) {
                uint16_t ry = (uint16_t)(dyy + 26 + i * 16);
                if (i == g_np_open_sel) {
                    graphics_fill_rect((uint16_t)(dx + 6), ry, (uint16_t)(dw - 12), 14, 0x00D8E9FAu);
                }
                graphics_draw_text((uint16_t)(dx + 10), ry, g_np_open_names[i], 0x0017232Eu);
            }
        }
    }
}

static void graphics_notepad_key(const key_event_t *event)
{
    np_tab_t *t = np_active_tab();

    if (g_np_dlg != NP_DLG_NONE) {
        if (event->type == KEY_EVENT_ESC) { g_np_dlg = NP_DLG_NONE; return; }
        if (g_np_dlg == NP_DLG_OPEN) {
            if (event->type == KEY_EVENT_DOWN) { if (g_np_open_sel + 1u < g_np_open_count) g_np_open_sel++; return; }
            if (event->type == KEY_EVENT_UP)   { if (g_np_open_sel > 0u) g_np_open_sel--; return; }
            if (event->type == KEY_EVENT_CHAR && event->ch == '\n') {
                if (g_np_open_sel < g_np_open_count) {
                    char full[GRAPHICS_CLIPBOARD_PATH_MAX];
                    strcpy(full, g_np_open_dir);
                    graphics_append_path_component(full, sizeof(full), g_np_open_names[g_np_open_sel]);
                    if (g_np_open_isdir[g_np_open_sel]) {
                        if (file_is_dir(full)) { strcpy(g_np_open_dir, full); np_open_refresh(); }
                    } else {
                        int32_t idx = np_free_tab();
                        if (idx >= 0) {
                            np_tab_t *nt = &g_np_tabs[idx];
                            np_tab_reset(nt);
                            if (np_load_into(nt, full)) {
                                g_np_active = idx;
                                g_np_dlg = NP_DLG_NONE;
                            }
                        }
                    }
                }
                return;
            }
            return;
        }
        /* find / replace field editing */
        if (event->type == KEY_EVENT_CHAR) {
            char *buf = g_np_dlg_replace_field ? g_np_replace : g_np_find;
            uint32_t l = strlen(buf);
            if (event->ch == '\b') {
                if (g_np_dlg_pos > 0u) {
                    memmove(buf + g_np_dlg_pos - 1u, buf + g_np_dlg_pos, l - g_np_dlg_pos + 1u);
                    g_np_dlg_pos--;
                }
                return;
            }
            if (event->ch == '\t') { g_np_dlg_replace_field = !g_np_dlg_replace_field; return; }
            if (event->ch == '\n') {
                g_np_find_next = 0u; g_np_find_valid = true;
                np_do_find_next(np_active_tab());
                return;
            }
            if (event->ch >= 32 && event->ch <= 126 && l + 1u < GRAPHICS_NOTEPAD_DLG_BUF) {
                memmove(buf + g_np_dlg_pos + 1u, buf + g_np_dlg_pos, l - g_np_dlg_pos + 1u);
                buf[g_np_dlg_pos] = event->ch;
                g_np_dlg_pos++;
                buf[l + 1u] = '\0';
            }
        }
        return;
    }

    if (t == NULL) { return; }

    if (event->status.ctrl_down) {
        char c = event->ch;
        if (c >= 'A' && c <= 'Z') { c = (char)(c + 32); }
        switch (c) {
        case 'n': {
            const char *dir = UI_ROOT_DESKTOP;
            if (t->path[0] != '\0') {
                char tmp[GRAPHICS_CLIPBOARD_PATH_MAX];
                char *sl;
                strcpy(tmp, t->path);
                sl = strrchr(tmp, '\\');
                if (sl != NULL) { *sl = '\0'; dir = tmp; }
            }
            (void)np_open_blank(dir);
            return;
        }
        case 'o': {
            strcpy(g_np_open_dir, UI_ROOT_DESKTOP);
            if (t->path[0] != '\0') {
                char tmp[GRAPHICS_CLIPBOARD_PATH_MAX];
                char *sl;
                strcpy(tmp, t->path);
                sl = strrchr(tmp, '\\');
                if (sl != NULL) { *sl = '\0'; strcpy(g_np_open_dir, tmp); }
            }
            np_open_refresh();
            g_np_dlg = NP_DLG_OPEN;
            return;
        }
        case 's': np_save(t); return;
        case 'f':
            g_np_dlg = NP_DLG_FIND; g_np_dlg_replace_field = false;
            g_np_dlg_pos = strlen(g_np_find);
            return;
        case 'h':
            g_np_dlg = NP_DLG_REPLACE; g_np_dlg_replace_field = false;
            g_np_dlg_pos = strlen(g_np_find);
            return;
        case 'w':
            if (t->modified) { np_save(t); }
            t->used = false;
            g_np_active = -1;
            for (int32_t i = 0; i < GRAPHICS_NOTEPAD_TABS_MAX; i++) {
                if (g_np_tabs[i].used) { g_np_active = i; break; }
            }
            return;
        case 'r': { /* replace current match */
            if (g_np_find_valid) {
                uint32_t ml = g_np_find_match_len, rl = strlen(g_np_replace);
                int32_t delta = (int32_t)rl - (int32_t)ml;
                if ((int32_t)t->len + delta >= 0 && t->len + (uint32_t)delta < GRAPHICS_NOTEPAD_TEXT_MAX) {
                    memmove(t->text + g_np_find_match_start + rl,
                            t->text + g_np_find_match_start + ml,
                            t->len - g_np_find_match_start - ml);
                    memcpy(t->text + g_np_find_match_start, g_np_replace, rl);
                    t->len = (uint32_t)((int32_t)t->len + delta);
                    t->text[t->len] = '\0';
                    t->cursor = g_np_find_match_start + rl;
                    t->modified = true;
                    g_np_find_next = t->cursor;
                    np_do_find_next(t);
                }
            }
            return;
        }
        case 'a': { /* replace all */
            g_np_find_next = 0u; g_np_find_valid = true;
            while (np_find_from(t, g_np_find, g_np_find_next, &g_np_find_match_start, &g_np_find_match_len)) {
                uint32_t ml = g_np_find_match_len, rl = strlen(g_np_replace);
                int32_t delta = (int32_t)rl - (int32_t)ml;
                if ((int32_t)t->len + delta < 0 || t->len + (uint32_t)delta >= GRAPHICS_NOTEPAD_TEXT_MAX) { break; }
                memmove(t->text + g_np_find_match_start + rl,
                        t->text + g_np_find_match_start + ml,
                        t->len - g_np_find_match_start - ml);
                memcpy(t->text + g_np_find_match_start, g_np_replace, rl);
                t->len = (uint32_t)((int32_t)t->len + delta);
                t->text[t->len] = '\0';
                g_np_find_next = g_np_find_match_start + rl;
                t->modified = true;
            }
            g_np_find_valid = false;
            return;
        }
        case 'c':   /* Ctrl+C: copy whole document to global text clipboard */
            graphics_clipboard_set_text(t->text);
            graphics_notification_post("Notepad", "Copied to clipboard");
            return;
        case 'x':   /* Ctrl+X: cut */
            graphics_clipboard_set_text(t->text);
            t->text[0] = '\0';
            t->len = 0;
            t->cursor = 0;
            t->modified = true;
            return;
        case 'v': { /* Ctrl+V: paste clipboard text at caret */
            char pb[GRAPHICS_CLIPBOARD_PATH_MAX];
            uint32_t pl = graphics_clipboard_get_text(pb, sizeof(pb));
            for (uint32_t k = 0U; k < pl; k++) {
                np_insert_at(t, t->cursor, pb[k]);
            }
            return;
        }
        default: break;
        }
    }

    if (event->type == KEY_EVENT_TAB) {
        for (int32_t step = 1; step <= GRAPHICS_NOTEPAD_TABS_MAX; step++) {
            int32_t nx = (g_np_active + step) % GRAPHICS_NOTEPAD_TABS_MAX;
            if (g_np_tabs[nx].used) { g_np_active = nx; break; }
        }
        return;
    }
    if (event->type == KEY_EVENT_F3) { np_do_find_next(t); return; }
    if (event->type == KEY_EVENT_LEFT)  { np_cursor_left(t); return; }
    if (event->type == KEY_EVENT_RIGHT) { np_cursor_right(t); return; }
    if (event->type == KEY_EVENT_UP)    { np_cursor_up(t); return; }
    if (event->type == KEY_EVENT_DOWN)  { np_cursor_down(t); return; }
    if (event->type == KEY_EVENT_HOME)  { np_cursor_home(t); return; }
    if (event->type == KEY_EVENT_END)   { np_cursor_end(t); return; }
    if (event->type == KEY_EVENT_DELETE){ np_erase_at(t, t->cursor); return; }
    if (event->type == KEY_EVENT_CHAR) {
        if (event->ch == '\b') { if (t->cursor > 0u) np_erase_at(t, t->cursor - 1u); return; }
        if (event->ch == '\n') { np_insert_at(t, t->cursor, '\n'); return; }
        if (event->ch >= 32 && event->ch <= 126) { np_insert_at(t, t->cursor, event->ch); return; }
    }
}

static void graphics_notepad_click_tab(const ui_window_t *window, uint16_t x, uint16_t y, uint32_t windex)
{
    uint16_t bx = (uint16_t)(window->x + 8);
    uint16_t by = (uint16_t)(window->y + 24);
    for (int32_t i = 0; i < GRAPHICS_NOTEPAD_TABS_MAX; i++) {
        if (!g_np_tabs[i].used) { continue; }
        if (graphics_point_in_rect(x, y, (uint16_t)(bx + i * GRAPHICS_NOTEPAD_TAB_TITLE_W), by,
                                   (uint16_t)(GRAPHICS_NOTEPAD_TAB_TITLE_W - 2), 20)) {
            g_np_active = i;
            g_notepad_focus = true;
            graphics_set_terminal_focus(false);
            g_run_input_focus = false;
            graphics_bring_window_to_front(windex);
            graphics_draw_shell();
            return;
        }
    }
}

/* ==================== Terminal multi-tab manager ==================== */
#define TTY_TABS_MAX 4
typedef struct {
    bool used;
    uint32_t cells[CONSOLE_ROWS * CONSOLE_COLUMNS];
    uint16_t row;
    uint16_t col;
} tty_tab_t;

static tty_tab_t g_tty_tabs[TTY_TABS_MAX] = { { .used = true } };
static int32_t g_tty_active;

static void tty_tab_save_current(void)
{
    if (g_tty_active < 0 || g_tty_active >= TTY_TABS_MAX) {
        return;
    }
    g_tty_tabs[g_tty_active].used = true;
    console_snapshot_take(g_tty_tabs[g_tty_active].cells,
                          &g_tty_tabs[g_tty_active].row,
                          &g_tty_tabs[g_tty_active].col);
}

static void tty_tab_restore_current(void)
{
    if (g_tty_active < 0 || g_tty_active >= TTY_TABS_MAX || !g_tty_tabs[g_tty_active].used) {
        return;
    }
    console_snapshot_restore(g_tty_tabs[g_tty_active].cells,
                             g_tty_tabs[g_tty_active].row,
                             g_tty_tabs[g_tty_active].col);
}

void tty_tab_new(void)
{
    int32_t idx;

    tty_tab_save_current();
    idx = -1;
    for (int32_t i = 0; i < TTY_TABS_MAX; i++) {
        if (!g_tty_tabs[i].used) { idx = i; break; }
    }
    if (idx < 0) {
        return;
    }
    g_tty_active = idx;
    g_tty_tabs[idx].used = true;
    console_clear();
}

void tty_tab_next(void)
{
    tty_tab_save_current();
    for (int32_t step = 1; step <= TTY_TABS_MAX; step++) {
        int32_t nx = (g_tty_active + step) % TTY_TABS_MAX;
        if (g_tty_tabs[nx].used) { g_tty_active = nx; break; }
    }
    tty_tab_restore_current();
}

void tty_tab_close(void)
{
    if (g_tty_active < 0) {
        return;
    }
    g_tty_tabs[g_tty_active].used = false;
    g_tty_active = -1;
    for (int32_t i = 0; i < TTY_TABS_MAX; i++) {
        if (g_tty_tabs[i].used) { g_tty_active = i; break; }
    }
    if (g_tty_active >= 0) {
        tty_tab_restore_current();
    } else {
        g_tty_active = 0;
        g_tty_tabs[0].used = true;
        console_clear();
    }
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
    io_wait();
    outw(BGA_DATA_PORT, value);
    io_wait();
}

static uint16_t bga_read(uint16_t index)
{
    outw(BGA_INDEX_PORT, index);
    io_wait();
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

/* Filled circle (scanline midpoint-circle). */
static void graphics_fill_disc(uint16_t cx, uint16_t cy, uint16_t r, uint32_t color)
{
    if (r == 0u) { graphics_plot(cx, cy, color); return; }
    int rr = (int) r;
    int rr2 = rr * rr;
    for (int dy = -rr; dy <= rr; dy++) {
        int rem = rr2 - dy * dy;
        int half = 0;
        int row = (int) cy + dy;
        if (rem < 0 || row < 0) continue;
        while ((half + 1) * (half + 1) <= rem) half++;
        graphics_fill_rect((uint16_t) (cx - half), (uint16_t) row, (uint16_t) (2 * half + 1), 1, color);
    }
}

/* WinUI 3 rounded rectangle: corners traced with a midpoint-circle arc. */
static void graphics_fill_rounded_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color, uint16_t radius)
{
    if (w == 0 || h == 0) return;
    if ((uint32_t) radius * 2u > w) radius = (uint16_t) (w / 2u);
    if ((uint32_t) radius * 2u > h) radius = (uint16_t) (h / 2u);
    if (radius < 2u) { graphics_fill_rect(x, y, w, h, color); return; }
    graphics_fill_rect(x, (uint16_t) (y + radius), w, (uint16_t) (h - 2u * radius), color);
    graphics_fill_rect((uint16_t) (x + radius), y, (uint16_t) (w - 2u * radius), radius, color);
    graphics_fill_rect((uint16_t) (x + radius), (uint16_t) (y + h - radius), (uint16_t) (w - 2u * radius), radius, color);
    {
        int r = (int) radius;
        int r2 = r * r;
        for (int dy = 0; dy < r; dy++) {
            int dcy = r - dy;
            int rem = r2 - dcy * dcy;
            int half = 0;
            uint16_t rt, rb, lx;
            if (rem < 0) rem = 0;
            while ((half + 1) * (half + 1) <= rem) half++;
            rt = (uint16_t) (y + dy);
            rb = (uint16_t) (y + h - 1u - dy);
            lx = (uint16_t) (x + r - half);
            graphics_fill_rect(lx, rt, (uint16_t) half, 1, color);
            graphics_fill_rect((uint16_t) (x + w - r), rt, (uint16_t) half, 1, color);
            graphics_fill_rect(lx, rb, (uint16_t) half, 1, color);
            graphics_fill_rect((uint16_t) (x + w - r), rb, (uint16_t) half, 1, color);
        }
    }
}

static void graphics_draw_rounded_rect_outline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color, uint16_t radius)
{
    if (w < 2 || h < 2) return;
    if ((uint32_t) radius * 2u > w) radius = (uint16_t) (w / 2u);
    if ((uint32_t) radius * 2u > h) radius = (uint16_t) (h / 2u);
    if (radius < 1u) { graphics_draw_rect_outline(x, y, w, h, color); return; }
    for (uint16_t col = radius; col + radius < w; col++) {
        graphics_plot((uint16_t) (x + col), y, color);
        graphics_plot((uint16_t) (x + col), (uint16_t) (y + h - 1u), color);
    }
    for (uint16_t row = radius; row + radius < h; row++) {
        graphics_plot(x, (uint16_t) (y + row), color);
        graphics_plot((uint16_t) (x + w - 1u), (uint16_t) (y + row), color);
    }
    {
        int r = (int) radius;
        int r2 = r * r;
        int cxl = r, cxr = (int) w - 1 - r;
        int cyb = (int) h - 1 - r;
        for (int dy = 0; dy <= r; dy++) {
            int dcy = r - dy;
            int rem = r2 - dcy * dcy;
            int half = 0;
            if (rem < 0) rem = 0;
            while ((half + 1) * (half + 1) <= rem) half++;
            graphics_plot((uint16_t) (x + cxl - half), (uint16_t) (y + dy), color);
            graphics_plot((uint16_t) (x + cxr + half), (uint16_t) (y + dy), color);
            graphics_plot((uint16_t) (x + cxl - half), (uint16_t) (y + cyb + dcy), color);
            graphics_plot((uint16_t) (x + cxr + half), (uint16_t) (y + cyb + dcy), color);
        }
    }
}

static void graphics_fill_soft_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    /* WinUI 3 default: 8px rounded corners. */
    graphics_fill_rounded_rect(x, y, width, height, color, 8u);
}

static void graphics_draw_soft_rect_outline(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint32_t color)
{
    graphics_draw_rounded_rect_outline(x, y, width, height, color, 8u);
}

/* ---- Win11-style rendering helpers: alpha blending + PNG icon blit ---- */
static uint32_t graphics_blend_rgb(uint32_t fg, uint32_t bg, uint32_t alpha)
{
    uint32_t sr = (fg >> 16) & 0xFFu;
    uint32_t sg = (fg >> 8) & 0xFFu;
    uint32_t sb = fg & 0xFFu;
    uint32_t dr = (bg >> 16) & 0xFFu;
    uint32_t dg = (bg >> 8) & 0xFFu;
    uint32_t db = bg & 0xFFu;
    uint32_t ia = 255u - alpha;
    uint32_t ro = (sr * alpha + dr * ia) / 255u;
    uint32_t go = (sg * alpha + dg * ia) / 255u;
    uint32_t bo = (sb * alpha + db * ia) / 255u;
    return ((ro & 0xFFu) << 16) | ((go & 0xFFu) << 8) | (bo & 0xFFu);
}

static void graphics_fill_rect_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                     uint32_t fg, uint32_t alpha)
{
    uint16_t rw = width, rh = height, rx = x, ry = y;
    if (!g_graphics_active || rw == 0 || rh == 0) return;
    if (rx >= FB_WIDTH || ry >= FB_HEIGHT) return;
    if ((uint32_t)rx + rw > FB_WIDTH) rw = (uint16_t)(FB_WIDTH - rx);
    if ((uint32_t)ry + rh > FB_HEIGHT) rh = (uint16_t)(FB_HEIGHT - ry);
    for (uint16_t row = 0; row < rh; row++) {
        for (uint16_t col = 0; col < rw; col++) {
            uint64_t di = (uint64_t)(ry + row) * FB_WIDTH + (rx + col);
            g_backbuffer[di] = graphics_blend_rgb(fg, g_backbuffer[di], alpha);
        }
    }
}

static void graphics_fill_rounded_rect_alpha(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                             uint32_t fg, uint32_t alpha, uint16_t radius)
{
    if (w == 0 || h == 0) return;
    if ((uint32_t) radius * 2u > w) radius = (uint16_t) (w / 2u);
    if ((uint32_t) radius * 2u > h) radius = (uint16_t) (h / 2u);
    if (radius < 2u) { graphics_fill_rect_alpha(x, y, w, h, fg, alpha); return; }
    graphics_fill_rect_alpha(x, (uint16_t) (y + radius), w, (uint16_t) (h - 2u * radius), fg, alpha);
    graphics_fill_rect_alpha((uint16_t) (x + radius), y, (uint16_t) (w - 2u * radius), radius, fg, alpha);
    graphics_fill_rect_alpha((uint16_t) (x + radius), (uint16_t) (y + h - radius), (uint16_t) (w - 2u * radius), radius, fg, alpha);
    {
        int r = (int) radius;
        int r2 = r * r;
        for (int dy = 0; dy < r; dy++) {
            int dcy = r - dy;
            int rem = r2 - dcy * dcy;
            int half = 0;
            uint16_t rt, rb, lx;
            if (rem < 0) rem = 0;
            while ((half + 1) * (half + 1) <= rem) half++;
            rt = (uint16_t) (y + dy);
            rb = (uint16_t) (y + h - 1u - dy);
            lx = (uint16_t) (x + r - half);
            graphics_fill_rect_alpha(lx, rt, (uint16_t) half, 1, fg, alpha);
            graphics_fill_rect_alpha((uint16_t) (x + w - r), rt, (uint16_t) half, 1, fg, alpha);
            graphics_fill_rect_alpha(lx, rb, (uint16_t) half, 1, fg, alpha);
            graphics_fill_rect_alpha((uint16_t) (x + w - r), rb, (uint16_t) half, 1, fg, alpha);
        }
    }
}

static void graphics_fill_soft_rect_alpha(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                          uint32_t fg, uint32_t alpha)
{
    graphics_fill_rounded_rect_alpha(x, y, width, height, fg, alpha, 8u);
}

static void graphics_blit_icon(uint32_t icon_id, uint16_t x, uint16_t y, uint16_t size)
{
    uint16_t w, h, rx, ry;
    if (!g_graphics_active || icon_id >= MONIOS_ICON_COUNT || size == 0) return;
    /* Source canvas is exactly MONIOS_ICON_SIZE x MONIOS_ICON_SIZE (64x64).
     * Cap any over-size request so we never index past the icon bitmap. */
    if (size > MONIOS_ICON_SIZE) size = MONIOS_ICON_SIZE;
    w = size; h = size;
    rx = x; ry = y;
    if (rx >= FB_WIDTH || ry >= FB_HEIGHT) return;
    if ((uint32_t)rx + w > FB_WIDTH) w = (uint16_t)(FB_WIDTH - rx);
    if ((uint32_t)ry + h > FB_HEIGHT) h = (uint16_t)(FB_HEIGHT - ry);
    if (w == 0 || h == 0) return;
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint16_t sx, sy;
            uint32_t spx, sa;
            uint64_t di;
            /* Edge-complete nearest-neighbour: destination [0,size-1] maps onto
             * source [0,63] inclusive. Dividing by (size-1) (and guarding
             * size==1) guarantees both the first and last source rows/columns
             * are sampled for every size (16/24/32/48/64). The old
             * `col*64/size` truncation never sampled source row/col 63 for
             * non-divisor sizes (24/48), cropping the icon's right/bottom edge. */
            if (size <= 1u) {
                sx = 0u;
                sy = 0u;
            } else {
                sx = (uint16_t)(((uint32_t)col * (MONIOS_ICON_SIZE - 1u)) / (uint32_t)(size - 1u));
                sy = (uint16_t)(((uint32_t)row * (MONIOS_ICON_SIZE - 1u)) / (uint32_t)(size - 1u));
            }
            if (sx >= MONIOS_ICON_SIZE) sx = MONIOS_ICON_SIZE - 1u;
            if (sy >= MONIOS_ICON_SIZE) sy = MONIOS_ICON_SIZE - 1u;
            memcpy(&spx, &g_icon_data[icon_id][((uint32_t)sy * MONIOS_ICON_SIZE + sx) * 4u], sizeof(spx));
            sa = (spx >> 24) & 0xFFu;
            if (sa == 0u) continue;
            di = (uint64_t)(ry + row) * FB_WIDTH + (rx + col);
            if (di >= (uint64_t)FB_WIDTH * FB_HEIGHT) continue;
            if (sa == 0xFFu) {
                g_backbuffer[di] = spx & 0x00FFFFFFu;
                continue;
            }
            {
                uint32_t sr = (spx >> 16) & 0xFFu;
                uint32_t sg = (spx >> 8) & 0xFFu;
                uint32_t sb = spx & 0xFFu;
                uint32_t dpx = g_backbuffer[di];
                uint32_t dr = (dpx >> 16) & 0xFFu;
                uint32_t dg = (dpx >> 8) & 0xFFu;
                uint32_t db = dpx & 0xFFu;
                uint32_t ia = 255u - sa;
                uint32_t ro = (sr * sa + dr * ia) / 255u;
                uint32_t go = (sg * sa + dg * ia) / 255u;
                uint32_t bo = (sb * sa + db * ia) / 255u;
                g_backbuffer[di] = ((ro & 0xFFu) << 16) | ((go & 0xFFu) << 8) | (bo & 0xFFu);
            }
        }
    }
}

static void graphics_draw_shadow(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    /* WinUI 3 soft drop shadow: layered translucent black, offset +4y,
     * widening spread with falling alpha to fake a Gaussian blur. */
    static const struct { int dx; int dy; int spread; uint8_t alpha; } sh[] = {
        { 0, 1, 0, 28u },
        { 0, 2, 2, 18u },
        { 0, 3, 4, 12u },
        { 0, 5, 6, 8u  },
        { 0, 7, 9, 5u  },
    };
    for (uint32_t i = 0; i < 5u; i++) {
        int lx = (int) x - sh[i].spread + sh[i].dx;
        int ly = (int) y - sh[i].spread + sh[i].dy;
        int lw = (int) width + 2 * sh[i].spread;
        int lh = (int) height + 2 * sh[i].spread;
        if (lx < 0) lx = 0;
        if (ly < 0) ly = 0;
        if (lw <= 0 || lh <= 0) continue;
        graphics_fill_rounded_rect_alpha((uint16_t) lx, (uint16_t) ly, (uint16_t) lw, (uint16_t) lh,
                                         0x00000000u, (uint32_t) sh[i].alpha, 8u);
    }
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


uint32_t graphics_user_read_framebuffer(void *user_dst, uint16_t x, uint16_t y,
                                        uint16_t width, uint16_t height)
{
    uint16_t w;
    uint16_t h;
    uint16_t rx;
    uint16_t ry;
    uint32_t row;

    if (!g_graphics_active || g_backbuffer == NULL || user_dst == NULL) {
        return 0;
    }
    if (x >= FB_WIDTH || y >= FB_HEIGHT) {
        return 0;
    }
    w = width;
    h = height;
    if ((uint32_t)x + w > FB_WIDTH) {
        w = (uint16_t)(FB_WIDTH - x);
    }
    if ((uint32_t)y + h > FB_HEIGHT) {
        h = (uint16_t)(FB_HEIGHT - y);
    }
    if (w == 0 || h == 0) {
        return 0;
    }
    rx = x;
    ry = y;
    for (row = 0; row < h; row++) {
        const uint32_t *src = &g_backbuffer[((uint64_t)(ry + row) * FB_WIDTH) + rx];
        uint8_t *dst = (uint8_t *)user_dst + (uint64_t)row * w * 4u;
        uint16_t col;

        for (col = 0; col < w; col++) {
            uint32_t px = src[col];
            dst[(uint32_t)col * 4u + 0u] = (uint8_t)(px & 0xFFu);
            dst[(uint32_t)col * 4u + 1u] = (uint8_t)((px >> 8) & 0xFFu);
            dst[(uint32_t)col * 4u + 2u] = (uint8_t)((px >> 16) & 0xFFu);
            dst[(uint32_t)col * 4u + 3u] = 0xFFu;
        }
    }
    return (uint32_t)w * (uint32_t)h * 4u;
}

/* Blit a caller-supplied pixel buffer into the backbuffer.
 *
 * `src` points to a kernel-safe (already copied out of user space) array of
 * `width`*`height` pixels, stored little-endian in the native framebuffer
 * layout: a uint32 pixel is 0xAARRGGBB (memory bytes B,G,R,A). Coordinates are
 * clipped to the visible screen. When bit0 of `flags` is set the pixels are
 * blended source-over onto the existing backbuffer content (integer math);
 * otherwise they overwrite opaquely.
 *
 * Returns the number of pixels actually written (after clipping). */
uint32_t graphics_user_blit(const void *src, uint16_t x, uint16_t y,
                            uint16_t width, uint16_t height, uint32_t flags)
{
    uint16_t w;
    uint16_t h;
    uint16_t rx;
    uint16_t ry;
    uint16_t row;
    bool use_alpha;

    if (!g_graphics_active || g_backbuffer == NULL || src == NULL) {
        return 0;
    }
    if (x >= FB_WIDTH || y >= FB_HEIGHT || width == 0 || height == 0) {
        return 0;
    }
    w = width;
    h = height;
    if ((uint32_t)x + w > FB_WIDTH) {
        w = (uint16_t)(FB_WIDTH - x);
    }
    if ((uint32_t)y + h > FB_HEIGHT) {
        h = (uint16_t)(FB_HEIGHT - y);
    }
    if (w == 0 || h == 0) {
        return 0;
    }
    rx = x;
    ry = y;
    use_alpha = (flags & 0x1u) != 0u;

    for (row = 0; row < h; row++) {
        const uint32_t *src_row = (const uint32_t *)src + (uint64_t)row * width;
        uint32_t *dst_row = &g_backbuffer[((uint64_t)(ry + row) * FB_WIDTH) + rx];
        uint16_t col;

        for (col = 0; col < w; col++) {
            uint32_t spx = src_row[col];

            if (!use_alpha) {
                /* Opaque overwrite: drop the source alpha byte, write RGB. */
                dst_row[col] = spx & 0x00FFFFFFu;
                continue;
            }
            {
                uint32_t sa = (spx >> 24) & 0xFFu;

                if (sa == 0u) {
                    /* Fully transparent: leave destination untouched. */
                    continue;
                }
                if (sa == 0xFFu) {
                    dst_row[col] = spx & 0x00FFFFFFu;
                    continue;
                }
                {
                    uint32_t sr = (spx >> 16) & 0xFFu;
                    uint32_t sg = (spx >> 8) & 0xFFu;
                    uint32_t sb = spx & 0xFFu;
                    uint32_t dpx = dst_row[col];
                    uint32_t dr = (dpx >> 16) & 0xFFu;
                    uint32_t dg = (dpx >> 8) & 0xFFu;
                    uint32_t db = dpx & 0xFFu;
                    uint32_t ia = 255u - sa;
                    uint32_t out_r = (sr * sa + dr * ia) / 255u;
                    uint32_t out_g = (sg * sa + dg * ia) / 255u;
                    uint32_t out_b = (sb * sa + db * ia) / 255u;

                    dst_row[col] = ((out_r & 0xFFu) << 16) |
                                   ((out_g & 0xFFu) << 8) |
                                   (out_b & 0xFFu);
                }
            }
        }
    }
    return (uint32_t)w * (uint32_t)h;
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

static bool g_ux_icon_flat = false;
static bool g_ux_font_aa = true;
static bool g_ux_anim = true;

static void graphics_draw_codepoint(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color)
{
    /* Font smoothing: when anti-aliasing is enabled, first lay down a faint
     * offset copy of the glyph to soften the hard thresholded edges produced
     * by the core font rasteriser, then draw the crisp glyph on top. */
    if (g_ux_font_aa && codepoint >= 0x20U) {
        uint32_t soft = graphics_lerp_color(color, 0x00FFFFFFU, 35U, 100U);
        font_draw_codepoint(x, (uint16_t) (y + 1U), codepoint, soft, graphics_plot);
    }
    font_draw_codepoint(x, y, codepoint, color, graphics_plot);
}

static void __attribute__((unused)) graphics_draw_char(uint16_t x, uint16_t y, char ch, uint32_t color)
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
    memset(&g_icon_cache[slot], 0, sizeof(g_icon_cache[slot]));
    g_icon_cache[slot].valid = true;
    strlcpy(g_icon_cache[slot].path, path, sizeof(g_icon_cache[slot].path));
    g_icon_cache[slot].image_flags = flags;
    if ((flags & EXEC_IMAGE_FLAG_ICON_RESOURCE) != 0) {
        g_icon_cache[slot].bitmap_valid =
            exec_extract_icon_bitmap(path,
                                     g_icon_cache[slot].bitmap,
                                     GRAPHICS_ICON_BITMAP_SIZE,
                                     GRAPHICS_ICON_BITMAP_SIZE);
    }
    return flags;
}

static const graphics_icon_cache_entry_t *graphics_cached_icon_for_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    (void) graphics_cached_image_flags_for_path(path);
    for (uint32_t i = 0; i < GRAPHICS_ICON_CACHE_MAX; i++) {
        if (g_icon_cache[i].valid &&
            strcasecmp(g_icon_cache[i].path, path) == 0 &&
            g_icon_cache[i].bitmap_valid) {
            return &g_icon_cache[i];
        }
    }
    return NULL;
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
    /* 文本/代码文件 */
    if (graphics_path_has_suffix(path, ".txt") || graphics_path_has_suffix(path, ".c") ||
        graphics_path_has_suffix(path, ".h") || graphics_path_has_suffix(path, ".md") ||
        graphics_path_has_suffix(path, ".csv") || graphics_path_has_suffix(path, ".log") ||
        graphics_path_has_suffix(path, ".cpp") || graphics_path_has_suffix(path, ".hpp") ||
        graphics_path_has_suffix(path, ".py") || graphics_path_has_suffix(path, ".js") ||
        graphics_path_has_suffix(path, ".html") || graphics_path_has_suffix(path, ".ini")) {
        return GRAPHICS_ICON_TEXT;
    }
    /* 图片文件 */
    if (graphics_path_has_suffix(path, ".jpg") || graphics_path_has_suffix(path, ".jpeg") ||
        graphics_path_has_suffix(path, ".png") || graphics_path_has_suffix(path, ".bmp") ||
        graphics_path_has_suffix(path, ".gif")) {
        return GRAPHICS_ICON_IMAGE;
    }
    /* 视频文件 */
    if (graphics_path_has_suffix(path, ".mp4") || graphics_path_has_suffix(path, ".avi") ||
        graphics_path_has_suffix(path, ".mkv") || graphics_path_has_suffix(path, ".wmv") ||
        graphics_path_has_suffix(path, ".mov") || graphics_path_has_suffix(path, ".MP4") ||
        graphics_path_has_suffix(path, ".AVI") || graphics_path_has_suffix(path, ".MKV")) {
        return GRAPHICS_ICON_VIDEO;
    }
    /* 压缩包 */
    if (graphics_path_has_suffix(path, ".zip") || graphics_path_has_suffix(path, ".rar") ||
        graphics_path_has_suffix(path, ".7z") || graphics_path_has_suffix(path, ".tar") ||
        graphics_path_has_suffix(path, ".gz") || graphics_path_has_suffix(path, ".ZIP") ||
        graphics_path_has_suffix(path, ".RAR")) {
        return GRAPHICS_ICON_PACKAGE;
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
        return GRAPHICS_ICON_DEFAULT_PROGRAM;
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

__attribute__((unused))
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

static bool graphics_draw_cached_icon(uint16_t x,
                                      uint16_t y,
                                      const char *path,
                                      bool small)
{
    const graphics_icon_cache_entry_t *entry;
    uint16_t target = small ? 18 : 48;

    entry = graphics_cached_icon_for_path(path);
    if (entry == NULL) {
        return false;
    }
    for (uint16_t py = 0; py < target; py++) {
        uint32_t source_y = ((uint32_t) py * (GRAPHICS_ICON_BITMAP_SIZE - 1)) / (target - 1);
        for (uint16_t px = 0; px < target; px++) {
            uint32_t source_x = ((uint32_t) px * (GRAPHICS_ICON_BITMAP_SIZE - 1)) / (target - 1);
            uint32_t pixel = entry->bitmap[source_y * GRAPHICS_ICON_BITMAP_SIZE + source_x];
            uint8_t alpha = (uint8_t) (pixel >> 24);

            if (alpha == 0) {
                continue;
            }
            graphics_plot((uint16_t) (x + px), (uint16_t) (y + py), pixel & 0x00FFFFFFU);
        }
    }
    return true;
}

static void graphics_fluent_gear(uint16_t bx, uint16_t by, uint16_t u, uint32_t color)
{
    static const int teeth[8][2] = { {8,1},{11,3},{13,8},{11,13},{8,15},{5,13},{3,8},{5,3} };
    for (uint32_t i = 0; i < 8u; i++) {
        graphics_fill_rect((uint16_t) (bx + (teeth[i][0] - 1) * u),
                           (uint16_t) (by + (teeth[i][1] - 1) * u),
                           (uint16_t) (2u * u), (uint16_t) (2u * u), color);
    }
    graphics_fill_disc((uint16_t) (bx + 8u * u), (uint16_t) (by + 8u * u), (uint16_t) (5u * u), color);
    graphics_fill_disc((uint16_t) (bx + 8u * u), (uint16_t) (by + 8u * u), (uint16_t) (2u * u), 0x00FFFFFFu);
}

static void graphics_draw_icon(uint16_t x, uint16_t y, graphics_icon_kind_t kind, bool small)
{
    /* Fluent design grid is 16x16: small 18px icons use 1px units centred,
     * large 48px icons use 3px units. Flat, line+fill, WinUI accent blue. */
    uint16_t u = small ? 1u : 3u;
    uint16_t bx = (uint16_t) (x + (small ? 1u : 0u));
    uint16_t by = (uint16_t) (y + (small ? 1u : 0u));
    uint32_t accent = 0x000078D4u;
    uint32_t folder = 0x00FFB900u;
    uint32_t folder_light = 0x00FFD24Du;
#define IX(gx) ((uint16_t) (bx + (gx) * u))
#define IY(gy) ((uint16_t) (by + (gy) * u))
#define IW(gw) ((uint16_t) ((gw) * u))

    switch (kind) {
    case GRAPHICS_ICON_FOLDER:
        graphics_fill_rounded_rect(IX(1), IY(2), IW(6), IW(4), folder_light, IW(1));
        graphics_fill_rounded_rect(IX(1), IY(5), IW(14), IW(10), folder, IW(1));
        break;
    case GRAPHICS_ICON_FILE:
    default:
        graphics_fill_rounded_rect(IX(3), IY(1), IW(10), IW(14), 0x00FFFFFFu, IW(1));
        graphics_draw_rounded_rect_outline(IX(3), IY(1), IW(10), IW(14), 0x00C8C8C8u, IW(1));
        graphics_fill_rect(IX(12), IY(1), IW(2), 1, 0x00E5E5E5u);
        graphics_fill_rect(IX(13), IY(2), IW(1), 1, 0x00E5E5E5u);
        if (!small) {
            graphics_fill_rect(IX(5), IY(6), IW(6), 1, 0x00C8C8C8u);
            graphics_fill_rect(IX(5), IY(9), IW(5), 1, 0x00C8C8C8u);
            graphics_fill_rect(IX(5), IY(12), IW(6), 1, 0x00C8C8C8u);
        }
        break;
    case GRAPHICS_ICON_APP:
    case GRAPHICS_ICON_DEFAULT_PROGRAM:
        graphics_fill_rounded_rect(IX(2), IY(2), IW(12), IW(12), accent, IW(2));
        break;
    case GRAPHICS_ICON_APP_RESOURCE:
        graphics_fill_rounded_rect(IX(2), IY(2), IW(12), IW(12), 0x00FFFFFFu, IW(2));
        graphics_draw_rounded_rect_outline(IX(2), IY(2), IW(12), IW(12), accent, IW(2));
        graphics_fill_rounded_rect(IX(4), IY(4), IW(8), IW(4), 0x00CDE4FFu, IW(1));
        graphics_fill_rounded_rect(IX(4), IY(9), IW(5), IW(4), folder, IW(1));
        break;
    case GRAPHICS_ICON_AUDIO:
        graphics_draw_line(IX(9), IY(4), IX(9), IY(12), accent);
        graphics_draw_line(IX(9), IY(4), IX(12), IY(5), accent);
        graphics_fill_disc(IX(7), IY(12), IW(2), accent);
        graphics_fill_disc(IX(11), IY(12), IW(2), accent);
        break;
    case GRAPHICS_ICON_PACKAGE:
        graphics_fill_rounded_rect(IX(2), IY(4), IW(12), IW(10), 0x00F3F2F1u, IW(1));
        graphics_draw_rounded_rect_outline(IX(2), IY(4), IW(12), IW(10), 0x00C8C8C8u, IW(1));
        graphics_fill_rect(IX(2), IY(4), IW(12), IW(3), 0x00E1DFDDu);
        graphics_fill_rect(IX(7), IY(4), IW(2), IW(10), accent);
        break;
    case GRAPHICS_ICON_DLL:
        graphics_fluent_gear(bx, by, u, accent);
        break;
    case GRAPHICS_ICON_DRIVER:
        graphics_fill_rounded_rect(IX(3), IY(4), IW(10), IW(10), 0x00EAF2FBu, IW(1));
        graphics_draw_rounded_rect_outline(IX(3), IY(4), IW(10), IW(10), accent, IW(1));
        graphics_fill_rect(IX(5), IY(2), IW(2), IW(2), accent);
        graphics_fill_rect(IX(8), IY(2), IW(2), IW(2), accent);
        graphics_fill_rect(IX(11), IY(2), IW(2), IW(2), accent);
        graphics_fill_rect(IX(5), IY(12), IW(2), IW(2), accent);
        graphics_fill_rect(IX(8), IY(12), IW(2), IW(2), accent);
        graphics_fill_rect(IX(11), IY(12), IW(2), IW(2), accent);
        graphics_fill_rect(IX(6), IY(7), IW(4), IW(4), accent);
        break;
    case GRAPHICS_ICON_UAC:
        graphics_fill_rect(IX(3), IY(2), IW(10), IW(6), accent);
        graphics_fill_rect(IX(4), IY(8), IW(8), IW(2), accent);
        graphics_fill_rect(IX(5), IY(10), IW(6), IW(1), accent);
        graphics_fill_rect(IX(6), IY(11), IW(4), IW(1), accent);
        graphics_fill_rect(IX(7), IY(12), IW(2), IW(1), accent);
        graphics_draw_line(IX(5), IY(7), IX(7), IY(9), 0x00FFFFFFu);
        graphics_draw_line(IX(7), IY(9), IX(11), IY(5), 0x00FFFFFFu);
        break;
    case GRAPHICS_ICON_NETWORK_CONNECTED:
    case GRAPHICS_ICON_NETWORK_LIMITED:
    case GRAPHICS_ICON_NETWORK_OFFLINE: {
        uint32_t arc = (kind == GRAPHICS_ICON_NETWORK_CONNECTED) ? accent
                     : (kind == GRAPHICS_ICON_NETWORK_LIMITED) ? 0x00C0801Fu : 0x00888888u;
        for (uint16_t rr = 3; rr <= 7u; rr += 2u) {
            for (int dx = -(int) rr; dx <= (int) rr; dx++) {
                int rem = (int) rr * rr - dx * dx;
                int half = 0;
                if (rem < 0) continue;
                while ((half + 1) * (half + 1) <= rem) half++;
                graphics_plot((uint16_t) (bx + (8 + dx) * u), (uint16_t) (by + (13 - half) * u), arc);
            }
        }
        graphics_fill_disc(IX(8), IY(13), IW(1), arc);
        if (kind == GRAPHICS_ICON_NETWORK_OFFLINE) {
            graphics_draw_line(IX(11), IY(10), IX(14), IY(14), 0x00C42B1Cu);
            graphics_draw_line(IX(14), IY(10), IX(11), IY(14), 0x00C42B1Cu);
        }
        break;
    }
    case GRAPHICS_ICON_SHORTCUT:
        graphics_draw_icon(bx, by, GRAPHICS_ICON_APP, small);
        graphics_draw_shortcut_badge((uint16_t) (bx + (small ? 0u : 2u)),
                                     (uint16_t) (by + 14u * u - (small ? 9u : 16u)), small);
        break;
    case GRAPHICS_ICON_TERMINAL:
        graphics_fill_rounded_rect(IX(2), IY(2), IW(12), IW(12), 0x001F1F1Fu, IW(2));
        graphics_draw_line(IX(5), IY(5), IX(8), IY(8), 0x006CCB6Fu);
        graphics_draw_line(IX(8), IY(8), IX(5), IY(11), 0x006CCB6Fu);
        graphics_fill_rect(IX(9), IY(10), IW(3), 1, 0x006CCB6Fu);
        break;
    case GRAPHICS_ICON_SETTINGS:
        graphics_fluent_gear(bx, by, u, accent);
        break;
    case GRAPHICS_ICON_INFO:
        graphics_fill_disc(IX(8), IY(8), IW(6), accent);
        graphics_fill_disc(IX(8), IY(5), IW(1), 0x00FFFFFFu);
        graphics_fill_rect(IX(7), IY(8), IW(2), IW(4), 0x00FFFFFFu);
        break;
    case GRAPHICS_ICON_WINDOW:
        graphics_fill_rounded_rect(IX(2), IY(2), IW(12), IW(12), 0x00FFFFFFu, IW(1));
        graphics_draw_rounded_rect_outline(IX(2), IY(2), IW(12), IW(12), 0x00C8C8C8u, IW(1));
        graphics_fill_rect(IX(2), IY(2), IW(12), IW(3), accent);
        graphics_fill_rect(IX(3), IY(6), IW(10), IW(7), 0x00F3F3F3u);
        break;
    case GRAPHICS_ICON_TEXT:
        graphics_fill_rounded_rect(IX(3), IY(1), IW(10), IW(14), 0x00FFFFFFu, IW(1));
        graphics_draw_rounded_rect_outline(IX(3), IY(1), IW(10), IW(14), 0x00C8C8C8u, IW(1));
        graphics_fill_rect(IX(5), IY(5), IW(6), 1, 0x00B0B0B0u);
        graphics_fill_rect(IX(5), IY(8), IW(7), 1, 0x00B0B0B0u);
        graphics_fill_rect(IX(5), IY(11), IW(5), 1, 0x00B0B0B0u);
        break;
    case GRAPHICS_ICON_IMAGE:
        graphics_fill_rounded_rect(IX(2), IY(2), IW(12), IW(12), 0x00DCEAF7u, IW(1));
        graphics_draw_rounded_rect_outline(IX(2), IY(2), IW(12), IW(12), 0x009CC3E5u, IW(1));
        graphics_fill_disc(IX(11), IY(5), IW(2), 0x00FFC83Du);
        graphics_fill_rect(IX(10), IY(7), IW(1), 1, 0x004C8CCFu);
        graphics_fill_rect(IX(9), IY(8), IW(3), 1, 0x004C8CCFu);
        graphics_fill_rect(IX(8), IY(9), IW(5), 2, 0x004C8CCFu);
        graphics_fill_rect(IX(7), IY(11), IW(7), 2, 0x004C8CCFu);
        graphics_fill_rect(IX(5), IY(9), IW(1), 1, 0x002E6DA4u);
        graphics_fill_rect(IX(4), IY(10), IW(3), 1, 0x002E6DA4u);
        graphics_fill_rect(IX(3), IY(11), IW(5), 1, 0x002E6DA4u);
        graphics_fill_rect(IX(2), IY(12), IW(7), 1, 0x002E6DA4u);
        break;
    case GRAPHICS_ICON_DRIVE:
        graphics_fill_rect(IX(2), IY(4), IW(12), IW(3), 0x00BDBDBDu);
        graphics_fill_rounded_rect(IX(2), IY(7), IW(12), IW(6), 0x00E8E8E8u, IW(1));
        graphics_draw_rounded_rect_outline(IX(2), IY(7), IW(12), IW(6), 0x009E9E9Eu, IW(1));
        graphics_fill_rect(IX(11), IY(9), IW(2), IW(2), 0x004CAF50u);
        break;
    case GRAPHICS_ICON_VIDEO:
        graphics_fill_rounded_rect(IX(2), IY(3), IW(12), IW(10), accent, IW(1));
        graphics_fill_rect(IX(6), IY(5), IW(1), 1, 0x00FFFFFFu);
        graphics_fill_rect(IX(6), IY(6), IW(2), 1, 0x00FFFFFFu);
        graphics_fill_rect(IX(6), IY(7), IW(3), 1, 0x00FFFFFFu);
        graphics_fill_rect(IX(6), IY(8), IW(4), 1, 0x00FFFFFFu);
        graphics_fill_rect(IX(6), IY(9), IW(3), 1, 0x00FFFFFFu);
        graphics_fill_rect(IX(6), IY(10), IW(2), 1, 0x00FFFFFFu);
        break;
    case GRAPHICS_ICON_TEXT_EDITOR:
        graphics_fill_rounded_rect(IX(2), IY(1), IW(12), IW(14), 0x00FFFFFFu, IW(1));
        graphics_draw_rounded_rect_outline(IX(2), IY(1), IW(12), IW(14), 0x009CC3E5u, IW(1));
        graphics_fill_rect(IX(2), IY(1), IW(12), IW(3), accent);
        graphics_fill_rect(IX(4), IY(6), IW(8), 1, 0x00C8C8C8u);
        graphics_fill_rect(IX(4), IY(9), IW(9), 1, 0x00C8C8C8u);
        graphics_fill_rect(IX(4), IY(12), IW(7), 1, 0x00C8C8C8u);
        break;
    }
#undef IX
#undef IY
#undef IW
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
    if (kind == GRAPHICS_ICON_APP_RESOURCE &&
        !graphics_draw_cached_icon(x, y, icon_path, small)) {
        kind = GRAPHICS_ICON_DEFAULT_PROGRAM;
    }
    if (kind != GRAPHICS_ICON_APP_RESOURCE) {
        graphics_draw_icon(x, y, kind, small);
    }
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
    g_ux_win_open_tick[(uint32_t)(window - g_windows)] = g_ux_anim ? timer_ticks() : 0ULL;
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
        graphics_window_set(window, kind, 8, 8,
                            FB_WIDTH > 16 ? (uint16_t) (FB_WIDTH - 16) : FB_WIDTH,
                            FB_HEIGHT > 16 ? (uint16_t) (FB_HEIGHT - 16) : FB_HEIGHT,
                            "MONIOS");
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
        graphics_window_set(window, kind, graphics_clamp_window_x(460), 56, 700, 460, "\u8bb0\u4e8b\u672c");
        g_notepad_focus = true;
        if (g_np_active < 0) {
            (void)np_open_blank(UI_ROOT_DESKTOP);
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
        g_secure_desktop = false;
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

static void __attribute__((unused)) graphics_wait_ticks(uint64_t wait_ticks)
{
    uint64_t start = timer_ticks();

    while (timer_ticks() - start < wait_ticks) {
    }
}

static uint16_t graphics_read_le16(const uint8_t *data);
static uint32_t graphics_read_le32(const uint8_t *data);
static uint16_t graphics_read_be16(const uint8_t *data);

static uint64_t graphics_u64_ceil_div(uint64_t value, uint64_t divisor)
{
    if (divisor == 0) {
        return 0;
    }
    return (value + divisor - 1ULL) / divisor;
}

static int32_t graphics_jpeg_descale(int64_t value, uint8_t shift)
{
    int64_t rounding = (int64_t) 1 << (shift - 1);

    if (value >= 0) {
        return (int32_t) ((value + rounding) >> shift);
    }
    return -(int32_t) (((-value) + rounding) >> shift);
}

static void graphics_wallpaper_release(void)
{
    if (g_wallpaper_pixels != NULL) {
        kfree(g_wallpaper_pixels);
        g_wallpaper_pixels = NULL;
    }
    g_wallpaper_width = 0;
    g_wallpaper_height = 0;
    g_wallpaper_attempted = false;
    g_wallpaper_loaded = false;
}

static bool graphics_wallpaper_prepare_target(graphics_wallpaper_target_t *target,
                                              uint16_t source_width,
                                              uint16_t source_height,
                                              uint16_t dest_height)
{
    uint64_t dest_pixels;
    uint32_t scaled_width;
    uint32_t scaled_height;

    if (target == NULL || source_width == 0 || source_height == 0 || dest_height == 0) {
        return false;
    }
    dest_pixels = (uint64_t) FB_WIDTH * dest_height;
    if (dest_pixels == 0 || dest_pixels > GRAPHICS_WALLPAPER_MAX_PIXELS) {
        return false;
    }
    if ((uint32_t) source_width > GRAPHICS_WALLPAPER_MAX_WIDTH ||
        (uint32_t) source_height > GRAPHICS_WALLPAPER_MAX_HEIGHT) {
        return false;
    }

    if ((uint64_t) FB_WIDTH * source_height >= (uint64_t) dest_height * source_width) {
        scaled_width = FB_WIDTH;
        scaled_height = (uint32_t) graphics_u64_ceil_div((uint64_t) FB_WIDTH * source_height, source_width);
    } else {
        scaled_height = dest_height;
        scaled_width = (uint32_t) graphics_u64_ceil_div((uint64_t) dest_height * source_width, source_height);
    }
    if (scaled_width == 0 || scaled_height == 0) {
        return false;
    }

    target->pixels = (uint32_t *) kmalloc(dest_pixels * sizeof(uint32_t));
    if (target->pixels == NULL) {
        return false;
    }
    memset(target->pixels, 0, dest_pixels * sizeof(uint32_t));
    target->dest_width = FB_WIDTH;
    target->dest_height = dest_height;
    target->source_width = source_width;
    target->source_height = source_height;
    target->scaled_width = scaled_width;
    target->scaled_height = scaled_height;
    if ((uint64_t) FB_WIDTH * source_height >= (uint64_t) dest_height * source_width) {
        target->crop_x = 0;
        target->crop_y = scaled_height > dest_height ? (scaled_height - dest_height) / 2U : 0;
    } else {
        target->crop_y = 0;
        target->crop_x = scaled_width > FB_WIDTH ? (scaled_width - FB_WIDTH) / 2U : 0;
    }
    return true;
}

static void graphics_wallpaper_put_source_pixel(graphics_wallpaper_target_t *target,
                                                uint16_t source_x,
                                                uint16_t source_y,
                                                uint32_t color)
{
    uint64_t dest_x0;
    uint64_t dest_x1;
    uint64_t dest_y0;
    uint64_t dest_y1;
    uint16_t dest_x;
    uint16_t dest_y;

    if (target == NULL || target->pixels == NULL || source_x >= target->source_width || source_y >= target->source_height) {
        return;
    }

    dest_x0 = graphics_u64_ceil_div((uint64_t) source_x * target->scaled_width, target->source_width);
    dest_x1 = graphics_u64_ceil_div((uint64_t) (source_x + 1U) * target->scaled_width, target->source_width);
    dest_y0 = graphics_u64_ceil_div((uint64_t) source_y * target->scaled_height, target->source_height);
    dest_y1 = graphics_u64_ceil_div((uint64_t) (source_y + 1U) * target->scaled_height, target->source_height);

    if (dest_x1 <= target->crop_x || dest_y1 <= target->crop_y) {
        return;
    }
    if (dest_x0 < target->crop_x) {
        dest_x0 = target->crop_x;
    }
    if (dest_y0 < target->crop_y) {
        dest_y0 = target->crop_y;
    }
    if (dest_x1 > target->crop_x + target->dest_width) {
        dest_x1 = target->crop_x + target->dest_width;
    }
    if (dest_y1 > target->crop_y + target->dest_height) {
        dest_y1 = target->crop_y + target->dest_height;
    }
    if (dest_x0 >= dest_x1 || dest_y0 >= dest_y1) {
        return;
    }

    for (dest_y = (uint16_t) (dest_y0 - target->crop_y); dest_y < (uint16_t) (dest_y1 - target->crop_y); dest_y++) {
        uint64_t row = (uint64_t) dest_y * target->dest_width;

        for (dest_x = (uint16_t) (dest_x0 - target->crop_x); dest_x < (uint16_t) (dest_x1 - target->crop_x); dest_x++) {
            target->pixels[row + dest_x] = color;
        }
    }
}

static int32_t graphics_jpeg_receive_bits(graphics_jpeg_bit_reader_t *reader, uint8_t count)
{
    int32_t result = 0;

    if (reader == NULL || count > 16) {
        return -1;
    }
    while (count > 0) {
        if (reader->bits_left == 0) {
            int32_t byte;

            if (reader->pos >= reader->size) {
                reader->failed = true;
                return -1;
            }
            byte = (int32_t) reader->data[reader->pos++];
            if (byte == 0xFF) {
                while (reader->pos < reader->size && reader->data[reader->pos] == 0xFF) {
                    reader->pos++;
                }
                if (reader->pos < reader->size && reader->data[reader->pos] == 0x00) {
                    reader->pos++;
                } else {
                    reader->failed = true;
                    return -1;
                }
            }
            reader->bit_buffer = (uint32_t) byte;
            reader->bits_left = 8;
        }
        result = (result << 1) | (int32_t) ((reader->bit_buffer >> (reader->bits_left - 1U)) & 1U);
        reader->bits_left--;
        count--;
    }
    return result;
}

static bool graphics_jpeg_get_bits(graphics_jpeg_bit_reader_t *reader, uint8_t count, int32_t *value)
{
    int32_t bits;

    bits = graphics_jpeg_receive_bits(reader, count);
    if (bits < 0) {
        return false;
    }
    if (count > 0 && bits < (1 << (count - 1U))) {
        bits -= (1 << count) - 1;
    }
    *value = bits;
    return true;
}

static bool graphics_jpeg_decode_huffman(graphics_jpeg_bit_reader_t *reader,
                                         const graphics_jpeg_huffman_table_t *table,
                                         uint8_t *symbol)
{
    int32_t code = 0;

    if (reader == NULL || table == NULL || symbol == NULL || !table->valid) {
        return false;
    }
    for (uint8_t len = 1; len <= 16; len++) {
        int32_t bit = graphics_jpeg_receive_bits(reader, 1);

        if (bit < 0) {
            return false;
        }
        code = (code << 1) | bit;
        if (table->counts[len] == 0) {
            continue;
        }
        if (code >= table->first_code[len] &&
            code < table->first_code[len] + (int32_t) table->counts[len]) {
            *symbol = table->symbols[table->first_symbol[len] + (uint16_t) (code - table->first_code[len])];
            return true;
        }
    }
    return false;
}

static const uint8_t g_jpeg_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static void graphics_jpeg_idct_block(const int32_t *block, uint8_t *out_pixels)
{
    static const int32_t c[8] = { 724, 1024, 1024, 1024, 1024, 1024, 1024, 1024 };
    static const int32_t cos_table[8][8] = {
        { 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024 },
        { 1004, 851, 569, 200, -200, -569, -851, -1004 },
        { 946, 392, -392, -946, -946, -392, 392, 946 },
        { 851, -200, -1004, -569, 569, 1004, 200, -851 },
        { 724, -724, -724, 724, 724, -724, -724, 724 },
        { 569, -1004, 200, 851, -851, -200, 1004, -569 },
        { 392, -946, 946, -392, -392, 946, -946, 392 },
        { 200, -569, 851, -1004, 1004, -851, 569, -200 }
    };
    int64_t temp[64];
    bool has_ac = false;
    uint16_t index;

    for (index = 1; index < 64; index++) {
        if (block[index] != 0) {
            has_ac = true;
            break;
        }
    }
    if (!has_ac) {
        int32_t value = graphics_jpeg_descale(block[0], 3) + 128;

        if (value < 0) {
            value = 0;
        } else if (value > 255) {
            value = 255;
        }
        memset(out_pixels, (uint8_t) value, 64);
        return;
    }

    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            int64_t sum = 0;

            for (uint8_t u = 0; u < 8; u++) {
                sum += (int64_t) block[y * 8U + u] * c[u] * cos_table[u][x];
            }
            temp[y * 8U + x] = sum;
        }
    }

    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            int64_t sum = 0;

            for (uint8_t v = 0; v < 8; v++) {
                sum += (int64_t) c[v] * temp[v * 8U + x] * cos_table[v][y];
            }
            {
                int32_t value = graphics_jpeg_descale(sum, 42) + 128;

                if (value < 0) {
                    value = 0;
                } else if (value > 255) {
                    value = 255;
                }
                out_pixels[y * 8U + x] = (uint8_t) value;
            }
        }
    }
}

static bool graphics_jpeg_decode_block(graphics_jpeg_decoder_t *decoder,
                                       graphics_jpeg_bit_reader_t *reader,
                                       uint8_t component_index,
                                       int32_t *block)
{
    graphics_jpeg_component_t *component;
    const graphics_jpeg_quant_table_t *quant;
    uint8_t symbol;
    uint8_t run_length;
    uint8_t size_bits;
    uint8_t k = 1;

    if (decoder == NULL || reader == NULL || block == NULL || component_index >= decoder->component_count) {
        return false;
    }
    component = &decoder->components[component_index];
    if (component->quant_table >= 4U || !decoder->quant[component->quant_table].valid) {
        return false;
    }
    quant = &decoder->quant[component->quant_table];
    memset(block, 0, 64 * sizeof(int32_t));

    if (!graphics_jpeg_decode_huffman(reader, &decoder->huffman[0][component->dc_table], &symbol)) {
        return false;
    }
    size_bits = symbol;
    if (size_bits > 0) {
        int32_t delta;

        if (!graphics_jpeg_get_bits(reader, size_bits, &delta)) {
            return false;
        }
        component->dc_pred += delta;
    }
    block[0] = component->dc_pred * (int32_t) quant->values[0];

    while (k < 64) {
        if (!graphics_jpeg_decode_huffman(reader, &decoder->huffman[1][component->ac_table], &symbol)) {
            return false;
        }
        if (symbol == 0x00U) {
            break;
        }
        if (symbol == 0xF0U) {
            k = (uint8_t) (k + 16U);
            continue;
        }
        run_length = (uint8_t) (symbol >> 4U);
        size_bits = (uint8_t) (symbol & 0x0FU);
        k = (uint8_t) (k + run_length);
        if (k >= 64U) {
            return false;
        }
        {
            int32_t coeff;
            uint8_t natural_index = g_jpeg_zigzag[k];

            if (!graphics_jpeg_get_bits(reader, size_bits, &coeff)) {
                return false;
            }
            block[natural_index] = coeff * (int32_t) quant->values[natural_index];
        }
        k++;
    }
    return true;
}

static bool graphics_jpeg_decode_scan(graphics_jpeg_decoder_t *decoder,
                                      const uint8_t *data,
                                      uint32_t size,
                                      uint16_t target_height,
                                      graphics_wallpaper_target_t *target)
{
    graphics_jpeg_bit_reader_t reader;
    uint16_t mcu_width;
    uint16_t mcu_height;
    uint16_t mcu_cols;
    uint16_t mcu_rows;
    uint8_t samples[GRAPHICS_JPEG_MAX_COMPONENTS][GRAPHICS_JPEG_MAX_MCU_SAMPLES];
    uint16_t sample_width[GRAPHICS_JPEG_MAX_COMPONENTS];
    uint16_t sample_height[GRAPHICS_JPEG_MAX_COMPONENTS];
    uint16_t mcu_y;
    uint16_t mcu_x;

    if (decoder == NULL || data == NULL || target == NULL || decoder->component_count == 0) {
        return false;
    }
    mcu_width = (uint16_t) (decoder->max_h * 8U);
    mcu_height = (uint16_t) (decoder->max_v * 8U);
    if (mcu_width == 0 || mcu_height == 0) {
        return false;
    }
    if (!graphics_wallpaper_prepare_target(target, decoder->width, decoder->height, target_height)) {
        return false;
    }

    reader.data = data;
    reader.size = size;
    reader.pos = 0;
    reader.bit_buffer = 0;
    reader.bits_left = 0;
    reader.failed = false;

    mcu_cols = (uint16_t) ((decoder->width + mcu_width - 1U) / mcu_width);
    mcu_rows = (uint16_t) ((decoder->height + mcu_height - 1U) / mcu_height);
    for (uint8_t i = 0; i < decoder->component_count; i++) {
        graphics_jpeg_component_t *component = &decoder->components[i];
        uint16_t sample_w = (uint16_t) (component->h * 8U);
        uint16_t sample_h = (uint16_t) (component->v * 8U);

        if (sample_w == 0 || sample_h == 0 || (uint32_t) sample_w * sample_h > GRAPHICS_JPEG_MAX_MCU_SAMPLES) {
            return false;
        }
        sample_width[i] = sample_w;
        sample_height[i] = sample_h;
    }

    for (mcu_y = 0; mcu_y < mcu_rows; mcu_y++) {
        for (mcu_x = 0; mcu_x < mcu_cols; mcu_x++) {
            uint16_t source_x_base = (uint16_t) (mcu_x * mcu_width);
            uint16_t source_y_base = (uint16_t) (mcu_y * mcu_height);
            bool mcu_visible = true;

            (void) mcu_visible;
            for (uint8_t order = 0; order < decoder->scan_count; order++) {
                uint8_t i = decoder->scan_order[order];
                graphics_jpeg_component_t *component = &decoder->components[i];
                uint16_t sample_w = sample_width[i];
                uint16_t sample_h = sample_height[i];

                for (uint8_t block_y = 0; block_y < component->v; block_y++) {
                    for (uint8_t block_x = 0; block_x < component->h; block_x++) {
                        int32_t block[64];
                        uint8_t block_pixels[64];

                        if (!graphics_jpeg_decode_block(decoder, &reader, i, block)) {
                            kfree(target->pixels);
                            target->pixels = NULL;
                            return false;
                        }
                        graphics_jpeg_idct_block(block, block_pixels);
                        for (uint8_t yy = 0; yy < 8; yy++) {
                            uint8_t *row = samples[i] + (block_y * 8U + yy) * sample_w + block_x * 8U;

                            memcpy(row, block_pixels + yy * 8U, 8);
                        }
                    }
                }
                (void) sample_h;
            }

            for (uint16_t py = 0; py < mcu_height; py++) {
                uint16_t source_y = (uint16_t) (source_y_base + py);

                if (source_y >= decoder->height) {
                    continue;
                }
                for (uint16_t px = 0; px < mcu_width; px++) {
                    uint16_t source_x = (uint16_t) (source_x_base + px);
                    uint8_t yv;
                    int32_t cb = 0;
                    int32_t cr = 0;
                    uint32_t color;

                    if (source_x >= decoder->width) {
                        continue;
                    }
                    {
                        uint16_t yx = (uint16_t) ((px * sample_width[0]) / mcu_width);
                        uint16_t yy = (uint16_t) ((py * sample_height[0]) / mcu_height);

                        if (yx >= sample_width[0]) {
                            yx = (uint16_t) (sample_width[0] - 1U);
                        }
                        if (yy >= sample_height[0]) {
                            yy = (uint16_t) (sample_height[0] - 1U);
                        }
                        yv = samples[0][yy * sample_width[0] + yx];
                    }
                    if (decoder->component_count > 1) {
                        uint16_t cx = (uint16_t) ((px * sample_width[1]) / mcu_width);
                        uint16_t cy = (uint16_t) ((py * sample_height[1]) / mcu_height);
                        uint16_t crx = (uint16_t) ((px * sample_width[2]) / mcu_width);
                        uint16_t cry = (uint16_t) ((py * sample_height[2]) / mcu_height);

                        if (cx >= sample_width[1]) {
                            cx = (uint16_t) (sample_width[1] - 1U);
                        }
                        if (cy >= sample_height[1]) {
                            cy = (uint16_t) (sample_height[1] - 1U);
                        }
                        if (crx >= sample_width[2]) {
                            crx = (uint16_t) (sample_width[2] - 1U);
                        }
                        if (cry >= sample_height[2]) {
                            cry = (uint16_t) (sample_height[2] - 1U);
                        }
                        cb = samples[1][cy * sample_width[1] + cx] - 128;
                        cr = samples[2][cry * sample_width[2] + crx] - 128;
                    } else {
                        cb = 0;
                        cr = 0;
                    }
                    color = (uint32_t) yv;
                    if (decoder->component_count > 1) {
                        int32_t r = (int32_t) yv + ((91881 * cr) >> 16);
                        int32_t g = (int32_t) yv - ((22554 * cb + 46802 * cr) >> 16);
                        int32_t b = (int32_t) yv + ((116130 * cb) >> 16);

                        if (r < 0) {
                            r = 0;
                        } else if (r > 255) {
                            r = 255;
                        }
                        if (g < 0) {
                            g = 0;
                        } else if (g > 255) {
                            g = 255;
                        }
                        if (b < 0) {
                            b = 0;
                        } else if (b > 255) {
                            b = 255;
                        }
                        color = ((uint32_t) r << 16) | ((uint32_t) g << 8) | (uint32_t) b;
                    }
                    graphics_wallpaper_put_source_pixel(target, source_x, source_y, color);
                }
            }
        }
    }

    if (reader.failed) {
        kfree(target->pixels);
        target->pixels = NULL;
        return false;
    }
    return true;
}

static bool graphics_decode_jpeg_wallpaper(const uint8_t *data, uint32_t size, uint16_t target_height)
{
    graphics_jpeg_decoder_t decoder;
    graphics_wallpaper_target_t target;
    uint32_t pos;
    bool saw_sof;
    bool saw_sos;

    if (data == NULL || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    memset(&decoder, 0, sizeof(decoder));
    memset(&target, 0, sizeof(target));
    saw_sof = false;
    saw_sos = false;
    pos = 2;

    while (pos + 4 <= size) {
        uint8_t marker;
        uint16_t segment_size;
        uint32_t segment_end;

        if (data[pos] != 0xFF) {
            pos++;
            continue;
        }
        while (pos < size && data[pos] == 0xFF) {
            pos++;
        }
        if (pos >= size) {
            break;
        }
        marker = data[pos++];
        if (marker == 0x00) {
            continue;
        }
        if (marker == 0xD9) {
            break;
        }
        if (marker >= 0xD0 && marker <= 0xD7) {
            continue;
        }
        if (pos + 2 > size) {
            return false;
        }
        segment_size = graphics_read_be16(data + pos);
        if (segment_size < 2) {
            return false;
        }
        segment_end = pos + segment_size;
        if (segment_end > size) {
            return false;
        }

        if (marker == 0xDB) {
            uint32_t cursor = pos + 2;

            while (cursor < segment_end) {
                uint8_t table_info;
                uint8_t table_id;
                uint8_t precision;
                uint32_t value_index;

                if (cursor >= segment_end) {
                    return false;
                }
                table_info = data[cursor++];
                precision = table_info >> 4U;
                table_id = table_info & 0x0FU;
                if (table_id >= 4U || precision > 1U) {
                    return false;
                }
                if (cursor + (precision == 0 ? 64U : 128U) > segment_end) {
                    return false;
                }
                for (value_index = 0; value_index < 64; value_index++) {
                    uint16_t value;
                    uint8_t natural_index = g_jpeg_zigzag[value_index];

                    if (precision == 0) {
                        value = data[cursor++];
                    } else {
                        value = graphics_read_be16(data + cursor);
                        cursor += 2;
                    }
                    decoder.quant[table_id].values[natural_index] = value;
                }
                decoder.quant[table_id].valid = true;
            }
        } else if (marker == 0xC0) {
            uint32_t cursor = pos + 2;
            uint8_t component_count;

            if (segment_end - cursor < 6) {
                return false;
            }
            if (data[cursor++] != 8) {
                return false;
            }
            decoder.height = graphics_read_be16(data + cursor);
            cursor += 2;
            decoder.width = graphics_read_be16(data + cursor);
            cursor += 2;
            component_count = data[cursor++];
            if (component_count == 0 || component_count > GRAPHICS_JPEG_MAX_COMPONENTS ||
                component_count == 2 ||
                decoder.width == 0 || decoder.height == 0 ||
                decoder.width > GRAPHICS_WALLPAPER_MAX_WIDTH ||
                decoder.height > GRAPHICS_WALLPAPER_MAX_HEIGHT) {
                return false;
            }
            decoder.component_count = component_count;
            decoder.max_h = 0;
            decoder.max_v = 0;
            for (uint8_t i = 0; i < component_count; i++) {
                graphics_jpeg_component_t *component = &decoder.components[i];

                component->id = data[cursor++];
                component->h = data[cursor] >> 4U;
                component->v = data[cursor] & 0x0FU;
                cursor++;
                component->quant_table = data[cursor++];
                component->dc_table = 0;
                component->ac_table = 0;
                component->dc_pred = 0;
                if (component->h == 0 || component->v == 0 ||
                    component->h > GRAPHICS_JPEG_MAX_SAMPLING ||
                    component->v > GRAPHICS_JPEG_MAX_SAMPLING ||
                    component->quant_table >= 4U) {
                    return false;
                }
                if (component->h > decoder.max_h) {
                    decoder.max_h = component->h;
                }
                if (component->v > decoder.max_v) {
                    decoder.max_v = component->v;
                }
            }
            saw_sof = true;
        } else if (marker == 0xC4) {
            uint32_t cursor = pos + 2;

            while (cursor < segment_end) {
                uint8_t table_info;
                uint8_t table_class;
                uint8_t table_id;
                uint32_t total = 0;
                uint16_t symbol_index = 0;
                int32_t code = 0;

                table_info = data[cursor++];
                table_class = table_info >> 4U;
                table_id = table_info & 0x0FU;
                if (table_class > 1U || table_id >= 4U || cursor + 16 > segment_end) {
                    return false;
                }
                memset(&decoder.huffman[table_class][table_id], 0, sizeof(graphics_jpeg_huffman_table_t));
                decoder.huffman[table_class][table_id].valid = true;
                for (uint8_t len = 1; len <= 16; len++) {
                    decoder.huffman[table_class][table_id].counts[len] = data[cursor++];
                    total += decoder.huffman[table_class][table_id].counts[len];
                    if (decoder.huffman[table_class][table_id].counts[len] > 0) {
                        decoder.huffman[table_class][table_id].first_code[len] = code;
                        decoder.huffman[table_class][table_id].first_symbol[len] = symbol_index;
                    }
                    code = (code + decoder.huffman[table_class][table_id].counts[len]) << 1;
                    symbol_index += decoder.huffman[table_class][table_id].counts[len];
                }
                if (cursor + total > segment_end || total > 256U) {
                    return false;
                }
                for (uint32_t i = 0; i < total; i++) {
                    decoder.huffman[table_class][table_id].symbols[i] = data[cursor++];
                }
            }
        } else if (marker == 0xDD) {
            if (segment_end - pos < 4) {
                return false;
            }
            decoder.restart_interval = graphics_read_be16(data + pos + 2);
        } else if (marker == 0xDA) {
            uint32_t cursor = pos + 2;

            if (!saw_sof || decoder.component_count == 0) {
                return false;
            }
            if (cursor >= segment_end) {
                return false;
            }
            decoder.scan_count = data[cursor++];
            if (decoder.scan_count == 0 || decoder.scan_count != decoder.component_count ||
                cursor + (uint32_t) decoder.scan_count * 2U + 3U > segment_end) {
                return false;
            }
            for (uint8_t i = 0; i < decoder.scan_count; i++) {
                uint8_t component_id = data[cursor++];
                uint8_t tables = data[cursor++];
                bool found = false;

                for (uint8_t j = 0; j < decoder.component_count; j++) {
                    if (decoder.components[j].id == component_id) {
                        decoder.components[j].dc_table = tables >> 4U;
                        decoder.components[j].ac_table = tables & 0x0FU;
                        if (decoder.components[j].dc_table >= 4U || decoder.components[j].ac_table >= 4U) {
                            return false;
                        }
                        decoder.scan_order[i] = j;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
            if (data[cursor++] != 0 || data[cursor++] != 63 || data[cursor++] != 0) {
                return false;
            }
            saw_sos = true;
            pos = cursor;
            break;
        }
        pos = segment_end;
    }

    if (!saw_sof || !saw_sos) {
        return false;
    }
    if (!graphics_jpeg_decode_scan(&decoder, data + pos, size - pos, target_height, &target)) {
        return false;
    }
    graphics_wallpaper_release();
    g_wallpaper_pixels = target.pixels;
    g_wallpaper_width = target.dest_width;
    g_wallpaper_height = target.dest_height;
    g_wallpaper_loaded = true;
    return true;
}

static bool graphics_decode_bmp_wallpaper(const uint8_t *data, uint32_t size, uint16_t target_height)
{
    graphics_wallpaper_target_t target;
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width;
    int32_t raw_height;
    uint32_t height;
    uint32_t stride;
    bool top_down;

    if (data == NULL || size <= 54 || data[0] != 'B' || data[1] != 'M') {
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
        return false;
    }

    top_down = raw_height < 0;
    height = top_down ? (uint32_t) -raw_height : (uint32_t) raw_height;
    stride = (((uint32_t) width * 3U) + 3U) & ~3U;
    if (pixel_offset > size || stride == 0 || height == 0 || stride > (size - pixel_offset) / height) {
        return false;
    }
    if (!graphics_wallpaper_prepare_target(&target, (uint16_t) width, (uint16_t) height, target_height)) {
        return false;
    }
    for (uint32_t y = 0; y < height; y++) {
        uint32_t source_y = top_down ? y : height - 1U - y;
        const uint8_t *row = data + pixel_offset + source_y * stride;

        for (uint32_t x = 0; x < (uint32_t) width; x++) {
            uint8_t b = row[x * 3U];
            uint8_t g = row[x * 3U + 1U];
            uint8_t r = row[x * 3U + 2U];

            graphics_wallpaper_put_source_pixel(&target, (uint16_t) x, (uint16_t) y,
                                                ((uint32_t) r << 16) | ((uint32_t) g << 8) | b);
        }
    }
    if (target.pixels == NULL) {
        return false;
    }
    graphics_wallpaper_release();
    g_wallpaper_pixels = target.pixels;
    g_wallpaper_width = target.dest_width;
    g_wallpaper_height = target.dest_height;
    g_wallpaper_loaded = true;
    return true;
}

static const char *ux_wallpaper_effective_path(void);
static bool graphics_load_wallpaper_asset(uint16_t target_height)
{
    uint8_t *data;
    int32_t size;

    if (g_wallpaper_loaded && g_wallpaper_width == FB_WIDTH && g_wallpaper_height == target_height) {
        return true;
    }
    if (g_wallpaper_pixels != NULL && (g_wallpaper_width != FB_WIDTH || g_wallpaper_height != target_height)) {
        graphics_wallpaper_release();
    }
    if (g_wallpaper_attempted && !g_wallpaper_loaded) {
        return false;
    }

    g_wallpaper_attempted = true;
    {
        const char *wp_path = ux_wallpaper_effective_path();
        size = file_size(wp_path);
    }
    if (size <= 4 || (uint32_t) size > GRAPHICS_WALLPAPER_MAX_BYTES) {
        return false;
    }
    data = (uint8_t *) kmalloc((uint32_t) size);
    if (data == NULL) {
        return false;
    }
    if (file_read(ux_wallpaper_effective_path(), data, (uint32_t) size) != size) {
        kfree(data);
        return false;
    }
    if (data[0] == 'B' && data[1] == 'M') {
        g_wallpaper_loaded = graphics_decode_bmp_wallpaper(data, (uint32_t) size, target_height);
    } else if ((uint32_t) size >= 4U && data[0] == 0xFF && data[1] == 0xD8) {
        g_wallpaper_loaded = graphics_decode_jpeg_wallpaper(data, (uint32_t) size, target_height);
    } else {
        g_wallpaper_loaded = false;
    }
    kfree(data);
    if (!g_wallpaper_loaded) {
        graphics_wallpaper_release();
        return false;
    }
    g_wallpaper_width = FB_WIDTH;
    g_wallpaper_height = target_height;
    log_write("graphics: wallpaper loaded");
    return true;
}

static bool graphics_draw_wallpaper_asset(uint16_t height)
{
    uint64_t row_bytes;

    if (height == 0 || !graphics_load_wallpaper_asset(height) || g_wallpaper_pixels == NULL) {
        return false;
    }
    row_bytes = (uint64_t) FB_WIDTH * sizeof(uint32_t);
    for (uint16_t y = 0; y < height; y++) {
        memcpy(&g_backbuffer[(uint64_t) y * FB_WIDTH], &g_wallpaper_pixels[(uint64_t) y * FB_WIDTH], (uint32_t) row_bytes);
    }
    return true;
}

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

static uint16_t graphics_read_be16(const uint8_t *data)
{
    return ((uint16_t) data[0] << 8) | (uint16_t) data[1];
}

static bool graphics_cursor_decode_dib(const uint8_t *data, uint32_t size,
                                       uint16_t entry_width, uint16_t entry_height,
                                       uint16_t hotspot_x, uint16_t hotspot_y)
{
    (void)entry_width;
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

static void graphics_draw_desktop(void)
{
    uint16_t usable_h = (uint16_t) (FB_HEIGHT > TASKBAR_HEIGHT ? FB_HEIGHT - TASKBAR_HEIGHT : FB_HEIGHT);
    uint16_t center_x = (uint16_t) (FB_WIDTH / 2);
    uint16_t fold_w = (uint16_t) (FB_WIDTH / 3);
    uint16_t fold_h = (uint16_t) (usable_h / 2);

    if (graphics_draw_wallpaper_asset(FB_HEIGHT)) {
        graphics_fill_rect(0, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), FB_WIDTH, TASKBAR_HEIGHT,
                           ui_color(0x00EAF0F6u, 0x001C1C1Eu));
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
    graphics_fill_rect(0, (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT), FB_WIDTH, TASKBAR_HEIGHT,
                       ui_color(0x00EAF0F6u, 0x001C1C1Eu));
}

static void graphics_load_desktop_entries(void)
{
    char desktop_path[GRAPHICS_FILE_PATH_MAX];

    /* The desktop is just a full-screen grid view of the Desktop folder, so it
     * reuses the exact same directory scanner as the file-manager window. */
    g_desktop_entry_count = 0;
    memset(g_desktop_recycle_flag, 0, sizeof(g_desktop_recycle_flag));
    graphics_current_desktop_path(desktop_path);
    if (!file_exists(desktop_path)) {
        file_mkdir(desktop_path);
    }
    g_desktop_entry_count = graphics_scan_directory(desktop_path, g_desktop_entries,
                                                    DESKTOP_LABEL_MAX);

    /* synthetic Recycle Bin desktop icon */
    if (g_desktop_entry_count < DESKTOP_LABEL_MAX) {
        char desktop_path2[GRAPHICS_FILE_PATH_MAX];
        char rp[GRAPHICS_FILE_PATH_MAX];
        uint32_t rc = g_desktop_entry_count++;
        strlcpy(g_desktop_entries[rc].name, "Recycle Bin", sizeof(g_desktop_entries[rc].name));
        g_desktop_entries[rc].is_dir = true;
        g_desktop_recycle_flag[rc] = true;
        graphics_current_desktop_path(desktop_path2);
        rp[0] = desktop_path2[0];
        rp[1] = ':';
        rp[2] = '\\';
        strlcpy(rp + 3, "$RECYCLE.BIN", sizeof(rp) - 3);
        if (!file_exists(rp)) {
            file_mkdir(rp);
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
            /* icon theme: flat uses a solid block; skeuomorphic adds a soft outline */
            if (g_ux_icon_flat) {
                graphics_fill_rect((uint16_t) (x + 4), y, 66, 74, 0x004C83C6);
            } else {
                graphics_fill_soft_rect((uint16_t) (x + 4), y, 66, 74, 0x004C83C6);
                graphics_draw_soft_rect_outline((uint16_t) (x + 4), y, 66, 74, 0x00B8E2FF);
            }
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
    uint32_t fixed_w = START_BUTTON_WIDTH + 10u +
                       (uint32_t)(sizeof(g_taskbar_buttons)/sizeof(g_taskbar_buttons[0])) * (BUTTON_WIDTH + BUTTON_GAP);
    uint32_t cx = (uint32_t)FB_WIDTH / 2u;
    return cx > fixed_w / 2u ? (uint16_t)(cx - fixed_w / 2u) : 0u;
}

static uint16_t graphics_taskbar_running_start_x(void)
{
    uint32_t pinned_count = (uint32_t) (sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0]));
    return (uint16_t) (graphics_taskbar_pinned_start_x() + START_BUTTON_WIDTH + 10u +
                       pinned_count * (BUTTON_WIDTH + BUTTON_GAP) + 10u);
}

static uint32_t graphics_png_icon_for_window(ui_window_kind_t kind)
{
    switch (kind) {
    case UI_WINDOW_FILES: return ICON_FILE_FOLDER;
    case UI_WINDOW_TERMINAL:
    case UI_WINDOW_PROCESS_CONSOLE:
    case UI_WINDOW_SHELL:
    case UI_WINDOW_RUN: return ICON_FILE_CODE;
    case UI_WINDOW_CONTROL_PANEL: return ICON_UI_SETTINGS;
    case UI_WINDOW_ABOUT: return ICON_UI_INFO;
    case UI_WINDOW_PLAYER: return ICON_FILE_AUDIO;
    case UI_WINDOW_UAC: return ICON_UI_LOCK;
    case UI_WINDOW_NOTEPAD: return ICON_FILE_TEXT;
    default: return ICON_UI_HOME;
    }
}

static void graphics_draw_taskbar(void)
{
    if (!g_session_logged_in) {
        return;
    }
    uint16_t start_x = graphics_taskbar_pinned_start_x();
    uint16_t task_x;
    uint16_t pill_left, pill_right;
    uint32_t taskbar_rgb = ui_color(UI_LIGHT_TASKBAR_RGB, UI_DARK_TASKBAR_RGB);
    uint32_t hover_rgb = ui_color(0x00FFFFFFu, 0x003A3A3Au);
    uint32_t accent = g_ux_accent;
    uint16_t fixed_end = (uint16_t)(start_x + START_BUTTON_WIDTH + 10u +
                        (uint32_t)(sizeof(g_taskbar_buttons)/sizeof(g_taskbar_buttons[0])) * (BUTTON_WIDTH + BUTTON_GAP));

    /* measure running buttons so the acrylic pill spans the whole cluster */
    uint16_t run_x = graphics_taskbar_running_start_x();
    uint16_t run_end = run_x;
    uint32_t run_slots = 0;
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible || g_windows[i].kind == UI_WINDOW_LOGON || g_windows[i].kind == UI_WINDOW_POWER) continue;
        if (run_x + UI_TASK_BUTTON_W >= FB_WIDTH - 184) break;
        run_end = (uint16_t)(run_x + UI_TASK_BUTTON_W);
        run_x = (uint16_t)(run_x + UI_TASK_BUTTON_W + 8);
        run_slots++;
    }
    pill_left = (uint16_t)(start_x - 6);
    pill_right = (uint16_t)((run_slots ? run_end : fixed_end) + 6u);

    /* Win11 acrylic pill: one rounded translucent bar behind all icons */
    graphics_fill_rounded_rect_alpha(pill_left, (uint16_t)(BUTTON_Y - 5),
                                  (uint16_t)(pill_right - pill_left),
                                  (uint16_t)(BUTTON_HEIGHT + 10),
                                  taskbar_rgb, UI_TASKBAR_ALPHA, 8u);

    /* Start button */
    graphics_fill_rounded_rect_alpha(start_x, BUTTON_Y, START_BUTTON_WIDTH, BUTTON_HEIGHT,
                                  hover_rgb, g_start_menu_open ? 230u : 80u, 8u);
    /* Draw custom Monios logo: stylized "M" */
    {
        uint16_t lx = (uint16_t)(start_x + 12);
        uint16_t ly = (uint16_t)(BUTTON_Y + 6);
        uint32_t logo_color = ui_color(0x0000897Bu, 0x004DB6ACu); /* Teal accent */
        graphics_fill_rect(lx, ly, 4, 16, logo_color);
        graphics_fill_rect((uint16_t)(lx + 16), ly, 4, 16, logo_color);
        for (uint16_t i = 0; i < 8; i++) {
            graphics_fill_rect((uint16_t)(lx + 4 + i), (uint16_t)(ly + 8 - i), 2, 2, logo_color);
        }
        for (uint16_t i = 0; i < 8; i++) {
            graphics_fill_rect((uint16_t)(lx + 12 + i), (uint16_t)(ly + i), 2, 2, logo_color);
        }
    }

    /* Pinned apps */
    task_x = (uint16_t)(start_x + START_BUTTON_WIDTH + 10);
    for (uint32_t i = 0; i < sizeof(g_taskbar_buttons)/sizeof(g_taskbar_buttons[0]); i++) {
        uint16_t x = (uint16_t)(task_x + i * (BUTTON_WIDTH + BUTTON_GAP));
        bool active = (i == g_active_button_index);
        uint32_t icon_id = ICON_START_FILES;
        if (i == 1) icon_id = ICON_FILE_CODE;
        else if (i == 2) icon_id = ICON_UI_SETTINGS;
        else if (i == 3) icon_id = ICON_UI_INFO;
        uint32_t pin_a = active ? 220u : (g_ux_hover.pinned == (int8_t) i
                            ? (g_ux_hover.pressed ? 90u : 120u) : 60u);
        graphics_fill_rounded_rect_alpha(x, BUTTON_Y, BUTTON_WIDTH, BUTTON_HEIGHT, hover_rgb, pin_a, 6u);
        graphics_blit_icon(icon_id, (uint16_t)(x + 10), (uint16_t)(BUTTON_Y + 5), 24u);
        if (active) {
            graphics_fill_rect((uint16_t)(x + 16), (uint16_t)(BUTTON_Y + BUTTON_HEIGHT - 2), 12, 2, accent);
        }
    }

    /* Running windows */
    task_x = graphics_taskbar_running_start_x();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        if (!g_windows[i].visible || g_windows[i].kind == UI_WINDOW_LOGON || g_windows[i].kind == UI_WINDOW_POWER) continue;
        if (task_x + UI_TASK_BUTTON_W >= FB_WIDTH - 184) break;
        uint32_t run_a = (g_ux_hover.running == (int32_t) i)
                       ? (g_ux_hover.pressed ? 90u : 120u) : 60u;
        graphics_fill_rounded_rect_alpha(task_x, BUTTON_Y, UI_TASK_BUTTON_W, BUTTON_HEIGHT, hover_rgb, run_a, 6u);
        graphics_blit_icon(graphics_png_icon_for_window(g_windows[i].kind),
                           (uint16_t)(task_x + 10), (uint16_t)(BUTTON_Y + 5), 24u);
        task_x = (uint16_t)(task_x + UI_TASK_BUTTON_W + 8);
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


/* =====================================================================
 * MoniOS Desktop Experience extensions (UI/UX group)
 * ===================================================================== */

#define UX_CLIP_HISTORY_MAX   10
#define UX_CLIP_ITEM_MAX      256
#define UX_NOTIFY_MAX         8
#define UX_NOTIFY_TITLE_MAX   32
#define UX_NOTIFY_BODY_MAX    72
#define UX_NOTIFY_SECONDS     5U
#define UX_NOTIFY_TOAST_W     320U
#define UX_NOTIFY_TOAST_H     56U
#define UX_MONITOR_MAX        4

typedef struct {
    bool active;
    uint32_t id;
    uint64_t post_tick;
    uint64_t expiry_tick;
    char title[UX_NOTIFY_TITLE_MAX];
    char body[UX_NOTIFY_BODY_MAX];
} ux_notify_t;

typedef struct {
    bool present;
    bool extended;
    uint16_t width;
    uint16_t height;
} ux_monitor_t;

static char g_ux_clip[UX_CLIP_HISTORY_MAX][UX_CLIP_ITEM_MAX];
static uint32_t g_ux_clip_count;
static uint32_t g_ux_clip_head;
static bool g_ux_clip_open;
static int32_t g_ux_clip_sel;

static ux_notify_t g_ux_notify[UX_NOTIFY_MAX];
static uint32_t g_ux_notify_next_id = 1U;

static uint8_t g_ux_vol __attribute__((unused)) = 70U;
static bool g_ux_muted = false;
static bool g_ux_ime_on = false;
static bool g_bt_panel_open = false;

static bool g_ux_dark = false;
static uint32_t g_ux_accent = 0x000078D4u;
static char g_ux_wallpaper_path[GRAPHICS_CLIPBOARD_PATH_MAX];

static ux_monitor_t g_ux_monitors[UX_MONITOR_MAX] = {
    { true, false, GRAPHICS_WIDTH, GRAPHICS_HEIGHT },
    { false, false, 0, 0 },
    { false, false, 0, 0 },
    { false, false, 0, 0 },
};

/* edge-snap hint while dragging a window: 0 none, 1 maximize(top),
 * 2 left-half, 3 right-half */
static uint8_t g_ux_snap_hint;
static uint64_t g_ux_titlebar_click_tick;
static uint32_t g_ux_titlebar_click_index = 0xFFFFFFFFU;

/* ---------------- theme helpers ---------------- */
uint32_t ux_accent_color(void)
{
    return g_ux_accent;
}

bool ux_dark_theme_active(void)
{
    return g_ux_dark;
}

/* Theme-aware color picker: returns light or dark palette entry. */
static uint32_t ui_color(uint32_t light_color, uint32_t dark_color)
{
    return g_ux_dark ? dark_color : light_color;
}

/* WinUI 3 luminance helpers. color is 0x00RRGGBB; percent is 0..100.
 * brighten fades the channel towards white; darken fades it towards black. */
static uint32_t graphics_brighten_color(uint32_t color, uint32_t percent)
{
    uint32_t r = (color >> 16) & 0xFFu;
    uint32_t g = (color >> 8) & 0xFFu;
    uint32_t b = color & 0xFFu;
    r += (255u - r) * percent / 100u;
    g += (255u - g) * percent / 100u;
    b += (255u - b) * percent / 100u;
    return (r << 16) | (g << 8) | b;
}

static uint32_t graphics_darken_color(uint32_t color, uint32_t percent)
{
    uint32_t r = (color >> 16) & 0xFFu;
    uint32_t g = (color >> 8) & 0xFFu;
    uint32_t b = color & 0xFFu;
    r -= r * percent / 100u;
    g -= g * percent / 100u;
    b -= b * percent / 100u;
    return (r << 16) | (g << 8) | b;
}

/* ---------------- clipboard history (syscall 44/45/46) ---------------- */
static void ux_clip_push(const char *text)
{
    uint32_t newest;

    if (text == NULL || text[0] == '\0') {
        return;
    }
    if (g_ux_clip_count > 0U) {
        newest = (g_ux_clip_head + UX_CLIP_HISTORY_MAX - 1U) % UX_CLIP_HISTORY_MAX;
        if (strcmp(g_ux_clip[newest], text) == 0) {
            return;
        }
    }
    strlcpy(g_ux_clip[g_ux_clip_head], text, UX_CLIP_ITEM_MAX);
    g_ux_clip_head = (g_ux_clip_head + 1U) % UX_CLIP_HISTORY_MAX;
    if (g_ux_clip_count < UX_CLIP_HISTORY_MAX) {
        g_ux_clip_count++;
    }
}

static const char *ux_clip_at(uint32_t index)
{
    uint32_t newest;

    if (index >= g_ux_clip_count) {
        return NULL;
    }
    newest = (g_ux_clip_head + UX_CLIP_HISTORY_MAX - 1U) % UX_CLIP_HISTORY_MAX;
    return g_ux_clip[(newest + UX_CLIP_HISTORY_MAX - index) % UX_CLIP_HISTORY_MAX];
}

/* Kernel-side syscall handlers for SYS_CLIPBOARD_SET / GET / HISTORY.
 * The dispatch switch in syscall.c is wired by the integration phase;
 * these entry points are the callable kernel implementations. */
void graphics_clipboard_set_text(const char *text)
{
    if (text != NULL) {
        strlcpy(g_clipboard_text, text, sizeof(g_clipboard_text));
        g_clipboard_mode = UI_CLIPBOARD_TEXT;
    }
    ux_clip_push(text);
    if (text != NULL && strchr(text, '\\') != NULL) {
        strlcpy(g_clipboard_path, text, sizeof(g_clipboard_path));
    }
}

uint32_t graphics_clipboard_get_text(char *buf, uint32_t cap)
{
    uint32_t n;

    if (buf == NULL || cap == 0U) {
        return (uint32_t) strlen(g_clipboard_text);
    }
    n = 0U;
    while (n + 1U < cap && g_clipboard_text[n] != '\0') {
        buf[n] = g_clipboard_text[n];
        n++;
    }
    buf[n] = '\0';
    return n;
}

/* Pack the clipboard history (newest first) into buf as NUL-separated entries.
 * Returns the number of bytes written (not including the trailing NUL). */
uint32_t graphics_clipboard_history_pack(char *buf, uint32_t cap)
{
    uint32_t off = 0U;

    if (buf == NULL || cap == 0U) {
        return 0U;
    }
    for (uint32_t i = 0U; i < g_ux_clip_count; i++) {
        const char *s = ux_clip_at(i);
        uint32_t l;

        if (s == NULL) {
            continue;
        }
        l = (uint32_t) strlen(s);
        if (off + l + 1U >= cap) {
            break;
        }
        memcpy(buf + off, s, l);
        off += l;
        buf[off++] = '\0';
    }
    buf[off] = '\0';
    return off;
}

uint32_t graphics_clipboard_history_count(void)
{
    return g_ux_clip_count;
}

const char *graphics_clipboard_history_at(uint32_t index)
{
    return ux_clip_at(index);
}

static void ux_toggle_clipboard_history(void)
{
    g_ux_clip_open = !g_ux_clip_open;
    g_ux_clip_sel = 0;
    graphics_draw_shell();
}

static void ux_draw_clipboard_history(void)
{
    uint32_t shown;
    uint16_t panel_w = 380U;
    uint16_t row_h = 24U;
    uint16_t panel_h;
    uint16_t px;
    uint16_t py;

    if (!g_ux_clip_open || g_ux_clip_count == 0U) {
        return;
    }
    shown = g_ux_clip_count > 10U ? 10U : g_ux_clip_count;
    panel_h = (uint16_t) (shown * row_h + 44U);
    px = (uint16_t) ((FB_WIDTH - panel_w) / 2U);
    py = (uint16_t) ((FB_HEIGHT - panel_h) / 2U);
    graphics_draw_shadow(px, py, panel_w, panel_h);
    graphics_fill_soft_rect(px, py, panel_w, panel_h, 0x00FFFFFFU);
    graphics_draw_soft_rect_outline(px, py, panel_w, panel_h, 0x00CFDAE8U);
    graphics_draw_text((uint16_t) (px + 14), (uint16_t) (py + 10), "Clipboard history", 0x001F2937U);
    graphics_draw_text((uint16_t) (px + 200), (uint16_t) (py + 10), "Win+V Enter=paste", 0x00566A80U);
    for (uint32_t i = 0U; i < shown; i++) {
        uint16_t ry = (uint16_t) (py + 34U + i * row_h);
        uint32_t fill = ((int32_t) i == g_ux_clip_sel) ? 0x00E7F0FFU : 0x00F6FAFFU;

        graphics_fill_soft_rect((uint16_t) (px + 8), ry, (uint16_t) (panel_w - 16), (uint16_t) (row_h - 2U), fill);
        graphics_draw_text_clipped((uint16_t) (px + 18), (uint16_t) (ry + 5), (uint16_t) (panel_w - 40),
                                   ux_clip_at(i), 0x001F2937U);
    }
}

/* ---------------- notification center (syscall 47) ---------------- */
static void ux_notify_expire(void)
{
    uint64_t now = timer_ticks();

    for (uint32_t i = 0U; i < UX_NOTIFY_MAX; i++) {
        if (g_ux_notify[i].active && now >= g_ux_notify[i].expiry_tick) {
            g_ux_notify[i].active = false;
        }
    }
}

static bool ux_notifications_alive(void)
{
    uint64_t now = timer_ticks();

    for (uint32_t i = 0U; i < UX_NOTIFY_MAX; i++) {
        if (g_ux_notify[i].active && now >= g_ux_notify[i].expiry_tick) {
            return true;
        }
    }
    return false;
}

/* Kernel-side handler for SYS_NOTIFICATION_POST. */
void graphics_notification_post(const char *title, const char *body)
{
    uint32_t slot = g_ux_notify_next_id % UX_NOTIFY_MAX;
    ux_notify_t *n = &g_ux_notify[slot];
    uint64_t hz = timer_hz();

    n->active = true;
    n->id = g_ux_notify_next_id++;
    n->post_tick = timer_ticks();
    n->expiry_tick = n->post_tick + (uint64_t) UX_NOTIFY_SECONDS * (hz == 0U ? 1U : hz);
    strlcpy(n->title, title != NULL ? title : "Notification", UX_NOTIFY_TITLE_MAX);
    strlcpy(n->body, body != NULL ? body : "", UX_NOTIFY_BODY_MAX);
    graphics_draw_shell();
}

static void ux_draw_notifications(void)
{
    uint64_t now = timer_ticks();
    uint16_t nx = (uint16_t) (FB_WIDTH - UX_NOTIFY_TOAST_W - 12U);
    uint16_t base_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - 12U);
    uint32_t shown = 0U;

    if (!g_session_logged_in) {
        return;
    }
    for (int32_t i = UX_NOTIFY_MAX - 1; i >= 0; i--) {
        ux_notify_t *n = &g_ux_notify[(uint32_t) i];
        uint16_t ny;

        if (!n->active) {
            continue;
        }
        if (now >= n->expiry_tick) {
            n->active = false;
            continue;
        }
        ny = (uint16_t) (base_y - shown * (UX_NOTIFY_TOAST_H + 8U));
        graphics_draw_shadow(nx, ny, UX_NOTIFY_TOAST_W, UX_NOTIFY_TOAST_H);
        graphics_fill_soft_rect(nx, ny, UX_NOTIFY_TOAST_W, UX_NOTIFY_TOAST_H, 0x00FFFFFFU);
        graphics_draw_soft_rect_outline(nx, ny, UX_NOTIFY_TOAST_W, UX_NOTIFY_TOAST_H, ux_accent_color());
        graphics_fill_rect(nx, ny, 4U, UX_NOTIFY_TOAST_H, ux_accent_color());
        graphics_draw_text((uint16_t) (nx + 12), (uint16_t) (ny + 8), n->title, 0x001F2937U);
        graphics_draw_text_clipped((uint16_t) (nx + 12), (uint16_t) (ny + 30), (uint16_t) (UX_NOTIFY_TOAST_W - 24U),
                                   n->body, 0x00566A80U);
        shown++;
    }
}

/* ---------------- theme / wallpaper / display / monitor (sys 48/49/50) ---------------- */
void graphics_theme_set(bool dark, uint32_t accent)
{
    char num[12];

    g_ux_dark = dark;
    g_ux_accent = accent;
    registry_set("ui.theme.dark", dark ? "1" : "0");
    graphics_u32_to_dec(num, accent);
    registry_set("ui.theme.accent", num);
    graphics_draw_shell();
}

static void ux_theme_load(void)
{
    const char *dark = registry_get("ui.theme.dark");
    const char *accent = registry_get("ui.theme.accent");

    g_ux_dark = (dark != NULL && dark[0] == '1');
    if (accent != NULL && accent[0] >= '0' && accent[0] <= '9') {
        uint32_t value = 0U;
        for (uint32_t i = 0U; accent[i] >= '0' && accent[i] <= '9'; i++) {
            value = value * 10U + (uint32_t) (accent[i] - '0');
        }
        if (value != 0U) {
            g_ux_accent = value;
        }
    }
}

void graphics_wallpaper_set(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        g_ux_wallpaper_path[0] = '\0';
        registry_set("ui.wallpaper.path", "");
    } else {
        strlcpy(g_ux_wallpaper_path, path, sizeof(g_ux_wallpaper_path));
        registry_set("ui.wallpaper.path", path);
    }
    /* drop the cached decode so the next frame reloads the new image */
    g_wallpaper_loaded = false;
    g_wallpaper_attempted = false;
    graphics_wallpaper_release();
    graphics_draw_shell();
}

static const char *ux_wallpaper_effective_path(void)
{
    if (g_ux_wallpaper_path[0] != '\0') {
        return g_ux_wallpaper_path;
    }
    return UI_WALLPAPER_PATH;
}

/* Kernel-side handler for SYS_DISPLAY_MODE. Modes that fit the backbuffer
 * are applied natively through the existing BGA/SVGA switch; larger modes
 * are recorded as a scaled-output request (software upscaling framework). */
bool graphics_display_mode_set(uint16_t width, uint16_t height)
{
    char label[24];

    if (width == 0U || height == 0U) {
        return false;
    }
    if (width <= GRAPHICS_MAX_WIDTH && height <= GRAPHICS_MAX_HEIGHT) {
        bool ok = graphics_set_resolution(width, height);
        graphics_make_resolution_label(label, sizeof(label), width, height);
        registry_set("ui.display.mode", label);
        graphics_notification_post("Display", ok ? "Resolution applied" : "Resolution unsupported");
        return ok;
    }
    /* oversized: keep internal render resolution, request scaled output */
    graphics_make_resolution_label(label, sizeof(label), width, height);
    registry_set("ui.display.mode", label);
    graphics_notification_post("Display", "Scaled output requested");
    return true;
}

uint32_t ux_monitor_count(void)
{
    uint32_t count = 0U;

    for (uint32_t i = 0U; i < UX_MONITOR_MAX; i++) {
        if (g_ux_monitors[i].present) {
            count++;
        }
    }
    return count == 0U ? 1U : count;
}

bool ux_monitor_extend(uint8_t index, uint16_t width, uint16_t height)
{
    if (index == 0U || index >= UX_MONITOR_MAX) {
        return false;
    }
    g_ux_monitors[index].present = true;
    g_ux_monitors[index].extended = true;
    g_ux_monitors[index].width = width;
    g_ux_monitors[index].height = height;
    registry_set("ui.monitors.extended", "1");
    graphics_notification_post("Display", "Extended monitor attached");
    return true;
}

static void ux_experience_init(void)
{
    const char *wp = registry_get("ui.wallpaper.path");

    ux_theme_load();
    if (wp != NULL && wp[0] != '\0') {
        strlcpy(g_ux_wallpaper_path, wp, sizeof(g_ux_wallpaper_path));
    }
}

/* ---------------- tray click handling ---------------- */
static bool ux_handle_tray_click(uint16_t x, uint16_t y)
{
    uint16_t base = (uint16_t) (FB_WIDTH - 212);
    uint16_t rx[5] = { (uint16_t) (base + 5),  (uint16_t) (base + 25),
                       (uint16_t) (base + 45), (uint16_t) (base + 65),
                       (uint16_t) (base + 85) };
    uint16_t ry = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT + 16);

    for (uint8_t i = 0U; i < 5U; i++) {
        if (!graphics_point_in_rect(x, y, rx[i], ry, 22U, 20U)) {
            continue;
        }
        if (i == 0U) {
            g_ux_muted = !g_ux_muted;
            graphics_notification_post("Volume", g_ux_muted ? "Muted" : "Audio on");
        } else if (i == 1U) {
            g_start_menu_open = false;
            g_bt_panel_open = false;
            graphics_open_start_menu_view(UI_START_MENU_NETWORK);
        } else if (i == 3U) {
            g_bt_panel_open = !g_bt_panel_open;
            if (g_bt_panel_open) {
                g_start_menu_open = false;
                if (bt_hci_is_ready()) {
                    bt_scan_start();
                    graphics_notification_post("Bluetooth", "Bluetooth: scanning...");
                } else {
                    graphics_notification_post("Bluetooth", "Bluetooth: not found");
                }
            }
        } else if (i == 4U) {
            g_ux_ime_on = !g_ux_ime_on;
            graphics_notification_post("Input", g_ux_ime_on ? "Chinese IME on" : "English");
        }
        graphics_draw_shell();
        return true;
    }
    return false;
}

/* Bluetooth device list panel that pops above the tray */
static void graphics_draw_bt_panel(void)
{
    const bluetooth_info_t *bt;
    bt_device_t devs[BT_MAX_DEVICES];
    int count;
    uint16_t panel_x = (uint16_t) (FB_WIDTH - 212);
    uint16_t w = 220U;
    uint16_t rows;
    uint16_t h;
    uint16_t y;
    uint32_t menu_rgb = ui_color(UI_LIGHT_MENU_RGB, UI_DARK_MENU_RGB);
    uint32_t text_col = ui_color(UI_LIGHT_TEXT_RGB, UI_DARK_TEXT_RGB);
    uint32_t sub_col = ui_color(UI_LIGHT_SUBTEXT_RGB, UI_DARK_SUBTEXT_RGB);

    if (!g_bt_panel_open || !g_session_logged_in) {
        return;
    }

    bt = bluetooth_info();
    count = bt_get_devices(devs, BT_MAX_DEVICES);
    if (count < 0) {
        count = 0;
    }
    rows = (uint16_t) count;
    if (rows > 6U) {
        rows = 6U;
    }

    h = (uint16_t) (34U + rows * 26U + 26U);
    y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - h - 8U);

    graphics_draw_shadow(panel_x, y, w, h);
    graphics_fill_soft_rect_alpha(panel_x, y, w, h, menu_rgb, UI_MENU_ALPHA);
    graphics_draw_soft_rect_outline(panel_x, y, w, h,
                                   ui_color(0x00FFFFFFu, 0x003A3A3Au));

    /* header */
    graphics_draw_text((uint16_t) (panel_x + 12), (uint16_t) (y + 10),
                       "Bluetooth", g_ux_accent);
    graphics_draw_text((uint16_t) (panel_x + w - 58), (uint16_t) (y + 11),
                       !bt->hci_ready ? "off" : (bt->scanning ? "scan" : "on"),
                       sub_col);

    /* device rows */
    if (count == 0) {
        graphics_draw_text((uint16_t) (panel_x + 12), (uint16_t) (y + 44),
                           bt->hci_ready ? "No devices" : "adapter unavailable",
                           sub_col);
    }
    for (int i = 0; i < (int) rows; i++) {
        char mac[BT_ADDR_STR_LEN];
        uint16_t ry = (uint16_t) (y + 34 + i * 26);

        graphics_fill_soft_rect_alpha((uint16_t) (panel_x + 6), ry,
                                      (uint16_t) (w - 12), 24,
                                      ui_color(0x00FFFFFFu, 0x002D2D2Du), 235u);
        graphics_draw_text((uint16_t) (panel_x + 12), (uint16_t) (ry + 2),
                           devs[i].name[0] != '\0' ? devs[i].name : "(unknown)",
                           text_col);
        bt_addr_to_string(devs[i].addr, mac, sizeof(mac));
        graphics_draw_text((uint16_t) (panel_x + 12), (uint16_t) (ry + 12), mac,
                           sub_col);
    }

    /* footer */
    graphics_draw_text((uint16_t) (panel_x + 12), (uint16_t) (y + h - 18),
                       bt->scanning ? "Scanning..." : "Tap BT icon to rescan",
                       sub_col);
}

static void graphics_draw_taskbar_status(void)
{
    cmos_time_t now;
    char time_text[9];
    char date_text[11];
    uint16_t panel_x = (uint16_t) (FB_WIDTH - 212);
    uint16_t panel_y = (uint16_t)(FB_HEIGHT - TASKBAR_HEIGHT + 7);
    ui_network_state_t net_state = ui_network_state();
    uint32_t vol_icon = ICON_STATUS_VOLUME_HIGH;
    uint32_t wifi_icon = ICON_STATUS_WIFI_OFF;
    uint32_t text_col = ui_color(UI_LIGHT_TEXT_RGB, UI_DARK_TEXT_RGB);
    uint32_t sub_col = ui_color(UI_LIGHT_SUBTEXT_RGB, UI_DARK_SUBTEXT_RGB);
    const bluetooth_info_t *bt = bluetooth_info();
    uint32_t bt_col = sub_col;

    cmos_read_time(&now);
    /* acrylic tray card */
    graphics_fill_rounded_rect_alpha(panel_x, panel_y, 200, 38,
                                 ui_color(UI_LIGHT_TASKBAR_RGB, UI_DARK_TASKBAR_RGB), UI_TASKBAR_ALPHA, 8u);

    /* volume icon by level */
    if (g_ux_muted) vol_icon = ICON_STATUS_VOLUME_MUTE;
    else if (g_ux_vol >= 66u) vol_icon = ICON_STATUS_VOLUME_HIGH;
    else if (g_ux_vol >= 33u) vol_icon = ICON_STATUS_VOLUME_MID;
    else vol_icon = ICON_STATUS_VOLUME_LOW;
    graphics_blit_icon(vol_icon, (uint16_t)(panel_x + 6), (uint16_t)(panel_y + 11), 16u);

    /* wi-fi icon by signal */
    if (net_state == UI_NETWORK_CONNECTED) wifi_icon = ICON_STATUS_WIFI_STRONG;
    else if (net_state == UI_NETWORK_LIMITED) wifi_icon = ICON_STATUS_WIFI_MID;
    else if (net_state == UI_NETWORK_VALIDATING) wifi_icon = ICON_STATUS_WIFI_WEAK;
    graphics_blit_icon(wifi_icon, (uint16_t)(panel_x + 26), (uint16_t)(panel_y + 11), 16u);

    /* battery (no fuel-gauge driver present; show full) */
    graphics_blit_icon(ICON_STATUS_BATTERY_FULL, (uint16_t)(panel_x + 46), (uint16_t)(panel_y + 11), 16u);

    /* bluetooth indicator between battery and IME */
    if (bt->hci_ready) {
        bool any_connected = false;
        for (uint32_t i = 0; i < bt->device_count; i++) {
            if (bt->devices[i].connected) {
                any_connected = true;
            }
        }
        bt_col = any_connected ? g_ux_accent : text_col;
    }
    graphics_draw_text((uint16_t)(panel_x + 66), (uint16_t)(panel_y + 13), "BT", bt_col);

    /* input-method indicator */
    graphics_draw_text((uint16_t)(panel_x + 88), (uint16_t)(panel_y + 13),
                       g_ux_ime_on ? "\u62fc" : "EN", g_ux_ime_on ? g_ux_accent : sub_col);

    graphics_two_digits(&time_text[0], now.hour);
    time_text[2] = ':';
    graphics_two_digits(&time_text[3], now.minute);
    time_text[5] = '\0';

    graphics_four_digits(&date_text[0], now.year);
    date_text[4] = '-';
    graphics_two_digits(&date_text[5], now.month);
    date_text[7] = '-';
    graphics_two_digits(&date_text[8], now.day);
    date_text[10] = '\0';

    graphics_draw_text((uint16_t)(panel_x + 114), (uint16_t)(panel_y + 4), time_text, text_col);
    graphics_draw_text((uint16_t)(panel_x + 114), (uint16_t)(panel_y + 21), date_text, sub_col);
}

static void graphics_draw_start_menu(void)
{
    if (!g_session_logged_in || !g_start_menu_open) {
        return;
    }

    static const char *root_items[] = { "\u5e94\u7528", "\u8bbe\u7f6e", "\u7f51\u7edc", "\u7535\u6e90" };
    static const uint32_t root_icons[] = { ICON_START_ALL_APPS, ICON_START_SETTINGS, ICON_NET_WIFI, ICON_UI_POWER };
    static const char *apps_items[] = { "\u6587\u4ef6", "\u7ec8\u7aef", "\u64ad\u653e\u5668", "\u8bb0\u4e8b\u672c", "\u4efb\u52a1\u7ba1\u7406\u5668" };
    static const uint32_t apps_icons[] = { ICON_START_FILES, ICON_FILE_CODE, ICON_FILE_AUDIO, ICON_FILE_TEXT, ICON_UI_HOME };
    static const char *settings_items[] = { "\u663e\u793a", "\u6307\u9488", "\u7f51\u7edc" };
    static const uint32_t settings_icons[] = { ICON_UI_BRIGHTNESS, ICON_UI_EDIT, ICON_NET_WIFI };
    static const char *power_items[] = { "\u5173\u673a", "\u91cd\u542f", "\u4f11\u7720" };
    static const uint32_t power_icons[] = { ICON_START_SHUTDOWN, ICON_START_RESTART, ICON_START_SLEEP };
    uint16_t menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - START_MENU_H - 12);
    uint16_t root_top = (uint16_t) (menu_y + START_MENU_HEADER_H + 8);
    uint16_t left_x = (uint16_t) (START_MENU_X + 8);
    uint16_t mid_x = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + 12);
    uint16_t right_x = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + START_MENU_MID_W + 16);
    uint32_t menu_rgb = ui_color(UI_LIGHT_MENU_RGB, UI_DARK_MENU_RGB);
    uint32_t text_col = ui_color(UI_LIGHT_TEXT_RGB, UI_DARK_TEXT_RGB);
    uint32_t sub_col = ui_color(UI_LIGHT_SUBTEXT_RGB, UI_DARK_SUBTEXT_RGB);
    uint32_t row_rgb = ui_color(0x00FFFFFFu, 0x002D2D2Du);

    /* Win11 acrylic rounded card */
    graphics_draw_shadow(START_MENU_X, menu_y, START_MENU_W, START_MENU_H);
    graphics_fill_rounded_rect_alpha(START_MENU_X, menu_y, START_MENU_W, START_MENU_H, menu_rgb, UI_MENU_ALPHA, 16u);
    graphics_draw_rounded_rect_outline(START_MENU_X, menu_y, START_MENU_W, START_MENU_H,
                                   ui_color(0x00FFFFFFu, 0x003A3A3Au), 16u);

    /* search box in header */
    graphics_fill_rounded_rect_alpha((uint16_t)(START_MENU_X + 12), (uint16_t)(menu_y + 8),
                                  (uint16_t)(START_MENU_W - 64), 22, row_rgb, 235u, 8u);
    graphics_blit_icon(ICON_UI_SEARCH, (uint16_t)(START_MENU_X + 18), (uint16_t)(menu_y + 11), 16u);
    graphics_draw_text((uint16_t)(START_MENU_X + 40), (uint16_t)(menu_y + 13), "\u641c\u7d22", sub_col);
    /* theme toggle (sun/moon) at header right, with WinUI 3 hover pill */
    if (g_ux_hover.theme) {
        graphics_fill_rounded_rect_alpha((uint16_t)(START_MENU_X + START_MENU_W - 38), (uint16_t)(menu_y + 6),
                                         26, 24, row_rgb, g_ux_hover.pressed ? 150u : 200u, 8u);
    }
    graphics_blit_icon(g_ux_dark ? ICON_UI_SUN : ICON_UI_MOON,
                       (uint16_t)(START_MENU_X + START_MENU_W - 34), (uint16_t)(menu_y + 9), 20u);

    for (uint32_t i = 0; i < 4; i++) {
        uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
        bool root_selected = (g_start_menu_view == UI_START_MENU_APPS && i == 0) ||
                             (g_start_menu_view == UI_START_MENU_SETTINGS && i == 1) ||
                             (g_start_menu_view == UI_START_MENU_NETWORK && i == 2) ||
                             (g_start_menu_view == UI_START_MENU_POWER && i == 3);

        uint32_t root_a = root_selected ? 235u : (g_ux_hover.root == (int8_t) i
                            ? (g_ux_hover.pressed ? 170u : 200u) : 120u);
        graphics_fill_rounded_rect_alpha(left_x, row_y, START_MENU_LEFT_W - 16, UI_MENU_ITEM_H - 2, row_rgb, root_a, 4u);
        if (root_selected) {
            graphics_fill_rect(left_x, (uint16_t) (row_y + 4), 3, (uint16_t) (UI_MENU_ITEM_H - 10), g_ux_accent);
        }
        graphics_blit_icon(root_icons[i], (uint16_t)(left_x + 8), (uint16_t)(row_y + 3), 18u);
        graphics_draw_text((uint16_t) (left_x + 32), (uint16_t) (row_y + 6), root_items[i], text_col);
    }

    if (g_start_menu_view == UI_START_MENU_APPS || g_start_menu_view == UI_START_MENU_SETTINGS ||
        g_start_menu_view == UI_START_MENU_DISPLAY || g_start_menu_view == UI_START_MENU_CURSOR ||
        g_start_menu_view == UI_START_MENU_NETWORK || g_start_menu_view == UI_START_MENU_POWER) {
        graphics_fill_rounded_rect_alpha(mid_x, root_top, START_MENU_MID_W - 12, 5 * UI_MENU_ITEM_H - 2, row_rgb, 200u, 4u);
    }

    /* WinUI 3 hover row highlight for the active content column */
    {
        uint16_t hx = mid_x;
        uint16_t hw = START_MENU_MID_W - 12;
        uint32_t hcount = 0;
        uint8_t hr = (uint8_t) g_ux_hover.content;
        if (g_start_menu_view == UI_START_MENU_DISPLAY) {
            hx = right_x; hw = START_MENU_RIGHT_W - 20;
            hcount = (uint32_t)(sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]));
        } else if (g_start_menu_view == UI_START_MENU_APPS) {
            hcount = 5u;
        } else if (g_start_menu_view == UI_START_MENU_SETTINGS) {
            hcount = 3u;
        } else if (g_start_menu_view == UI_START_MENU_NETWORK) {
            hcount = 2u;
        } else if (g_start_menu_view == UI_START_MENU_POWER) {
            hcount = 3u;
        }
        if (g_start_menu_view == UI_START_MENU_DISPLAY && !g_ux_hover.content_right) hcount = 0;
        if (g_start_menu_view != UI_START_MENU_DISPLAY && g_ux_hover.content_right) hcount = 0;
        if (g_ux_hover.content >= 0 && hr < hcount) {
            uint16_t row_y = (uint16_t) (root_top + hr * UI_MENU_ITEM_H);
            graphics_fill_rounded_rect_alpha(hx, row_y, hw, UI_MENU_ITEM_H - 2, row_rgb,
                                            g_ux_hover.pressed ? 150u : 190u, 4u);
        }
    }

    if (g_start_menu_view == UI_START_MENU_APPS) {
        for (uint32_t i = 0; i < sizeof(apps_items) / sizeof(apps_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_blit_icon(apps_icons[i], (uint16_t)(mid_x + 8), (uint16_t)(row_y + 3), 18u);
            graphics_draw_text((uint16_t) (mid_x + 32), (uint16_t) (row_y + 6), apps_items[i], text_col);
        }
    } else if (g_start_menu_view == UI_START_MENU_SETTINGS) {
        for (uint32_t i = 0; i < sizeof(settings_items) / sizeof(settings_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_blit_icon(settings_icons[i], (uint16_t)(mid_x + 8), (uint16_t)(row_y + 3), 18u);
            graphics_draw_text((uint16_t) (mid_x + 32), (uint16_t) (row_y + 6), settings_items[i], text_col);
        }
    } else if (g_start_menu_view == UI_START_MENU_DISPLAY) {
        for (uint32_t i = 0; i < sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            uint32_t fill = i == g_graphics_mode_index ? g_ux_accent : row_rgb;
            graphics_fill_soft_rect(right_x, row_y, START_MENU_RIGHT_W - 20, UI_MENU_ITEM_H - 2, fill);
            graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (row_y + 6), g_graphics_modes[i].label, i == g_graphics_mode_index ? 0x00FFFFFF : text_col);
        }
    } else if (g_start_menu_view == UI_START_MENU_CURSOR) {
        for (uint32_t i = 0; i < graphics_cursor_style_count(); i++) {
            uint16_t col = (uint16_t) (i & 1u);
            uint16_t row = (uint16_t) (i >> 1);
            uint16_t button_x = (uint16_t) (right_x + 12 + col * 82);
            uint16_t button_y = (uint16_t) (root_top + row * 28);
            uint32_t fill = i == g_cursor_style_index ? g_ux_accent : row_rgb;

            graphics_fill_soft_rect(button_x, button_y, 70, 22, fill);
            graphics_draw_text_aligned(button_x, (uint16_t) (button_y + 6), 70, g_cursor_styles[i].label, i == g_cursor_style_index ? 0x00FFFFFF : text_col);
        }
    } else if (g_start_menu_view == UI_START_MENU_NETWORK) {
        graphics_fill_rounded_rect_alpha(right_x, root_top, START_MENU_RIGHT_W - 20, 5 * UI_MENU_ITEM_H - 2, row_rgb, 200u, 4u);
        graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (root_top + 6), "ping 172.16.58.1", text_col);
        graphics_draw_text((uint16_t) (right_x + 12), (uint16_t) (root_top + 30), "control panel", text_col);
    } else if (g_start_menu_view == UI_START_MENU_POWER) {
        for (uint32_t i = 0; i < sizeof(power_items) / sizeof(power_items[0]); i++) {
            uint16_t row_y = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            graphics_blit_icon(power_icons[i], (uint16_t)(mid_x + 8), (uint16_t)(row_y + 3), 18u);
            graphics_draw_text((uint16_t) (mid_x + 32), (uint16_t) (row_y + 6), power_items[i], text_col);
        }
    }
}


static void graphics_properties_geometry(uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    *w = UI_PROP_DIALOG_W;
    *h = UI_PROP_DIALOG_H;
    *x = FB_WIDTH > UI_PROP_DIALOG_W ? (uint16_t) ((FB_WIDTH - UI_PROP_DIALOG_W) / 2) : 0;
    *y = FB_HEIGHT > UI_PROP_DIALOG_H ? (uint16_t) ((FB_HEIGHT - UI_PROP_DIALOG_H) / 2) : 0;
}

/* ---- Properties dialog helpers ---- */
static void prop_append_char(char *out, uint32_t *pos, uint32_t cap, char c)
{
    if (*pos + 1U < cap) {
        out[(*pos)++] = c;
        out[*pos] = '\0';
    }
}

static void prop_append2(char *out, uint32_t *pos, uint32_t cap, uint32_t v)
{
    char tmp[12];
    uint32_t i;

    graphics_u32_to_dec(tmp, v);
    if (v < 10U) {
        prop_append_char(out, pos, cap, '0');
    }
    i = 0U;
    while (tmp[i] != '\0') {
        prop_append_char(out, pos, cap, tmp[i++]);
    }
}

/* Format a unix timestamp (seconds since 1970-01-01 UTC) as
 * "YYYY-MM-DD HH:MM:SS". A zero timestamp renders as "Unknown". */
static void graphics_format_datetime(uint64_t unix, char *out, uint32_t out_size)
{
    uint64_t days;
    uint64_t rem;
    long z;
    long era;
    long doe;
    long yoe;
    long y;
    long doy;
    long mp;
    long d;
    long m;
    uint32_t hh;
    uint32_t mm;
    uint32_t ss;
    uint32_t pos = 0U;
    char num[12];

    if (out == NULL || out_size == 0U) {
        return;
    }
    out[0] = '\0';
    if (unix == 0U) {
        strlcpy(out, "Unknown", out_size);
        return;
    }
    days = unix / 86400U;
    rem = unix % 86400U;
    hh = (uint32_t) (rem / 3600U);
    mm = (uint32_t) ((rem % 3600U) / 60U);
    ss = (uint32_t) (rem % 60U);

    z = (long) days + 719468L;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365L;
    y = yoe + era * 400L;
    doy = doe - (365L * yoe + yoe / 4L - yoe / 100L);
    mp = (5L * doy + 2L) / 153L;
    d = doy - (153L * mp + 2L) / 5L + 1L;
    m = mp + (mp < 10L ? 3L : -9L);
    y += (m > 2L ? 0L : 1L);

    graphics_u32_to_dec(num, (uint32_t) y);
    {
        uint32_t i = 0U;
        while (num[i] != '\0') {
            prop_append_char(out, &pos, out_size, num[i++]);
        }
    }
    prop_append_char(out, &pos, out_size, '-');
    prop_append2(out, &pos, out_size, (uint32_t) m);
    prop_append_char(out, &pos, out_size, '-');
    prop_append2(out, &pos, out_size, (uint32_t) d);
    prop_append_char(out, &pos, out_size, ' ');
    prop_append2(out, &pos, out_size, hh);
    prop_append_char(out, &pos, out_size, ':');
    prop_append2(out, &pos, out_size, mm);
    prop_append_char(out, &pos, out_size, ':');
    prop_append2(out, &pos, out_size, ss);
}

/* Populate the editable state from the filesystem. Called once when the
 * dialog opens so the per-frame draw path does no disk I/O. */
static void graphics_properties_refresh_state(void)
{
    file_stat_t st;

    strlcpy(g_properties_edit_name, g_properties_name, sizeof(g_properties_edit_name));
    strlcpy(g_properties_orig_name, g_properties_name, sizeof(g_properties_orig_name));
    g_properties_editing = false;
    g_properties_readonly = false;
    g_properties_hidden = false;
    g_properties_orig_attr = 0U;
    g_properties_ctime = 0U;
    g_properties_mtime = 0U;
    g_properties_atime = 0U;
    g_properties_size = 0U;
    if (file_stat(g_properties_path, &st)) {
        g_properties_orig_attr = st.attr;
        g_properties_readonly = (st.attr & 0x01U) != 0U;
        g_properties_hidden = (st.attr & 0x02U) != 0U;
        g_properties_ctime = st.create_time;
        g_properties_mtime = st.modify_time;
        g_properties_atime = st.access_time;
        g_properties_size = st.size;
    }
}

/* Apply edits on OK: rename the file if the name changed, and write back
 * the read-only / hidden attribute bits. */
static void graphics_properties_commit(void)
{
    char newpath[GRAPHICS_CLIPBOARD_PATH_MAX];
    char *slash;
    uint8_t want_attr;
    uint8_t have_attr;

    if (g_properties_edit_name[0] != '\0' &&
        strcmp(g_properties_edit_name, g_properties_orig_name) != 0) {
        strlcpy(newpath, g_properties_path, sizeof(newpath));
        slash = strrchr(newpath, '\\');
        if (slash != NULL && slash > newpath) {
            slash[1] = '\0';
            graphics_append_path_component(newpath, sizeof(newpath),
                                           g_properties_edit_name);
            if (file_rename(g_properties_path, newpath)) {
                strlcpy(g_properties_path, newpath, sizeof(g_properties_path));
                strlcpy(g_properties_name, g_properties_edit_name, sizeof(g_properties_name));
                strlcpy(g_properties_orig_name, g_properties_edit_name,
                        sizeof(g_properties_orig_name));
            }
        }
    }

    want_attr = (uint8_t) ((g_properties_readonly ? 0x01U : 0U) |
                           (g_properties_hidden ? 0x02U : 0U));
    have_attr = (uint8_t) (g_properties_orig_attr & 0x03U);
    if (want_attr != have_attr) {
        file_set_attr(g_properties_path, 0x03U, want_attr);
    }

    g_properties_open = false;
    g_properties_editing = false;
}

static void graphics_draw_properties_overlay(void)
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t ex;
    uint16_t ey;
    uint16_t ew;
    uint16_t eh;
    char size_text[24];
    char type_text[32];
    char dir_path[GRAPHICS_CLIPBOARD_PATH_MAX];
    char ct[24];
    char mt[24];
    char at[24];
    char *slash;

    if (!g_properties_open || !g_session_logged_in) {
        return;
    }
    graphics_properties_geometry(&x, &y, &w, &h);
    graphics_fill_rect(0, 0, FB_WIDTH, FB_HEIGHT, 0x00304050);
    graphics_draw_shadow(x, y, w, h);
    graphics_fill_soft_rect(x, y, w, h, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(x, y, w, h, 0x00BFCEDF);

    /* Editable filename field */
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 17),
                       "\u6587\u4ef6\u540d\uff1a", 0x005D6B7A);
    ex = (uint16_t) (x + 80);
    ey = (uint16_t) (y + 12);
    ew = (uint16_t) (w - 96);
    eh = 22U;
    graphics_fill_rect(ex, ey, ew, eh, 0x00FFFFFF);
    graphics_draw_rect_outline(ex, ey, ew, eh,
                               g_properties_editing ? 0x002A6CC8 : 0x00999999);
    graphics_draw_text_clipped((uint16_t) (ex + 6), (uint16_t) (ey + 5),
                               (uint16_t) (ew - 10), g_properties_edit_name, 0x00111111);
    if (g_properties_editing) {
        uint32_t tw = graphics_text_width(g_properties_edit_name);
        uint16_t cx = (uint16_t) (ex + 6U + tw);
        uint16_t max_cx = (uint16_t) (ex + ew - 4U);
        if (cx > max_cx) {
            cx = max_cx;
        }
        graphics_fill_rect(cx, (uint16_t) (ey + 4), 2U, (uint16_t) (eh - 8U), 0x002A6CC8);
    }

    /* Type */
    if (g_properties_is_dir) {
        strlcpy(type_text, "\u6587\u4ef6\u5939", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".exe") || graphics_path_has_suffix(g_properties_path, ".EXE")) {
        strlcpy(type_text, "\u5e94\u7528\u7a0b\u5e8f", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".dll") || graphics_path_has_suffix(g_properties_path, ".DLL")) {
        strlcpy(type_text, "\u52a8\u6001\u94fe\u63a5\u5e93", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".mp4") || graphics_path_has_suffix(g_properties_path, ".avi") ||
               graphics_path_has_suffix(g_properties_path, ".mkv") || graphics_path_has_suffix(g_properties_path, ".wmv") ||
               graphics_path_has_suffix(g_properties_path, ".mov")) {
        strlcpy(type_text, "\u89c6\u9891\u6587\u4ef6", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".zip") || graphics_path_has_suffix(g_properties_path, ".rar") ||
               graphics_path_has_suffix(g_properties_path, ".7z")) {
        strlcpy(type_text, "\u538b\u7f29\u6587\u4ef6", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".txt") || graphics_path_has_suffix(g_properties_path, ".c") ||
               graphics_path_has_suffix(g_properties_path, ".h") || graphics_path_has_suffix(g_properties_path, ".py")) {
        strlcpy(type_text, "\u6587\u672c\u6587\u6863", sizeof(type_text));
    } else if (graphics_path_has_suffix(g_properties_path, ".jpg") || graphics_path_has_suffix(g_properties_path, ".png") ||
               graphics_path_has_suffix(g_properties_path, ".bmp")) {
        strlcpy(type_text, "\u56fe\u7247\u6587\u4ef6", sizeof(type_text));
    } else {
        strlcpy(type_text, "\u6587\u4ef6", sizeof(type_text));
    }
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 46), "\u7c7b\u578b\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 46), type_text, 0x0017232E);

    /* Location */
    strlcpy(dir_path, g_properties_path, sizeof(dir_path));
    slash = strrchr(dir_path, '\\');
    if (slash != NULL && slash > dir_path) {
        slash[1] = '\0';
    }
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 68), "\u4f4d\u7f6e\uff1a", 0x005D6B7A);
    graphics_draw_text_clipped((uint16_t) (x + 80), (uint16_t) (y + 68),
                               (uint16_t) (w - 96), dir_path, 0x0017232E);

    /* Size */
    if (g_properties_is_dir) {
        strlcpy(size_text, "-", sizeof(size_text));
    } else {
        graphics_format_file_size((int32_t) g_properties_size, size_text, sizeof(size_text));
    }
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 90), "\u5927\u5c0f\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 90), size_text, 0x0017232E);

    /* Timestamps */
    graphics_format_datetime(g_properties_ctime, ct, sizeof(ct));
    graphics_format_datetime(g_properties_mtime, mt, sizeof(mt));
    graphics_format_datetime(g_properties_atime, at, sizeof(at));
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 112), "\u521b\u5efa\u65f6\u95f4\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 112), ct, 0x0017232E);
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 134), "\u4fee\u6539\u65f6\u95f4\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 134), mt, 0x0017232E);
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 156), "\u8bbf\u95ee\u65f6\u95f4\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 156), at, 0x0017232E);

    /* Owner (single-user system) */
    graphics_draw_text((uint16_t) (x + 16), (uint16_t) (y + 178), "\u5c5e\u4e3b\uff1a", 0x005D6B7A);
    graphics_draw_text((uint16_t) (x + 80), (uint16_t) (y + 178), "root", 0x0017232E);

    /* Attribute checkboxes */
    graphics_draw_rect_outline((uint16_t) (x + 16), (uint16_t) (y + 206), 14U, 14U, 0x008AA1B8);
    if (g_properties_readonly) {
        graphics_fill_rect((uint16_t) (x + 19), (uint16_t) (y + 209), 8U, 8U, 0x002A6CC8);
    }
    graphics_draw_text((uint16_t) (x + 36), (uint16_t) (y + 208), "\u53ea\u8bfb", 0x0017232E);
    graphics_draw_rect_outline((uint16_t) (x + 120), (uint16_t) (y + 206), 14U, 14U, 0x008AA1B8);
    if (g_properties_hidden) {
        graphics_fill_rect((uint16_t) (x + 123), (uint16_t) (y + 209), 8U, 8U, 0x002A6CC8);
    }
    graphics_draw_text((uint16_t) (x + 140), (uint16_t) (y + 208), "\u9690\u85cf", 0x0017232E);

    /* OK button */
    {
        uint16_t btn_w = 80U;
        uint16_t btn_h = 24U;
        uint16_t btn_x = (uint16_t) (x + (w - btn_w) / 2U);
        uint16_t btn_y = (uint16_t) (y + h - 40U);
        graphics_fill_soft_rect(btn_x, btn_y, btn_w, btn_h, UI_COLOR_ACCENT);
        graphics_draw_soft_rect_outline(btn_x, btn_y, btn_w, btn_h, 0x00B7D8FF);
        graphics_draw_text_aligned(btn_x, (uint16_t) (btn_y + 5), btn_w,
                                   "\u786e\u5b9a", 0x00FFFFFF);
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
    graphics_fill_soft_rect(g_context_menu_x, g_context_menu_y, CONTEXT_MENU_W, menu_h, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(g_context_menu_x, g_context_menu_y, CONTEXT_MENU_W, menu_h, 0x00BFCEDF);
    if (g_context_menu_mode == UI_CONTEXT_MENU_FILES) {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u590d\u5236", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u526a\u5207", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), g_clipboard_mode != UI_CLIPBOARD_NONE ? "\u7c98\u8d34" : "-", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u521b\u5efa\u5feb\u6377\u65b9\u5f0f", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 110), "\u6253\u5f00", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 134), "\u5220\u9664", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 158), "\u91cd\u547d\u540d", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 182), "\u5c5e\u6027", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 206), "\u65b0\u5efa  >", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 230), "\u6253\u5f00\u65b9\u5f0f  >", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 254), "\u53d1\u9001\u5230  >", UI_COLOR_TITLE_TEXT);
    } else {
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 14), "\u5237\u65b0", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 38), "\u8fd0\u884c", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 62), "\u8bbe\u7f6e", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 86), "\u6ce8\u9500", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 110), "\u65b0\u5efa\u6587\u4ef6\u5939", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 134), "\u65b0\u5efa\u6587\u672c\u6587\u6863", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 158), "\u663e\u793a\u8bbe\u7f6e", UI_COLOR_TITLE_TEXT);
        graphics_draw_text((uint16_t) (g_context_menu_x + 14), (uint16_t) (g_context_menu_y + 182), "\u4e2a\u6027\u5316", UI_COLOR_TITLE_TEXT);
    }

    /* Cascading submenu for the FILES menu. */
    if (g_context_menu_mode == UI_CONTEXT_MENU_FILES && g_ctx_submenu != UI_CTX_SUB_NONE) {
        uint16_t sx = (uint16_t) (g_context_menu_x + CONTEXT_MENU_W - 4);
        uint16_t sy = (uint16_t) (g_context_menu_y + 206);
        uint16_t sh = 78U;

        graphics_draw_shadow(sx, sy, 130U, sh);
        graphics_fill_soft_rect(sx, sy, 130U, sh, 0x00FFFFFF);
        graphics_draw_soft_rect_outline(sx, sy, 130U, sh, 0x00BFCEDF);
        if (g_ctx_submenu == UI_CTX_SUB_NEW) {
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 8), "\u6587\u672c\u6587\u6863", UI_COLOR_TITLE_TEXT);
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 32), "\u6587\u4ef6\u5939", UI_COLOR_TITLE_TEXT);
        } else if (g_ctx_submenu == UI_CTX_SUB_OPENWITH) {
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 8), "\u8bb0\u4e8b\u672c", UI_COLOR_TITLE_TEXT);
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 32), "\u56fe\u7247\u67e5\u770b\u5668", UI_COLOR_TITLE_TEXT);
        } else {
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 8), "\u684c\u9762\u5feb\u6377\u65b9\u5f0f", UI_COLOR_TITLE_TEXT);
            graphics_draw_text((uint16_t) (sx + 10), (uint16_t) (sy + 32), "\u6587\u6863\u6587\u4ef6\u5939", UI_COLOR_TITLE_TEXT);
        }
    }
}

static void graphics_draw_power_menu(void)
{
    if (!g_power_menu_open) {
        return;
    }

    graphics_draw_shadow(g_power_menu_x, g_power_menu_y, 170, 104);
    graphics_fill_soft_rect(g_power_menu_x, g_power_menu_y, 170, 104, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(g_power_menu_x, g_power_menu_y, 170, 104, 0x00BFCEDF);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 14), "\u5173\u673a", UI_COLOR_TITLE_TEXT);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 38), "\u91cd\u542f", UI_COLOR_TITLE_TEXT);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 62), "\u4f11\u7720", UI_COLOR_TITLE_TEXT);
    graphics_draw_text((uint16_t) (g_power_menu_x + 14), (uint16_t) (g_power_menu_y + 86), "\u6ce8\u9500", UI_COLOR_TITLE_TEXT);
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
    graphics_login_layout_t layout;
    uint32_t user_count;
    uint16_t user_gap = 8;
    uint32_t accent = UI_COLOR_ACCENT;
    uint32_t password_border;
    bool wallpaper_loaded;

    if (window == NULL) {
        return;
    }
    graphics_login_layout(&layout);
    user_count = session_user_count();
    if (user_count > SESSION_USER_MAX) {
        user_count = SESSION_USER_MAX;
    }
    if (g_selected_login_user >= user_count && user_count > 0) {
        g_selected_login_user = 0;
    }

    wallpaper_loaded = graphics_draw_wallpaper_asset(FB_HEIGHT);
    if (!wallpaper_loaded) {
        graphics_fill_vertical_gradient(0x00152C4C, 0x00081318);
        graphics_fill_rect_gradient(0, 0, FB_WIDTH, 116, 0x001B385A, 0x000D1E32);
        graphics_fill_rect_gradient(0, (uint16_t) (FB_HEIGHT > 132 ? FB_HEIGHT - 132 : 0),
                                    FB_WIDTH, 132, 0x000D1E32, 0x00204770);
    } else {
        graphics_fill_rect_gradient(0, 0, FB_WIDTH, 94, 0x00101D2D, 0x000F1B2A);
        graphics_fill_rect_gradient(0, (uint16_t) (FB_HEIGHT > 108 ? FB_HEIGHT - 108 : 0),
                                    FB_WIDTH, 108, 0x000F1B2A, 0x00234467);
    }

    graphics_draw_text(28, 28, "MONIOS", 0x00FFFFFF);
    graphics_draw_text_clipped((uint16_t) (FB_WIDTH > 220 ? FB_WIDTH - 190 : 28), 28, 162,
                               "Secure sign-in", 0x00D5E8FF);

    graphics_draw_shadow(layout.card_x, layout.card_y, layout.card_width, layout.card_height);
    graphics_fill_soft_rect(layout.card_x, layout.card_y, layout.card_width, layout.card_height, 0x00F7FBFF);
    graphics_draw_soft_rect_outline(layout.card_x, layout.card_y, layout.card_width, layout.card_height, 0x00D4E2F0);

    if (layout.brand_width > 0) {
        graphics_fill_rect_gradient((uint16_t) (layout.card_x + 2), (uint16_t) (layout.card_y + 2),
                                    layout.brand_width, (uint16_t) (layout.card_height - 4),
                                    0x002C6FCB, 0x0015294E);
        graphics_fill_soft_rect((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 46),
                                74, 74, 0x003C6FEA);
        graphics_draw_soft_rect_outline((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 46),
                                         74, 74, 0x009FD0FF);
        graphics_draw_login_avatar((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 46),
                                   74, NULL, 0x003C6FEA, 0x00FFFFFF);
        graphics_draw_text((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 152),
                           "Welcome back", 0x00FFFFFF);
        graphics_draw_text_clipped((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 182),
                                   (uint16_t) (layout.brand_width - 54),
                                   "A modern desktop for your devices.", 0x00D5E8FF);
        graphics_draw_text_clipped((uint16_t) (layout.card_x + 34), (uint16_t) (layout.card_y + 224),
                                   (uint16_t) (layout.brand_width - 54),
                                   "Secure by default. Ready when you are.", 0x00B8D3F3);
        graphics_draw_text_clipped((uint16_t) (layout.card_x + 34),
                                   (uint16_t) (layout.card_y + layout.card_height - 44),
                                   (uint16_t) (layout.brand_width - 54),
                                   "MoniOS OSUI", 0x00B8D3F3);
    }

    graphics_draw_text(layout.form_x, (uint16_t) (layout.card_y + 36), "Welcome back", 0x001C2430);
    graphics_draw_text_clipped(layout.form_x, (uint16_t) (layout.card_y + 62), layout.form_width,
                               "Sign in to continue to your desktop.", 0x005D6B7A);
    graphics_draw_text(layout.form_x, (uint16_t) (layout.card_y + 88), "Choose an account", 0x005D6B7A);

    for (uint32_t i = 0; i < user_count; i++) {
        const session_user_t *user = session_user_at(i);
        uint16_t tile_x = (uint16_t) (layout.form_x + i * (layout.user_tile_width + user_gap));
        uint32_t fill = i == g_selected_login_user ? 0x00E7F0FF : 0x00FFFFFF;
        uint32_t border = i == g_selected_login_user ? accent : 0x00D3DFEC;
        uint32_t text = i == g_selected_login_user ? 0x001E4D9A : 0x003C4A5A;

        if (user == NULL) {
            continue;
        }
        graphics_fill_soft_rect(tile_x, layout.users_y, layout.user_tile_width, GRAPHICS_LOGIN_USER_TILE_H, fill);
        graphics_draw_soft_rect_outline(tile_x, layout.users_y, layout.user_tile_width,
                                        GRAPHICS_LOGIN_USER_TILE_H, border);
        graphics_draw_login_avatar((uint16_t) (tile_x + 8), (uint16_t) (layout.users_y + 9), 30,
                                   user->name, i == g_selected_login_user ? 0x003C6FEA : 0x00DCEBFF,
                                   i == g_selected_login_user ? 0x00FFFFFF : accent);
        graphics_draw_text_clipped((uint16_t) (tile_x + 46), (uint16_t) (layout.users_y + 17),
                                   layout.user_tile_width > 52 ? (uint16_t) (layout.user_tile_width - 52) : 1,
                                   user->name, text);
    }

    graphics_draw_text(layout.form_x, (uint16_t) (layout.account_y - 20), "Account", 0x005D6B7A);
    graphics_fill_soft_rect(layout.form_x, layout.account_y, layout.form_width, 34,
                            g_login_field == 0 ? 0x00FFFFFF : 0x00F2F6FB);
    graphics_draw_soft_rect_outline(layout.form_x, layout.account_y, layout.form_width, 34,
                                    g_login_field == 0 ? accent : 0x00D3DFEC);
    graphics_draw_login_avatar((uint16_t) (layout.form_x + 10), (uint16_t) (layout.account_y + 4),
                               26, g_login_username, 0x00DCEBFF, accent);
    graphics_draw_text_clipped((uint16_t) (layout.form_x + 46), (uint16_t) (layout.account_y + 9),
                               layout.form_width > 108 ? (uint16_t) (layout.form_width - 108) : 1,
                               g_login_username, 0x002E3A48);
    graphics_draw_text_aligned((uint16_t) (layout.form_x + layout.form_width - 62),
                               (uint16_t) (layout.account_y + 9), 52, "Change", accent);

    graphics_draw_text(layout.form_x, (uint16_t) (layout.password_y - 20), "Password", 0x005D6B7A);
    password_border = g_login_error ? UI_COLOR_WARN :
                      (g_login_field == 1 ? accent : 0x00BFCEDF);
    graphics_fill_soft_rect(layout.form_x, layout.password_y, layout.form_width, 36,
                            g_login_error ? 0x00FFF4F5 : 0x00FFFFFF);
    graphics_draw_soft_rect_outline(layout.form_x, layout.password_y, layout.form_width, 36, password_border);
    if (g_login_password[0] == '\0') {
        graphics_draw_text((uint16_t) (layout.form_x + 12), (uint16_t) (layout.password_y + 10),
                           "Enter your password", 0x008296A8);
    } else {
        uint32_t max_dots = layout.form_width > 28 ? (layout.form_width - 24) / 10 : 0;

        for (uint32_t i = 0; g_login_password[i] != '\0' && i < max_dots; i++) {
            graphics_fill_soft_rect((uint16_t) (layout.form_x + 12 + i * 10),
                                    (uint16_t) (layout.password_y + 15), 5, 5, 0x002E3A48);
        }
    }

    graphics_fill_soft_rect(layout.form_x, layout.button_y, layout.form_width, 36, UI_COLOR_ACCENT);
    graphics_draw_soft_rect_outline(layout.form_x, layout.button_y, layout.form_width, 36, 0x00B7D8FF);
    graphics_draw_text_aligned(layout.form_x, (uint16_t) (layout.button_y + 10),
                               layout.form_width, "Sign in", 0x00FFFFFF);
    if (session_auth_locked()) {
        graphics_draw_text_clipped(layout.form_x, layout.footer_y, layout.form_width,
                                   "Too many attempts. Try again shortly.", UI_COLOR_WARN);
    } else if (g_login_error) {
        graphics_draw_text_clipped(layout.form_x, layout.footer_y, layout.form_width,
                                   "We could not verify that password.", UI_COLOR_WARN);
    } else {
        graphics_draw_text_clipped(layout.form_x, layout.footer_y, layout.form_width,
                                   "Press Enter to sign in.", 0x005D6B7A);
    }
    graphics_draw_text_clipped(layout.form_x, (uint16_t) (layout.footer_y + 24), layout.form_width,
                               ui_network_label(), 0x00748AA1);

    graphics_fill_soft_rect(layout.power_x, layout.power_y, GRAPHICS_LOGIN_POWER_SIZE,
                            GRAPHICS_LOGIN_POWER_SIZE, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(layout.power_x, layout.power_y, GRAPHICS_LOGIN_POWER_SIZE,
                                    GRAPHICS_LOGIN_POWER_SIZE, 0x00D3DFEC);
    graphics_draw_text_aligned(layout.power_x, (uint16_t) (layout.power_y + 10),
                               GRAPHICS_LOGIN_POWER_SIZE, "P", 0x003C4A5A);
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
        graphics_draw_logon_window(window);
        return;
    }
    if (window->kind == UI_WINDOW_FILES) {
        uint32_t visible_rows = graphics_file_visible_rows(window);
        uint32_t first_item;
        uint32_t last_item;
        uint16_t list_top = (uint16_t) (window->y + UI_FILES_LIST_TOP_OFFSET);
        uint16_t list_height = window->height > UI_FILES_LIST_HEIGHT_OFFSET ? (uint16_t) (window->height - UI_FILES_LIST_HEIGHT_OFFSET) : 1;
        uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);
        uint16_t drive_bar_y = (uint16_t) (window->y + UI_FILES_DRIVE_BAR_Y);
        char drives[26];
        int32_t drive_count;
        char cur_drive;

        graphics_file_clamp_scroll(window);
        graphics_fill_rect((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x00DCE8F4);
        graphics_draw_rect_outline((uint16_t) (window->x + 8), (uint16_t) (window->y + 30), (uint16_t) (window->width - 16), 26, 0x008AA1B8);
        graphics_draw_text((uint16_t) (window->x + 18), (uint16_t) (window->y + 37), g_file_current_path, 0x0017232E);
        /* Toolbar search box (right end of path bar). */
        {
            uint16_t sbx = (uint16_t) (window->x + window->width - 132);
            uint16_t sby = (uint16_t) (window->y + 33);
            graphics_fill_rect(sbx, sby, 120, 20, 0x00FFFFFF);
            graphics_draw_rect_outline(sbx, sby, 120, 20, g_file_search_open ? 0x00256EC8 : 0x00999999);
            if (g_file_search_query[0] != '\0') {
                graphics_draw_text((uint16_t) (sbx + 6), (uint16_t) (sby + 4), g_file_search_query, 0x00111111);
            } else {
                graphics_draw_text((uint16_t) (sbx + 6), (uint16_t) (sby + 4), "Search...", 0x00999999);
            }
        }
        cur_drive = (g_file_current_path[0] >= 'a' && g_file_current_path[0] <= 'z') ?
                    (char) (g_file_current_path[0] - 32) : g_file_current_path[0];
        drive_count = file_get_mounted_drives(drives, (uint32_t) (sizeof(drives) - 1u));
        if (drive_count < 0) {
            drive_count = 0;
        }
        for (int32_t di = 0; di < drive_count; di++) {
            uint16_t btn_x = (uint16_t) (window->x + 12 + di * (UI_FILES_DRIVE_BTN_W + 4));
            bool active = drives[di] == cur_drive;
            uint32_t bg = active ? 0x003A8FD4 : 0x00E3EBF4;
            uint32_t fg = active ? 0x00FFFFFF : 0x0017232E;
            char lbl[4];
            lbl[0] = drives[di];
            lbl[1] = ':';
            lbl[2] = '\\';
            lbl[3] = '\0';
            graphics_fill_rect(btn_x, (uint16_t) (drive_bar_y + 1), UI_FILES_DRIVE_BTN_W, UI_FILES_DRIVE_BTN_H, bg);
            graphics_draw_rect_outline(btn_x, (uint16_t) (drive_bar_y + 1), UI_FILES_DRIVE_BTN_W, UI_FILES_DRIVE_BTN_H, 0x008AA1B8);
            graphics_draw_text_aligned(btn_x, (uint16_t) (drive_bar_y + 6), UI_FILES_DRIVE_BTN_W, lbl, fg);
        }
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
            uint32_t fill = g_file_selected[i] ? 0x00D8E9FA : 0x00F7FBFE;
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
        uint32_t hist_rows = console_history_rows_for_pid(window->owner_pid);
        uint32_t scheme_bg = console_scheme_bg();
        uint32_t scheme_fg = console_scheme_fg();
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
                           (uint16_t) (window->width - 24), list_height, scheme_bg);
        for (uint32_t row = 0; row < visible_rows; row++) {
            uint16_t draw_x = (uint16_t) (window->x + 16);
            uint32_t abs_row = scroll_offset + row;

            if (abs_row >= content_rows) {
                continue;
            }
            for (uint16_t col = 0; col < CONSOLE_COLUMNS; col++) {
                uint32_t codepoint;
                uint32_t advance;

                if (abs_row < hist_rows) {
                    codepoint = console_history_cell_for_pid(window->owner_pid, abs_row, col);
                } else {
                    codepoint = buffer[(abs_row - hist_rows) * CONSOLE_COLUMNS + col];
                }
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
                                        scheme_fg);
                draw_x = (uint16_t) (draw_x + advance);
            }
        }
        graphics_draw_scrollbar(scrollbar_x, list_top, list_height,
                                content_rows, visible_rows, scroll_offset, true);
        /* scroll position indicator when reviewing history */
        if (window->console_scroll_manual && max_scroll > 0 && scroll_offset < max_scroll) {
            char posbuf[24];
            uint32_t pct = (uint32_t)((uint64_t)scroll_offset * 100u / (uint64_t)max_scroll);
            posbuf[0] = '^'; posbuf[1] = '@'; posbuf[2] = ' ';
            posbuf[3] = (char)('0' + pct / 100u);
            posbuf[4] = (char)('0' + (pct / 10u) % 10u);
            posbuf[5] = (char)('0' + pct % 10u);
            posbuf[6] = '%'; posbuf[7] = ' ';
            posbuf[8] = 'N'; posbuf[9] = 'E'; posbuf[10] = 'W'; posbuf[11] = '\0';
            graphics_draw_text((uint16_t) (window->x + 16),
                               (uint16_t) (window->y + 36), posbuf, 0x00FFD580u);
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
        graphics_draw_text_aligned((uint16_t) (window->x + 200), (uint16_t) (window->y + 94), 136, "\u9009\u62e9WAV", 0x00FFFFFF);
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
        graphics_notepad_render(window);
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
        graphics_draw_text((uint16_t) (window->x + 16), (uint16_t) (window->y + 96),
                           g_uac_publisher_trusted ? "verified publisher" :
                           (g_uac_signed && g_uac_signature_valid ? "signed, publisher untrusted" :
                            (g_uac_signed ? "invalid signature" : "unsigned publisher")),
                           g_uac_publisher_trusted ? 0x002E8B57 : 0x00B43A3A);
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

static void graphics_draw_window(const ui_window_t *window, bool active)
{
    if (!window->visible || window->minimized) {
        return;
    }

    if (window->kind == UI_WINDOW_LOGON) {
        graphics_draw_logon_window(window);
        return;
    }

    {
        uint32_t win_bg = ui_color(UI_COLOR_WINDOW_BG, UI_COLOR_WINDOW_BG_DARK);
        uint32_t win_edge = ui_color(UI_COLOR_WINDOW_EDGE, 0x003A3A3Au);
        uint32_t title_text = ui_color(UI_COLOR_TITLE_TEXT, 0x00FFFFFFu);
        uint32_t ctrl_ink = ui_color(0x00444444u, 0x00FFFFFFu);
        uint32_t ctrl_bg = ui_color(0x00F5F5F5u, 0x003A3A3Au);
        uint32_t tb_top = active ? ui_color(0x00FFFFFFu, 0x002D2D2Du) : ui_color(0x00F3F3F3u, 0x00262626u);
        uint32_t tb_bot = active ? ui_color(0x00FAFAFAu, 0x002D2D2Du) : ui_color(0x00EFEFEFu, 0x00262626u);

        graphics_draw_shadow(window->x, window->y, window->width, window->height);
        graphics_fill_rounded_rect(window->x, window->y, window->width, window->height, win_bg, 8u);
        graphics_draw_rounded_rect_outline(window->x, window->y, window->width, window->height, win_edge, 8u);
        graphics_fill_rect_gradient((uint16_t) (window->x + 1), window->y, (uint16_t) (window->width - 2), UI_TITLEBAR_H,
                                    tb_top, tb_bot);
        graphics_fill_rect((uint16_t) (window->x + 1), (uint16_t) (window->y + UI_TITLEBAR_H), (uint16_t) (window->width - 2), 1,
                           active ? g_ux_accent : ui_color(0x00E0E0E0u, 0x003A3A3Au));
        graphics_fill_rounded_rect((uint16_t) (window->x + 10), (uint16_t) (window->y + 9), 10, 10,
                                   active ? g_ux_accent : ui_color(0x00888888u, 0x00888888u), 2u);
        graphics_draw_text_clipped((uint16_t) (window->x + 28),
                                   (uint16_t) (window->y + 7),
                                   window->width > 112 ? (uint16_t) (window->width - 112) : 1,
                                   window->title,
                                   active ? title_text : ui_color(0x007A7A7Au, 0x00AAAAAAu));
        /* WinUI 3 flat window controls (themed, close = red on hover) */
        {
            uint32_t this_win = (uint32_t) (window - g_windows);
            uint32_t min_bg = ctrl_bg;
            uint32_t max_bg = window->maximized ? ui_color(0x00E7F0FFu, 0x0033445Cu) : ctrl_bg;
            uint32_t close_bg = ctrl_bg;
            uint32_t close_ink = ctrl_ink;
            if (g_ux_hover.win_index == this_win && g_ux_hover.win_btn != 0u) {
                if (g_ux_hover.win_btn == 1u) {
                    min_bg = g_ux_hover.pressed ? graphics_darken_color(ctrl_bg, 5u)
                                                : graphics_brighten_color(ctrl_bg, 12u);
                } else if (g_ux_hover.win_btn == 2u) {
                    max_bg = g_ux_hover.pressed ? graphics_darken_color(max_bg, 5u)
                                                : graphics_brighten_color(max_bg, 12u);
                } else if (g_ux_hover.win_btn == 3u) {
                    close_bg = 0x00E81123u;
                    close_ink = 0x00FFFFFFu;
                }
            }
            graphics_fill_rounded_rect((uint16_t) (window->x + window->width - 66), window->y, 16, UI_TITLEBAR_H, min_bg, 0u);
            graphics_fill_rect((uint16_t) (window->x + window->width - 62), (uint16_t) (window->y + 13), 8, 1, ctrl_ink);
            graphics_fill_rounded_rect((uint16_t) (window->x + window->width - 46), window->y, 16, UI_TITLEBAR_H, max_bg, 0u);
            graphics_draw_rect_outline((uint16_t) (window->x + window->width - 42), (uint16_t) (window->y + 10), 8, 7, ctrl_ink);
            graphics_fill_rounded_rect((uint16_t) (window->x + window->width - 26), window->y, 18, UI_TITLEBAR_H, close_bg, 0u);
            graphics_draw_line((int16_t) (window->x + window->width - 21), (int16_t) (window->y + 10),
                               (int16_t) (window->x + window->width - 14), (int16_t) (window->y + 16), close_ink);
            graphics_draw_line((int16_t) (window->x + window->width - 14), (int16_t) (window->y + 10),
                               (int16_t) (window->x + window->width - 21), (int16_t) (window->y + 16), close_ink);
        }
        graphics_draw_window_content(window);
    }

    /* Window open fade-in: blend the freshly-drawn window towards white for the
     * first ~250ms after it is created, then leave it fully opaque. */
    if (g_ux_anim) {
        uint32_t ux_idx = (uint32_t) (window - g_windows);
        uint64_t ux_open = g_ux_win_open_tick[ux_idx];
        if (ux_open != 0ULL) {
            uint64_t ux_hz = timer_hz();
            uint64_t ux_age = timer_ticks() - ux_open;
            uint64_t ux_dur = (ux_hz == 0U ? 100U : ux_hz / 4U);
            if (ux_age < ux_dur) {
                uint32_t k = (uint32_t) (255ULL * (ux_dur - ux_age) / ux_dur);
                uint16_t y0 = window->y;
                uint16_t y1 = (uint16_t) (y0 + window->height);
                uint16_t x0 = window->x;
                uint16_t x1 = (uint16_t) (x0 + window->width);
                for (uint32_t yy = y0; yy < y1; yy++) {
                    for (uint32_t xx = x0; xx < x1; xx++) {
                        uint32_t bi = yy * (uint32_t) FB_WIDTH + xx;
                        uint32_t c = g_backbuffer[bi];
                        uint32_t r = (c >> 16) & 0xFFU;
                        uint32_t g = (c >> 8) & 0xFFU;
                        uint32_t b = c & 0xFFU;
                        r = (r * (255U - k) + 255U * k) / 255U;
                        g = (g * (255U - k) + 255U * k) / 255U;
                        b = (b * (255U - k) + 255U * k) / 255U;
                        g_backbuffer[bi] = (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
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

/* ---- Drag ghost: icon + filename follows the cursor ---- */
static void graphics_draw_drag_ghost(void)
{
    int16_t gx;
    int16_t gy;

    if (!g_dragging_file) {
        return;
    }
    gx = (int16_t) g_drag_file_x - 16;
    gy = (int16_t) g_drag_file_y - 28;
    if (gx < 0) gx = 0;
    if (gy < 0) gy = 0;
    graphics_fill_rect((uint16_t) gx, (uint16_t) gy, 86, 46, 0x00E8EEF6);
    graphics_draw_rect_outline((uint16_t) gx, (uint16_t) gy, 86, 46, 0x007AA2D0);
    graphics_draw_file_icon((uint16_t) gx + 4, (uint16_t) gy + 4, g_drag_file_path,
                            g_drag_file_is_dir, true);
    graphics_draw_text_clipped((uint16_t) gx + 4, (uint16_t) gy + 30, 78,
                               g_drag_file_name, 0x00223344);
}

/* ---- Rubber-band (marquee) selection rectangle ---- */
static void graphics_draw_rubber_band(void)
{
    uint16_t x0, y0, x1, y1;

    if (!g_rubber_active) {
        return;
    }
    x0 = g_rubber_start_x < g_rubber_cur_x ? g_rubber_start_x : g_rubber_cur_x;
    y0 = g_rubber_start_y < g_rubber_cur_y ? g_rubber_start_y : g_rubber_cur_y;
    x1 = g_rubber_start_x < g_rubber_cur_x ? g_rubber_cur_x : g_rubber_start_x;
    y1 = g_rubber_start_y < g_rubber_cur_y ? g_rubber_cur_y : g_rubber_start_y;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    graphics_fill_rect(x0, y0, (uint16_t) (x1 - x0), (uint16_t) (y1 - y0), 0x00B8D4F0);
    for (uint16_t cx = x0; cx < x1; cx += 4U) {
        graphics_plot(cx, y0, 0x002A6FD4);
        graphics_plot(cx, (uint16_t) (y1 - 1U), 0x002A6FD4);
    }
    for (uint16_t cy = y0; cy < y1; cy += 4U) {
        graphics_plot(x0, cy, 0x002A6FD4);
        graphics_plot((uint16_t) (x1 - 1U), cy, 0x002A6FD4);
    }
}

/* ---- Pinyin IME candidate window ---- */
static void graphics_draw_ime_candidates(void)
{
    uint16_t cx;
    uint16_t cy;
    char line[160];
    uint32_t off = 0U;

    if (g_ime_pinyin[0] == '\0') {
        return;
    }
    cx = g_ime_caret_x;
    cy = (uint16_t) (g_ime_caret_y + 24);
    if ((uint32_t) cx + 200U >= (uint32_t) FB_WIDTH) {
        cx = (uint16_t) (FB_WIDTH - 204U);
    }
    if ((uint32_t) cy + 44U >= (uint32_t) FB_HEIGHT) {
        cy = (uint16_t) (g_ime_caret_y - 48U);
    }
    graphics_draw_shadow(cx, cy, 200U, 44U);
    graphics_fill_soft_rect(cx, cy, 200U, 44U, 0x00FFFFFF);
    graphics_draw_soft_rect_outline(cx, cy, 200U, 44U, 0x00888888);
    graphics_draw_text_clipped((uint16_t) (cx + 6), (uint16_t) (cy + 4), 188U,
                               g_ime_pinyin, 0x00666666);
    line[0] = '\0';
    for (uint32_t i = 0U; i < g_ime_cand_count; i++) {
        const char *c = g_ime_cands[i];

        line[off++] = (char) ('1' + (int) i);
        line[off++] = ':';
        while (*c != '\0' && off + 3U < sizeof(line)) {
            line[off++] = *c++;
        }
        line[off++] = ' ';
        line[off++] = ' ';
    }
    line[off] = '\0';
    graphics_draw_text_clipped((uint16_t) (cx + 6), (uint16_t) (cy + 22), 188U,
                               line, 0x00111111);
}

/* ---- "New file/folder" inline name dialog ---- */
static void graphics_draw_newitem_dialog(void)
{
    uint16_t w = 320U;
    uint16_t h = 110U;
    uint16_t x = (uint16_t) ((FB_WIDTH - w) / 2U);
    uint16_t y = (uint16_t) ((FB_HEIGHT - h) / 2U);
    const char *title = g_newitem_is_dir ? "New Folder" : "New Text Document";

    if (!g_newitem_open) {
        return;
    }
    graphics_draw_shadow(x, y, w, h);
    graphics_fill_soft_rect(x, y, w, h, 0x00F4F6F9);
    graphics_draw_soft_rect_outline(x, y, w, h, 0x005A8AC0);
    graphics_draw_text_clipped((uint16_t) (x + 14), (uint16_t) (y + 12), (uint16_t) (w - 28U),
                               title, 0x00223344);
    graphics_fill_rect((uint16_t) (x + 14), (uint16_t) (y + 40), (uint16_t) (w - 28U), 26U, 0x00FFFFFF);
    graphics_draw_rect_outline((uint16_t) (x + 14), (uint16_t) (y + 40), (uint16_t) (w - 28U), 26U, 0x00999999);
    graphics_draw_text_clipped((uint16_t) (x + 20), (uint16_t) (y + 46), (uint16_t) (w - 40U),
                               g_newitem_name, 0x00111111);
    graphics_draw_text_clipped((uint16_t) (x + 14), (uint16_t) (y + 76), (uint16_t) (w - 28U),
                               "Enter=create  Esc=cancel", 0x00666666);
}

void graphics_draw_shell(void)
{
    if (!g_graphics_active) {
        return;
    }

    if (!g_session_logged_in && !g_installer_mode && graphics_find_window(UI_WINDOW_LOGON) < 0) {
        graphics_open_window(UI_WINDOW_LOGON);
    }
    if (g_secure_desktop && g_uac_pending) {
        graphics_fill(0x000B1220);
        graphics_fill_rect(0, 0, FB_WIDTH, 80, 0x00152136);
        graphics_draw_text(28, 28, "MoniOS Secure Desktop", 0x00DCEAFF);
        graphics_draw_text(28, 52, "The system is waiting for elevation approval", 0x009AB7D2);
        for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
            if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_UAC) {
                graphics_draw_window(&g_windows[i], true);
            }
        }
        graphics_present();
        g_cursor_drawn = false;
        graphics_mouse_redraw(g_cursor_x, g_cursor_y);
        return;
    }
    graphics_draw_desktop();
    graphics_load_desktop_entries();
    graphics_draw_desktop_icons();
    graphics_draw_taskbar();
    for (uint32_t i = 0; i < UI_WINDOW_MAX; i++) {
        graphics_draw_window(&g_windows[i], i == UI_WINDOW_MAX - 1);
    }
    graphics_draw_start_menu();
    graphics_draw_bt_panel();
    graphics_draw_context_menu();
    graphics_draw_power_menu();
    graphics_draw_power_overlay();
    graphics_draw_properties_overlay();
    ux_draw_notifications();
    ux_draw_clipboard_history();
    graphics_draw_rubber_band();
    graphics_draw_drag_ghost();
    graphics_draw_newitem_dialog();
    graphics_draw_ime_candidates();
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
    if (g_session_logged_in && ux_handle_tray_click(x, y)) {
        return;
    }
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

    if (g_bt_panel_open) {
        uint16_t bpx = (uint16_t)(FB_WIDTH - 212);
        uint16_t brows = bt_device_count();
        if (brows > 6U) brows = 6U;
        uint16_t bh = (uint16_t)(34U + brows * 26U + 26U);
        uint16_t bpy = (uint16_t)(FB_HEIGHT - TASKBAR_HEIGHT - bh - 8U);
        if (!graphics_point_in_rect(x, y, bpx, bpy, 220U, bh)) {
            g_bt_panel_open = false;
            graphics_draw_shell();
            return;
        }
    }

    if (g_rainbow_cat_open) {
        graphics_draw_shell();
        return;
    }

    if (g_properties_open) {
        uint16_t px;
        uint16_t py;
        uint16_t pw;
        uint16_t ph;
        uint16_t btn_w = 80U;
        uint16_t btn_h = 24U;
        uint16_t btn_x;
        uint16_t btn_y;
        graphics_properties_geometry(&px, &py, &pw, &ph);
        btn_x = (uint16_t) (px + (pw - btn_w) / 2U);
        btn_y = (uint16_t) (py + ph - 40U);
        if (graphics_point_in_rect(x, y, btn_x, btn_y, btn_w, btn_h)) {
            graphics_properties_commit();
        } else if (graphics_point_in_rect(x, y, (uint16_t) (px + 80), (uint16_t) (py + 12),
                                          (uint16_t) (pw - 96), 22U)) {
            g_properties_editing = true;
        } else if (graphics_point_in_rect(x, y, (uint16_t) (px + 14), (uint16_t) (py + 204),
                                          110U, 18U)) {
            g_properties_readonly = !g_properties_readonly;
        } else if (graphics_point_in_rect(x, y, (uint16_t) (px + 118), (uint16_t) (py + 204),
                                          110U, 18U)) {
            g_properties_hidden = !g_properties_hidden;
        }
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
        graphics_login_layout_t layout;
        uint32_t user_count = session_user_count();

        graphics_login_layout(&layout);
        if (graphics_point_in_rect(x, y, layout.power_x, layout.power_y,
                                   GRAPHICS_LOGIN_POWER_SIZE, GRAPHICS_LOGIN_POWER_SIZE)) {
            g_power_menu_x = FB_WIDTH > 182 ? (uint16_t) (FB_WIDTH - 182) : 8;
            g_power_menu_y = graphics_power_menu_top();
            g_power_menu_open = true;
            graphics_draw_shell();
            return;
        }
        if (user_count > SESSION_USER_MAX) {
            user_count = SESSION_USER_MAX;
        }
        for (uint32_t i = 0; i < user_count; i++) {
            uint16_t tile_x = (uint16_t) (layout.form_x + i * (layout.user_tile_width + 8));

            if (graphics_point_in_rect(x, y, tile_x, layout.users_y,
                                       layout.user_tile_width, GRAPHICS_LOGIN_USER_TILE_H)) {
                graphics_select_login_user(i);
                graphics_draw_shell();
                return;
            }
        }
        if (graphics_point_in_rect(x, y, layout.form_x, layout.account_y, layout.form_width, 34)) {
            g_login_field = 0;
            g_login_error = false;
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, layout.form_x, layout.password_y, layout.form_width, 36)) {
            g_login_field = 1;
            g_login_error = false;
            graphics_draw_shell();
            return;
        }
        if (graphics_point_in_rect(x, y, layout.form_x, layout.button_y, layout.form_width, 36)) {
            (void) graphics_attempt_login();
            graphics_draw_shell();
            return;
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
                graphics_open_item(path, g_desktop_entries[desktop_index].is_dir);
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
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 128), 140, 22)) {
                g_context_menu_open = false;
                for (uint32_t i = 0; i < g_file_item_count && i < GRAPHICS_FILE_ITEM_MAX; i++) {
                    if (g_file_selected[i]) {
                        char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                        graphics_build_file_entry_path(i, path);
                        if (!file_send_to_recycle_bin(path)) {
                            file_delete(path);
                        }
                    }
                }
                g_file_selected_index = 0;
                graphics_fill_file_browser();
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 152), 140, 22)) {
                g_context_menu_open = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 176), 140, 22)) {
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                g_context_menu_open = false;
                graphics_build_file_entry_path(g_file_selected_index, path);
                strlcpy(g_properties_path, path, sizeof(g_properties_path));
                strlcpy(g_properties_name, g_file_items[g_file_selected_index].name, sizeof(g_properties_name));
                g_properties_is_dir = g_file_items[g_file_selected_index].is_dir;
                g_properties_open = true;
                graphics_properties_refresh_state();
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 206), 140, 22)) {
                g_ctx_submenu = UI_CTX_SUB_NEW;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 230), 140, 22)) {
                g_ctx_submenu = UI_CTX_SUB_OPENWITH;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 254), 140, 22)) {
                g_ctx_submenu = UI_CTX_SUB_SENDTO;
                graphics_draw_shell();
                return;
            }
            if (g_ctx_submenu != UI_CTX_SUB_NONE) {
                uint16_t sx = (uint16_t) (g_context_menu_x + CONTEXT_MENU_W - 4);
                uint16_t sy = (uint16_t) (g_context_menu_y + 206);
                ui_ctx_submenu_t sub = g_ctx_submenu;
                char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_build_file_entry_path(g_file_selected_index, path);
                if (graphics_point_in_rect(x, y, sx, sy, 130, 22)) {
                    g_context_menu_open = false;
                    g_ctx_submenu = UI_CTX_SUB_NONE;
                    if (sub == UI_CTX_SUB_NEW) {
                        g_newitem_open = true; g_newitem_is_dir = false;
                        g_newitem_name[0] = '\0';
                        strlcpy(g_newitem_parent, g_file_current_path, sizeof(g_newitem_parent));
                    } else if (sub == UI_CTX_SUB_OPENWITH) {
                        graphics_open_path(path);
                    } else {
                        graphics_create_desktop_shortcut(path);
                        graphics_refresh_desktop_entries();
                    }
                    graphics_draw_shell();
                    return;
                }
                if (graphics_point_in_rect(x, y, sx, (uint16_t) (sy + 24), 130, 22)) {
                    g_context_menu_open = false;
                    g_ctx_submenu = UI_CTX_SUB_NONE;
                    if (sub == UI_CTX_SUB_NEW) {
                        g_newitem_open = true; g_newitem_is_dir = true;
                        g_newitem_name[0] = '\0';
                        strlcpy(g_newitem_parent, g_file_current_path, sizeof(g_newitem_parent));
                    } else if (sub == UI_CTX_SUB_OPENWITH) {
                        graphics_open_path(path);
                    } else {
                        graphics_create_desktop_shortcut(path);
                        graphics_refresh_desktop_entries();
                    }
                    graphics_draw_shell();
                    return;
                }
            }
            g_context_menu_open = false;
            g_ctx_submenu = UI_CTX_SUB_NONE;
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
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 110), 140, 22)) {
                g_context_menu_open = false;
                g_newitem_open = true; g_newitem_is_dir = true;
                g_newitem_name[0] = '\0';
                strlcpy(g_newitem_parent, UI_ROOT_DESKTOP, sizeof(g_newitem_parent));
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 134), 140, 22)) {
                g_context_menu_open = false;
                g_newitem_open = true; g_newitem_is_dir = false;
                g_newitem_name[0] = '\0';
                strlcpy(g_newitem_parent, UI_ROOT_DESKTOP, sizeof(g_newitem_parent));
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 158), 140, 22)) {
                g_context_menu_open = false;
                graphics_open_window(UI_WINDOW_CONTROL_PANEL);
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (g_context_menu_x + 8), (uint16_t) (g_context_menu_y + 182), 140, 22)) {
                g_context_menu_open = false;
                graphics_open_window(UI_WINDOW_CONTROL_PANEL);
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
            uint64_t now_ticks = timer_ticks();
            /* double-click on title bar toggles maximize / restore */
            if (g_ux_titlebar_click_index == (uint32_t) (UI_WINDOW_MAX - 1) &&
                now_ticks - g_ux_titlebar_click_tick < (uint64_t) timer_hz() / 2U) {
                g_ux_titlebar_click_tick = 0ULL;
                g_ux_titlebar_click_index = 0xFFFFFFFFU;
                graphics_toggle_maximize_window(UI_WINDOW_MAX - 1);
                graphics_draw_shell();
                return;
            }
            g_ux_titlebar_click_tick = now_ticks;
            g_ux_titlebar_click_index = (uint32_t) (UI_WINDOW_MAX - 1);
            g_dragging_window = true;
            g_drag_window_index = UI_WINDOW_MAX - 1;
            g_drag_offset_x = (int32_t) x - (int32_t) g_windows[UI_WINDOW_MAX - 1].x;
            g_drag_offset_y = (int32_t) y - (int32_t) g_windows[UI_WINDOW_MAX - 1].y;
            return;
        }
        if (window->kind == UI_WINDOW_FILES) {
            uint32_t visible_rows = graphics_file_visible_rows(window);
            uint16_t list_top = (uint16_t) (window->y + UI_FILES_LIST_TOP_OFFSET);
            uint16_t list_height = window->height > UI_FILES_LIST_HEIGHT_OFFSET ? (uint16_t) (window->height - UI_FILES_LIST_HEIGHT_OFFSET) : 1;
            uint16_t scrollbar_x = (uint16_t) (window->x + window->width - GRAPHICS_SCROLLBAR_W - 10);
            uint16_t drive_bar_y = (uint16_t) (window->y + UI_FILES_DRIVE_BAR_Y);
            const keyboard_status_t *kb = keyboard_status();

            graphics_set_terminal_focus(false);
            /* Search box hit area (right end of path bar). */
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + window->width - 132),
                                      (uint16_t) (window->y + 33), 120, 20)) {
                g_file_search_open = true;
                g_notepad_focus = false;
                g_terminal_input_focus = false;
                g_run_input_focus = false;
                graphics_draw_shell();
                return;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 12), list_top, 18, 18)) {
                graphics_file_browser_go_up();
                graphics_draw_shell();
                return;
            }
            {
                char drives[26];
                int32_t drive_count = file_get_mounted_drives(drives, (uint32_t) (sizeof(drives) - 1u));
                if (drive_count < 0) {
                    drive_count = 0;
                }
                for (int32_t di = 0; di < drive_count; di++) {
                    uint16_t btn_x = (uint16_t) (window->x + 12 + di * (UI_FILES_DRIVE_BTN_W + 4));
                    if (graphics_point_in_rect(x, y, btn_x, (uint16_t) (drive_bar_y + 1), UI_FILES_DRIVE_BTN_W, UI_FILES_DRIVE_BTN_H)) {
                        g_file_current_path[0] = drives[di];
                        g_file_current_path[1] = ':';
                        g_file_current_path[2] = '\\';
                        g_file_current_path[3] = '\0';
                        g_file_selected_index = 0;
                        g_file_anchor_index = 0;
                        g_file_scroll_offset = 0;
                        graphics_file_clear_selection();
                        graphics_fill_file_browser();
                        graphics_draw_shell();
                        return;
                    }
                }
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
                    g_drag_pending = true;
                    g_drag_start_x = x;
                    g_drag_start_y = y;
                    g_drag_file_index = entry_index;
                    if (kb->shift_down) {
                        uint32_t a = g_file_anchor_index;
                        uint32_t b = entry_index;
                        graphics_file_clear_selection();
                        if (a > b) {
                            uint32_t t = a;
                            a = b;
                            b = t;
                        }
                        for (uint32_t k = a; k <= b && k < g_file_item_count; k++) {
                            g_file_selected[k] = true;
                        }
                        g_file_selected_index = entry_index;
                    } else if (kb->ctrl_down) {
                        g_file_selected[entry_index] = !g_file_selected[entry_index];
                        g_file_selected_index = entry_index;
                        g_file_anchor_index = entry_index;
                    } else {
                        graphics_file_clear_selection();
                        g_file_selected[entry_index] = true;
                        g_file_selected_index = entry_index;
                        g_file_anchor_index = entry_index;
                    }
                    graphics_file_ensure_selected_visible(window);
                    if (same_item && !kb->ctrl_down && !kb->shift_down) {
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
            /* Click on blank list area: start rubber-band selection. */
            if (graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), (uint16_t) list_top,
                                      (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W),
                                      (uint16_t) list_height)) {
                g_rubber_active = true;
                g_rubber_start_x = x;
                g_rubber_start_y = y;
                g_rubber_cur_x = x;
                g_rubber_cur_y = y;
                if (!kb->ctrl_down) {
                    graphics_file_clear_selection();
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
        if (window->kind == UI_WINDOW_NOTEPAD) {
            graphics_notepad_click_tab(window, x, y, (uint32_t) i);
            g_notepad_focus = true;
            graphics_set_terminal_focus(false);
            g_run_input_focus = false;
            graphics_bring_window_to_front((uint32_t) i);
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
    bool allow_context = false;

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
            uint16_t list_top = (uint16_t) (window->y + UI_FILES_LIST_TOP_OFFSET);

            for (uint32_t row_index = 0; row_index < visible_rows; row_index++) {
                uint32_t entry_index = g_file_scroll_offset + row_index;
                uint16_t row_y = (uint16_t) (list_top + row_index * 22);
                if (entry_index < g_file_item_count &&
                    graphics_point_in_rect(x, y, (uint16_t) (window->x + 36), row_y,
                                           (uint16_t) (window->width - 48 - GRAPHICS_SCROLLBAR_W), 20)) {
                    g_file_selected_index = entry_index;
                    g_file_anchor_index = entry_index;
                    if (!g_file_selected[entry_index]) {
                        graphics_file_clear_selection();
                        g_file_selected[entry_index] = true;
                    }
                    g_context_menu_mode = UI_CONTEXT_MENU_FILES;
                    {
                        char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                        graphics_build_file_entry_path(entry_index, path);
                        allow_context = graphics_context_menu_enabled(path);
                    }
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
                {
                    char path[GRAPHICS_CLIPBOARD_PATH_MAX];
                    graphics_build_desktop_entry_path(desktop_index, path);
                    allow_context = graphics_context_menu_enabled(path);
                }
                break;
            }
        }
    }
    if (!allow_context) {
        g_context_menu_open = false;
        graphics_draw_shell();
        return;
    }
    graphics_context_open(x, y, g_context_menu_mode);
    graphics_draw_shell();
}

/* WinUI 3 hover/press hit-testing: recomputes g_ux_hover for (x,y).
 * Returns true when the hovered element (or press state) changed, so the
 * caller can issue a single full redraw. */
static bool graphics_update_hover(uint16_t x, uint16_t y, uint8_t buttons)
{
    g_ux_hover_t old;
    uint16_t start_x;
    uint16_t menu_y;
    uint16_t root_top;
    bool pressed;
    uint32_t i;

    old = g_ux_hover;
    memset(&g_ux_hover, 0, sizeof(g_ux_hover));
    g_ux_hover.pinned = -1;
    g_ux_hover.running = -1;
    g_ux_hover.root = -1;
    g_ux_hover.content = -1;
    g_ux_hover.win_btn = 0u;
    g_ux_hover.win_index = 0xFFFFFFFFu;
    pressed = (buttons & MOUSE_BUTTON_LEFT) != 0;

    start_x = graphics_taskbar_pinned_start_x();

    /* ---- taskbar pinned + running buttons ---- */
    if (y >= BUTTON_Y && y < BUTTON_Y + BUTTON_HEIGHT) {
        for (i = 0; i < (uint32_t)(sizeof(g_taskbar_buttons) / sizeof(g_taskbar_buttons[0])); i++) {
            uint16_t bx = (uint16_t) (start_x + START_BUTTON_WIDTH + 10u + i * (BUTTON_WIDTH + BUTTON_GAP));
            if (x >= bx && x < (uint16_t) (bx + BUTTON_WIDTH)) {
                g_ux_hover.pinned = (int8_t) i;
                break;
            }
        }
        if (g_ux_hover.pinned < 0) {
            uint32_t wi = 0xFFFFFFFFu;
            if (graphics_taskbar_window_at(x, y, &wi) && wi < UI_WINDOW_MAX) {
                g_ux_hover.running = (int32_t) wi;
            }
        }
    }

    /* ---- start menu ---- */
    if (g_start_menu_open) {
        menu_y = (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT - START_MENU_H - 12);
        root_top = (uint16_t) (menu_y + START_MENU_HEADER_H + 8);
        if (graphics_point_in_rect(x, y, (uint16_t) (START_MENU_X + START_MENU_W - 38),
                                   (uint16_t) (menu_y + 6), 26u, 24u)) {
            g_ux_hover.theme = 1u;
        }
        for (i = 0; i < 4u; i++) {
            uint16_t ry = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
            if (graphics_point_in_rect(x, y, (uint16_t) (START_MENU_X + 8), ry,
                                       START_MENU_LEFT_W - 16u, UI_MENU_ITEM_H - 2u)) {
                g_ux_hover.root = (int8_t) i;
            }
        }
        {
            uint16_t mid_xx = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + 12);
            uint16_t right_xx = (uint16_t) (START_MENU_X + START_MENU_LEFT_W + START_MENU_MID_W + 16);
            uint32_t maxrow = 0u;
            bool rightcol = false;
            if (g_start_menu_view == UI_START_MENU_DISPLAY) {
                maxrow = (uint32_t)(sizeof(g_graphics_modes) / sizeof(g_graphics_modes[0]));
                rightcol = true;
            } else if (g_start_menu_view == UI_START_MENU_APPS) {
                maxrow = 5u;
            } else if (g_start_menu_view == UI_START_MENU_SETTINGS) {
                maxrow = 3u;
            } else if (g_start_menu_view == UI_START_MENU_NETWORK) {
                maxrow = 2u;
            } else if (g_start_menu_view == UI_START_MENU_POWER) {
                maxrow = 3u;
            }
            for (i = 0; i < maxrow; i++) {
                uint16_t ry = (uint16_t) (root_top + i * UI_MENU_ITEM_H);
                uint16_t rx = rightcol ? right_xx : mid_xx;
                uint16_t rw = rightcol ? (START_MENU_RIGHT_W - 24u) : (START_MENU_MID_W - 24u);
                if (graphics_point_in_rect(x, y, rx, ry, rw, UI_MENU_ITEM_H - 2u)) {
                    g_ux_hover.content = (int8_t) i;
                    g_ux_hover.content_right = rightcol ? 1u : 0u;
                }
            }
        }
    }

    /* ---- window titlebar controls (topmost window under cursor) ---- */
    if (g_ux_hover.pinned < 0 && g_ux_hover.running < 0 && g_ux_hover.root < 0 &&
        g_ux_hover.content < 0 && !g_ux_hover.theme) {
        int32_t wi;
        for (wi = UI_WINDOW_MAX - 1; wi >= 0; wi--) {
            ui_window_t *win = &g_windows[wi];
            if (!win->visible || win->minimized) {
                continue;
            }
            if (!graphics_point_in_rect(x, y, win->x, win->y, win->width, win->height)) {
                continue;
            }
            if (graphics_point_in_rect(x, y, (uint16_t) (win->x + win->width - 62),
                                      (uint16_t) (win->y + 4), 14u, 16u)) {
                g_ux_hover.win_btn = 1u;
            } else if (graphics_point_in_rect(x, y, (uint16_t) (win->x + win->width - 44),
                                               (uint16_t) (win->y + 4), 14u, 16u)) {
                g_ux_hover.win_btn = 2u;
            } else if (graphics_point_in_rect(x, y, (uint16_t) (win->x + win->width - 26),
                                               (uint16_t) (win->y + 4), 18u, 16u)) {
                g_ux_hover.win_btn = 3u;
            }
            if (g_ux_hover.win_btn != 0u) {
                g_ux_hover.win_index = (uint32_t) wi;
            }
            break;
        }
    }

    /* pressed only counts when the cursor is actually over a hit element */
    if (pressed && g_ux_hover.pinned < 0 && g_ux_hover.running < 0 &&
        g_ux_hover.root < 0 && g_ux_hover.content < 0 && !g_ux_hover.theme &&
        g_ux_hover.win_btn == 0u) {
        g_ux_hover.pressed = 0u;
    } else {
        g_ux_hover.pressed = (uint8_t) (pressed ? 1u : 0u);
    }

    return memcmp(&old, &g_ux_hover, sizeof(old)) != 0;
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

        /* edge snap hint based on cursor position */
        if (y <= 4U) {
            g_ux_snap_hint = 1U;
        } else if (x <= 4U) {
            g_ux_snap_hint = 2U;
        } else if (x >= (uint16_t) (FB_WIDTH - 4U)) {
            g_ux_snap_hint = 3U;
        } else {
            g_ux_snap_hint = 0U;
        }

        if (!window->maximized) {
            if (new_x < 8) new_x = 8;
            if (new_y < 8) new_y = 8;
            if (new_x > (int32_t) FB_WIDTH - (int32_t) window->width - 8) new_x = (int32_t) FB_WIDTH - (int32_t) window->width - 8;
            if (new_y > (int32_t) FB_HEIGHT - TASKBAR_HEIGHT - (int32_t) window->height - 8) new_y = (int32_t) FB_HEIGHT - TASKBAR_HEIGHT - (int32_t) window->height - 8;
            window->x = (uint16_t) new_x;
            window->y = (uint16_t) new_y;
        }
        graphics_draw_shell();
    }

    /* ---- File drag: pending -> active after a small movement threshold ---- */
    if (g_drag_pending && (buttons & MOUSE_BUTTON_LEFT) != 0) {
        int32_t dx = (int32_t) x - (int32_t) g_drag_start_x;
        int32_t dy = (int32_t) y - (int32_t) g_drag_start_y;

        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if ((dx >= 6) || (dy >= 6)) {
            g_dragging_file = true;
            g_drag_pending = false;
            graphics_build_file_entry_path(g_drag_file_index, g_drag_file_path);
            strlcpy(g_drag_file_name, g_file_items[g_drag_file_index].name,
                    sizeof(g_drag_file_name));
            g_drag_file_is_dir = g_file_items[g_drag_file_index].is_dir;
        }
    }
    if (g_dragging_file && (buttons & MOUSE_BUTTON_LEFT) != 0) {
        g_drag_file_x = x;
        g_drag_file_y = y;
        graphics_draw_shell();
    }
    if (g_rubber_active && (buttons & MOUSE_BUTTON_LEFT) != 0) {
        g_rubber_cur_x = x;
        g_rubber_cur_y = y;
        graphics_draw_shell();
    }

    if ((buttons & MOUSE_BUTTON_LEFT) == 0) {
        /* ---- Commit file drop on release ---- */
        if (g_dragging_file) {
            graphics_drop_commit(x, y);
            g_dragging_file = false;
        }
        /* ---- Finalize rubber-band selection on release ---- */
        if (g_rubber_active) {
            uint16_t rx0 = g_rubber_start_x < g_rubber_cur_x ? g_rubber_start_x : g_rubber_cur_x;
            uint16_t ry0 = g_rubber_start_y < g_rubber_cur_y ? g_rubber_start_y : g_rubber_cur_y;
            uint16_t rx1 = g_rubber_start_x < g_rubber_cur_x ? g_rubber_cur_x : g_rubber_start_x;
            uint16_t ry1 = g_rubber_start_y < g_rubber_cur_y ? g_rubber_cur_y : g_rubber_start_y;
            int32_t wi = -1;

            for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
                if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_FILES) {
                    wi = i;
                    break;
                }
            }
            if (wi >= 0) {
                ui_window_t *fw = &g_windows[wi];
                uint32_t list_top = (uint32_t) fw->y + 62U;
                uint32_t visible_rows = graphics_file_visible_rows(fw);
                for (uint32_t row = 0; row < visible_rows; row++) {
                    uint32_t entry = g_file_scroll_offset + row;
                    uint16_t row_y = (uint16_t) (list_top + row * 22U);

                    if (entry < g_file_item_count &&
                        rx1 > (uint16_t) (fw->x + 36) && rx0 < (uint16_t) (fw->x + fw->width - 30) &&
                        ry1 > row_y && ry0 < (uint16_t) (row_y + 20U)) {
                        g_file_selected[entry] = true;
                        g_file_selected_index = entry;
                    }
                }
            }
            g_rubber_active = false;
            graphics_draw_shell();
        }
        g_drag_pending = false;
        /* commit edge-snap on mouse release */
        if (g_dragging_window && g_drag_window_index < UI_WINDOW_MAX && g_ux_snap_hint != 0U) {
            ui_window_t *window = &g_windows[g_drag_window_index];
            uint16_t half_w = (uint16_t) (FB_WIDTH / 2U);
            if (g_ux_snap_hint == 1U) {
                graphics_toggle_maximize_window(g_drag_window_index);
            } else if (g_ux_snap_hint == 2U) {
                window->restore_x = window->x; window->restore_y = window->y;
                window->restore_width = window->width; window->restore_height = window->height;
                window->x = 0; window->y = 0; window->width = half_w;
                window->height = FB_HEIGHT > TASKBAR_HEIGHT ? (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT) : FB_HEIGHT;
                window->maximized = false;
            } else if (g_ux_snap_hint == 3U) {
                window->restore_x = window->x; window->restore_y = window->y;
                window->restore_width = window->width; window->restore_height = window->height;
                window->x = half_w; window->y = 0; window->width = (uint16_t) (FB_WIDTH - half_w);
                window->height = FB_HEIGHT > TASKBAR_HEIGHT ? (uint16_t) (FB_HEIGHT - TASKBAR_HEIGHT) : FB_HEIGHT;
                window->maximized = false;
            }
            graphics_draw_shell();
        }
        g_ux_snap_hint = 0U;
        g_dragging_window = false;
        g_player_button_pressed = false;
    }
    g_prev_mouse_buttons = buttons;

    /* WinUI 3 hover/press feedback: only redraw when the hovered element
     * actually changes. Skip while actively dragging windows/files. */
    if (!g_dragging_window && !g_dragging_file && !g_rubber_active &&
        graphics_update_hover(x, y, buttons)) {
        graphics_draw_shell();
    }
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

/* ============================================================
 *  Pinyin input method (simplified, GB2312-level-1 common chars).
 *  Candidates are packed as UTF-8 strings; the table is compact
 *  (one pinyin syllable -> a string of common hanzi, best-first).
 * ============================================================ */
typedef struct {
    const char *py;
    const char *chars;
} py_entry_t;

typedef struct {
    const char *py;      /* full pinyin OR common abbreviation */
    const char *words;   /* phrase / word (UTF-8) */
} py_phrase_t;

static const py_entry_t g_py_table[] = {
    { "a", "啊阿呵" },
    { "ai", "爱埃哀矮挨艾哎" },
    { "an", "安按暗岸案俺" },
    { "ang", "昂盎" },
    { "ao", "奥熬傲袄澳" },
    { "ba", "八把爸吧罢巴拔" },
    { "bai", "百白拜摆败" },
    { "ban", "半班版办搬板" },
    { "bang", "帮棒榜绑" },
    { "bao", "包保报宝抱爆" },
    { "bei", "北背杯备被倍悲" },
    { "ben", "本奔" },
    { "beng", "蹦绷" },
    { "bi", "比必笔鼻币避闭逼" },
    { "bian", "边变便遍编" },
    { "biao", "表标彪" },
    { "bie", "别憋" },
    { "bin", "宾滨" },
    { "bing", "并冰病兵" },
    { "bo", "播波拨博伯" },
    { "bu", "不步部布补" },
    { "ca", "擦" },
    { "cai", "才材菜采彩" },
    { "can", "参残惨" },
    { "cang", "藏仓" },
    { "cao", "草操曹" },
    { "ce", "册侧测策" },
    { "ceng", "层" },
    { "cha", "查茶插差" },
    { "chai", "拆柴" },
    { "chan", "产缠" },
    { "chang", "长常场唱" },
    { "chao", "朝超抄" },
    { "che", "车彻" },
    { "chen", "陈沉晨" },
    { "cheng", "成城程称" },
    { "chi", "吃迟池持" },
    { "chong", "冲虫重" },
    { "chou", "抽愁丑" },
    { "chu", "出处初楚" },
    { "chuan", "穿船" },
    { "chuang", "创窗" },
    { "chui", "吹垂" },
    { "chun", "春纯" },
    { "ci", "此次词刺" },
    { "cong", "从聪" },
    { "cu", "粗促" },
    { "cuan", "窜" },
    { "cui", "崔脆" },
    { "cun", "村存" },
    { "cuo", "错" },
    { "da", "大打达" },
    { "dai", "代带待戴袋" },
    { "dan", "但单担淡" },
    { "dang", "当党档" },
    { "dao", "到道导岛刀" },
    { "de", "的得德" },
    { "deng", "等灯登" },
    { "di", "第地低底敌" },
    { "dian", "点电店典" },
    { "diao", "掉调" },
    { "die", "跌" },
    { "ding", "定顶丁" },
    { "diu", "丢" },
    { "dong", "东动懂冬" },
    { "dou", "都斗豆" },
    { "du", "读度独杜" },
    { "duan", "短段断" },
    { "dui", "对队" },
    { "dun", "顿" },
    { "duo", "多朵" },
    { "e", "饿恶俄" },
    { "en", "恩" },
    { "er", "而儿耳二" },
    { "fa", "发法" },
    { "fan", "反番烦凡" },
    { "fang", "方房放防" },
    { "fei", "非常飞肥" },
    { "fen", "分份粉" },
    { "feng", "风封丰" },
    { "fou", "否" },
    { "fu", "服父福付复富" },
    { "gai", "该改概" },
    { "gan", "干感赶敢" },
    { "gang", "刚岗钢" },
    { "gao", "高搞告" },
    { "ge", "个歌格革" },
    { "gei", "给" },
    { "gen", "根" },
    { "geng", "更" },
    { "gong", "工公共功" },
    { "gou", "够构购" },
    { "gu", "古故骨谷" },
    { "gua", "挂瓜" },
    { "guai", "怪" },
    { "guan", "关管观馆" },
    { "guang", "光广" },
    { "gui", "贵规鬼" },
    { "gun", "滚" },
    { "guo", "国果过" },
    { "ha", "哈" },
    { "hai", "还孩海" },
    { "han", "汉寒含" },
    { "hang", "航" },
    { "hao", "好号浩" },
    { "he", "和喝河何合" },
    { "hei", "黑" },
    { "hen", "很恨" },
    { "heng", "横恒" },
    { "hong", "红洪宏" },
    { "hou", "后候厚" },
    { "hu", "户互湖护" },
    { "hua", "化画花华" },
    { "huai", "怀" },
    { "huan", "换欢环" },
    { "huang", "黄慌" },
    { "hui", "会回灰辉" },
    { "hun", "混魂" },
    { "huo", "活火或货" },
    { "ji", "几机级记己" },
    { "jia", "加家价假" },
    { "jian", "见间件建" },
    { "jiang", "将讲江" },
    { "jiao", "叫教角" },
    { "jie", "结接节解" },
    { "jin", "进金今紧" },
    { "jing", "经京精景" },
    { "jiu", "九就久酒" },
    { "ju", "句据局举" },
    { "juan", "卷" },
    { "jue", "决觉" },
    { "jun", "军" },
    { "ka", "卡" },
    { "kai", "开凯" },
    { "kan", "看砍" },
    { "kang", "康" },
    { "kao", "考靠" },
    { "ke", "可课克客" },
    { "ken", "肯" },
    { "kong", "空控" },
    { "kou", "口" },
    { "ku", "苦库" },
    { "kua", "跨" },
    { "kuai", "快块" },
    { "kuan", "宽" },
    { "kuang", "况" },
    { "kui", "亏" },
    { "kun", "困" },
    { "kuo", "扩" },
    { "la", "拉" },
    { "lai", "来" },
    { "lan", "蓝烂" },
    { "lang", "浪" },
    { "lao", "老劳" },
    { "le", "乐了" },
    { "lei", "类累" },
    { "leng", "冷" },
    { "li", "里理力立" },
    { "liang", "两亮量" },
    { "liao", "料" },
    { "lie", "列" },
    { "lin", "林临" },
    { "ling", "零领灵" },
    { "liu", "六流留" },
    { "long", "龙弄" },
    { "lou", "楼" },
    { "lu", "路录陆" },
    { "lv", "律绿" },
    { "luan", "乱" },
    { "lun", "论" },
    { "luo", "落罗" },
    { "ma", "吗妈马" },
    { "mai", "买卖" },
    { "man", "满慢" },
    { "mang", "忙" },
    { "mao", "毛冒" },
    { "mei", "没美妹" },
    { "men", "们门" },
    { "meng", "梦" },
    { "mi", "米密迷" },
    { "mian", "面棉" },
    { "miao", "秒" },
    { "min", "民" },
    { "ming", "名明" },
    { "mo", "末模莫" },
    { "mou", "某" },
    { "mu", "母目木" },
    { "na", "那拿哪" },
    { "nai", "奶耐" },
    { "nan", "南难" },
    { "nao", "闹脑" },
    { "ne", "呢" },
    { "nei", "内" },
    { "neng", "能" },
    { "ni", "你尼" },
    { "nian", "年念" },
    { "niang", "娘" },
    { "nin", "您" },
    { "ning", "宁" },
    { "niu", "牛" },
    { "nong", "农" },
    { "nu", "努" },
    { "nv", "女" },
    { "nuan", "暖" },
    { "o", "哦" },
    { "ou", "偶" },
    { "pa", "怕" },
    { "pai", "排拍" },
    { "pan", "盘" },
    { "pang", "旁" },
    { "pao", "跑" },
    { "pei", "配" },
    { "peng", "朋" },
    { "pi", "皮批" },
    { "pian", "片" },
    { "piao", "票" },
    { "pin", "品" },
    { "ping", "平评" },
    { "po", "破" },
    { "pu", "普" },
    { "qi", "七起其气" },
    { "qia", "恰" },
    { "qian", "前千钱" },
    { "qiang", "强" },
    { "qiao", "桥" },
    { "qie", "切" },
    { "qin", "亲勤" },
    { "qing", "请青清" },
    { "qiong", "穷" },
    { "qiu", "求球" },
    { "qu", "去取区" },
    { "quan", "全权" },
    { "que", "确却" },
    { "qun", "群" },
    { "ran", "然" },
    { "rang", "让" },
    { "rao", "绕" },
    { "re", "热" },
    { "ren", "人认任" },
    { "ri", "日" },
    { "rong", "容" },
    { "rou", "肉" },
    { "ru", "入如" },
    { "ruan", "软" },
    { "rui", "瑞" },
    { "run", "润" },
    { "ruo", "若" },
    { "sa", "撒" },
    { "sai", "赛" },
    { "san", "三" },
    { "sao", "扫" },
    { "se", "色" },
    { "sen", "森" },
    { "sha", "杀沙" },
    { "shai", "晒" },
    { "shan", "山善" },
    { "shang", "上商" },
    { "shao", "少烧" },
    { "she", "社设" },
    { "shei", "谁" },
    { "shen", "什深身" },
    { "sheng", "生声省" },
    { "shi", "是十实事" },
    { "shou", "手受收" },
    { "shu", "书树术" },
    { "shua", "刷" },
    { "shuai", "率" },
    { "shuang", "双" },
    { "shui", "水睡" },
    { "shun", "顺" },
    { "shuo", "说" },
    { "si", "四思死" },
    { "song", "送松" },
    { "sou", "搜" },
    { "su", "速素" },
    { "suan", "算" },
    { "sui", "虽岁" },
    { "sun", "孙" },
    { "suo", "所锁" },
    { "ta", "他它她" },
    { "tai", "太台" },
    { "tan", "谈" },
    { "tang", "堂" },
    { "tao", "套" },
    { "te", "特" },
    { "teng", "疼" },
    { "ti", "体提" },
    { "tian", "天田" },
    { "tiao", "条跳" },
    { "tie", "铁" },
    { "ting", "听停" },
    { "tong", "同通统" },
    { "tou", "头" },
    { "tu", "图土" },
    { "tuan", "团" },
    { "tui", "退" },
    { "tun", "吞" },
    { "tuo", "脱" },
    { "wai", "外" },
    { "wan", "完晚万" },
    { "wang", "王往望" },
    { "wei", "为位未围" },
    { "wen", "问文温" },
    { "weng", "翁" },
    { "wo", "我握" },
    { "wu", "五无物" },
    { "xi", "西喜细息" },
    { "xia", "下夏" },
    { "xian", "先现线" },
    { "xiang", "想向像" },
    { "xiao", "小笑效" },
    { "xie", "写些" },
    { "xin", "心新信" },
    { "xing", "行星形" },
    { "xiong", "雄" },
    { "xiu", "修" },
    { "xu", "须许续" },
    { "xuan", "选" },
    { "xue", "学" },
    { "xun", "训" },
    { "ya", "呀压" },
    { "yan", "言眼严" },
    { "yang", "样养阳" },
    { "yao", "要药" },
    { "ye", "也夜业" },
    { "yi", "一以意易" },
    { "yin", "因音引" },
    { "ying", "应影营" },
    { "yong", "用永勇" },
    { "you", "有又友" },
    { "yu", "于语与" },
    { "yuan", "元月原" },
    { "yue", "月约" },
    { "yun", "云运" },
    { "za", "杂" },
    { "zai", "在再载" },
    { "zan", "咱" },
    { "zao", "早造" },
    { "ze", "则" },
    { "zei", "贼" },
    { "zen", "怎" },
    { "zeng", "增" },
    { "zha", "扎" },
    { "zhai", "摘" },
    { "zhan", "站" },
    { "zhang", "张长" },
    { "zhao", "找照" },
    { "zhe", "这者" },
    { "zhen", "真" },
    { "zheng", "正整证" },
    { "zhi", "只知之直" },
    { "zhong", "中重钟" },
    { "zhou", "周" },
    { "zhu", "主住助" },
    { "zhua", "抓" },
    { "zhuan", "转专" },
    { "zhuang", "装" },
    { "zhui", "追" },
    { "zhun", "准" },
    { "zhuo", "着" },
    { "zi", "子自字" },
    { "zong", "总从" },
    { "zou", "走" },
    { "zu", "足族" },
    { "zuan", "钻" },
    { "zui", "最" },
    { "zun", "尊" },
    { "zuo", "作左做" },
};

/* Common multi-char phrases; both full pinyin and short abbreviations are
 * listed so that simple-pinyin input (e.g. "zhg") resolves to a phrase. */
static const py_phrase_t g_py_phrases[] = {
    { "zhongguo", "中国" },
    { "zg", "中国" },
    { "zhg", "中国" },
    { "beijing", "北京" },
    { "bj", "北京" },
    { "shanghai", "上海" },
    { "sh", "上海" },
    { "nihao", "你好" },
    { "nh", "你好" },
    { "xiexie", "谢谢" },
    { "xx", "谢谢" },
    { "zaijian", "再见" },
    { "zaijian", "再见" },
    { "computer", "计算机" },
    { "diannao", "电脑" },
    { "dn", "电脑" },
    { "zhongwen", "中文" },
    { "zw", "中文" },
    { "hanzi", "汉字" },
    { "shurufa", "输入法" },
    { "srf", "输入法" },
};

static void graphics_me_reset(void)
{
    g_ime_pinyin[0] = '\0';
    g_ime_cand_count = 0;
}

/* Rebuild the candidate list from the current pinyin buffer. */
static void graphics_me_rebuild(void)
{
    const char *py = g_ime_pinyin;
    uint32_t plen;

    g_ime_cand_count = 0;
    if (py[0] == '\0') {
        return;
    }
    plen = (uint32_t) strlen(py);

    /* 1) phrases: exact prefix match on full pinyin OR abbreviation field. */
    for (uint32_t i = 0U; i < (sizeof(g_py_phrases) / sizeof(g_py_phrases[0])); i++) {
        const char *pp = g_py_phrases[i].py;

        if (strncmp(pp, py, plen) == 0U || strcmp(pp, py) == 0) {
            strlcpy(g_ime_cands[g_ime_cand_count], g_py_phrases[i].words,
                    sizeof(g_ime_cands[g_ime_cand_count]));
            g_ime_cand_count++;
            if (g_ime_cand_count >= 9U) {
                break;
            }
        }
    }

    /* 2) single syllable exact match: split its hanzi across the 9 slots. */
    for (uint32_t i = 0U; i < (sizeof(g_py_table) / sizeof(g_py_table[0])); i++) {
        if (strcmp(g_py_table[i].py, py) == 0) {
            const char *s = g_py_table[i].chars;

            while (*s != '\0' && g_ime_cand_count < 9U) {
                uint8_t b = (uint8_t) *s;
                uint32_t clen = (b < 0x80U) ? 1U : (b < 0xE0U ? 2U : 3U);

                for (uint32_t k = 0U; k < clen; k++) {
                    g_ime_cands[g_ime_cand_count][k] = s[k];
                }
                g_ime_cands[g_ime_cand_count][clen] = '\0';
                g_ime_cand_count++;
                s += clen;
            }
            break;
        }
    }
}

/* Route a committed UTF-8 string into whichever text input has focus. */
static void graphics_insert_text_into_focused_input(const char *utf8)
{
    np_tab_t *t;
    key_event_t ev;

    if (utf8 == NULL || utf8[0] == '\0') {
        return;
    }
    if (g_notepad_focus) {
        t = np_active_tab();
        if (t != NULL) {
            for (uint32_t k = 0U; utf8[k] != '\0'; k++) {
                np_insert_at(t, t->cursor, utf8[k]);
            }
        }
    } else if (g_run_input_focus) {
        for (uint32_t k = 0U; utf8[k] != '\0'; k++) {
            if (g_run_input_len + 1U >= sizeof(g_run_input)) {
                break;
            }
            g_run_input[g_run_input_len++] = utf8[k];
        }
        g_run_input[g_run_input_len] = '\0';
    } else if (g_file_search_open) {
        uint32_t l = (uint32_t) strlen(g_file_search_query);
        for (uint32_t k = 0U; utf8[k] != '\0'; k++) {
            if (l + 1U >= sizeof(g_file_search_query)) {
                break;
            }
            g_file_search_query[l++] = utf8[k];
        }
        g_file_search_query[l] = '\0';
    } else if (g_terminal_input_focus) {
        memset(&ev, 0, sizeof(ev));
        ev.type = KEY_EVENT_CHAR;
        for (uint32_t k = 0U; utf8[k] != '\0'; k++) {
            ev.ch = utf8[k];
            shell_handle_key_event(&ev);
        }
    }
}

/* ---- Recursive file search over the current drive ---- */
static void graphics_explorer_recursive_search(const char *root, const char *query)
{
    char buffer[GRAPHICS_FILE_LIST_BUFFER];
    uint32_t pos = 0U;

    if (!file_list_dir(root, buffer, sizeof(buffer))) {
        return;
    }
    while (buffer[pos] != '\0' && g_search_result_count < GRAPHICS_FILE_ITEM_MAX) {
        uint32_t ls = pos;
        char name[GRAPHICS_FILE_NAME_MAX];
        uint32_t n;
        char full[GRAPHICS_CLIPBOARD_PATH_MAX];
        bool isdir;

        while (buffer[pos] != '\0' && buffer[pos] != '\n') {
            pos++;
        }
        n = pos - ls;
        if (buffer[pos] == '\n') {
            pos++;
        }
        if (n == 0U) {
            continue;
        }
        if (n >= sizeof(name)) {
            n = sizeof(name) - 1U;
        }
        memcpy(name, &buffer[ls], n);
        name[n] = '\0';
        isdir = (n > 0U && name[n - 1U] == '\\');
        if (isdir) {
            name[n - 1U] = '\0';
        }
        strcpy(full, root);
        graphics_append_path_component(full, sizeof(full), name);
        if (strstr(name, query) != NULL) {
            strlcpy(g_search_results[g_search_result_count], full,
                    sizeof(g_search_results[g_search_result_count]));
            g_search_result_count++;
        }
        if (isdir) {
            graphics_explorer_recursive_search(full, query);
        }
    }
}

static void graphics_explorer_run_search(void)
{
    char root[8];

    g_search_result_count = 0U;
    g_file_searching = false;
    if (g_file_search_query[0] == '\0') {
        graphics_fill_file_browser();
        return;
    }
    root[0] = g_file_current_path[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';
    graphics_explorer_recursive_search(root, g_file_search_query);

    g_file_item_count = 0U;
    memset(g_file_selected, 0, sizeof(g_file_selected));
    for (uint32_t i = 0U; i < g_search_result_count && i < GRAPHICS_FILE_ITEM_MAX; i++) {
        const char *base = graphics_path_basename(g_search_results[i]);
        strlcpy(g_file_items[i].name, base, sizeof(g_file_items[i].name));
        g_file_items[i].is_dir = file_is_dir(g_search_results[i]);
        g_file_item_count++;
    }
    g_file_searching = true;
    g_file_selected_index = 0U;
    g_file_anchor_index = 0U;
    g_file_scroll_offset = 0U;
}

/* ---- Commit a file drop at (x,y) ---- */
static void graphics_drop_commit(uint16_t x, uint16_t y)
{
    /* recycle bin desktop icon? */
    for (uint32_t i = 0U; i < g_desktop_entry_count; i++) {
        uint16_t ix;
        uint16_t iy;
        char p[GRAPHICS_CLIPBOARD_PATH_MAX];

        graphics_desktop_icon_position(i, &ix, &iy);
        if (graphics_point_in_rect(x, y, ix, iy, DESKTOP_ICON_W, DESKTOP_ICON_H)) {
            graphics_build_desktop_entry_path(i, p);
            if (strstr(p, "RECYCLE") != NULL || strstr(p, "Recycle") != NULL) {
                if (!file_send_to_recycle_bin(g_drag_file_path)) {
                    file_delete(g_drag_file_path);
                }
                graphics_notification_post("Drag", "Moved to Recycle Bin");
                graphics_fill_file_browser();
                graphics_refresh_desktop_entries();
                return;
            }
        }
    }
    /* over an explorer window: copy into its current folder */
    for (int32_t i = UI_WINDOW_MAX - 1; i >= 0; i--) {
        if (g_windows[i].visible && g_windows[i].kind == UI_WINDOW_FILES &&
            graphics_point_in_rect(x, y, g_windows[i].x, g_windows[i].y,
                                  g_windows[i].width, g_windows[i].height)) {
            char target[GRAPHICS_CLIPBOARD_PATH_MAX];

            strcpy(target, g_file_current_path);
            graphics_append_path_component(target, sizeof(target),
                                           graphics_path_basename(g_drag_file_path));
            if (graphics_copy_file_path(g_drag_file_path, target)) {
                graphics_notification_post("Drag", "File copied");
            }
            graphics_fill_file_browser();
            return;
        }
    }
    /* plain desktop: copy to desktop */
    {
        char target[GRAPHICS_CLIPBOARD_PATH_MAX];

        strcpy(target, UI_ROOT_DESKTOP);
        graphics_append_path_component(target, sizeof(target),
                                       graphics_path_basename(g_drag_file_path));
        graphics_copy_file_path(g_drag_file_path, target);
        graphics_notification_post("Drag", "Copied to Desktop");
        graphics_refresh_desktop_entries();
    }
}

/* ---- Commit "new file/folder" from the inline name dialog ---- */
static void graphics_newitem_commit(void)
{
    char full[GRAPHICS_CLIPBOARD_PATH_MAX];
    char eol = '\n';

    if (g_newitem_name[0] == '\0') {
        g_newitem_open = false;
        return;
    }
    strcpy(full, g_newitem_parent);
    graphics_append_path_component(full, sizeof(full), g_newitem_name);
    if (g_newitem_is_dir) {
        file_mkdir(full);
    } else {
        file_write(full, &eol, 1U);
    }
    g_newitem_open = false;
    graphics_fill_file_browser();
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

    if (event->type == KEY_EVENT_CHAR && event->status.win_down) {
        char cl = (char)((event->ch >= 'A' && event->ch <= 'Z') ? event->ch + 32 : event->ch);
        if (cl == 'r') {
            graphics_open_window(UI_WINDOW_RUN);
            graphics_draw_shell();
            return;
        }
        if (g_session_logged_in) {
            if (cl == 'e') {
                graphics_launch_named_user_program("explorar");
                return;
            }
            if (cl == 'v') {
                ux_toggle_clipboard_history();
                return;
            }
            if (cl == 'd') {
                for (uint32_t wi = 0; wi < UI_WINDOW_MAX; wi++) {
                    if (g_windows[wi].visible && g_windows[wi].kind != UI_WINDOW_LOGON) {
                        g_windows[wi].minimized = true;
                    }
                }
                g_ux_clip_open = false;
                graphics_draw_shell();
                return;
            }
        }
    }

    if (event->type == KEY_EVENT_TAB && event->status.alt_down && g_session_logged_in) {
        /* Alt+Tab: cycle focus to the previous visible non-minimized window */
        for (int32_t i = UI_WINDOW_MAX - 2; i >= 0; i--) {
            if (g_windows[i].visible && !g_windows[i].minimized && g_windows[i].kind != UI_WINDOW_LOGON) {
                graphics_bring_window_to_front((uint32_t) i);
                graphics_notification_post("Window switch", g_windows[UI_WINDOW_MAX - 1].title);
                graphics_draw_shell();
                break;
            }
        }
        return;
    }

    if (g_ux_clip_open) {
        if (event->type == KEY_EVENT_ESC) {
            g_ux_clip_open = false;
            graphics_draw_shell();
            return;
        }
        if (event->type == KEY_EVENT_DOWN) {
            if (g_ux_clip_sel + 1 < (int32_t) g_ux_clip_count) g_ux_clip_sel++;
            graphics_draw_shell();
            return;
        }
        if (event->type == KEY_EVENT_UP) {
            if (g_ux_clip_sel > 0) g_ux_clip_sel--;
            graphics_draw_shell();
            return;
        }
        if (event->type == KEY_EVENT_CHAR && event->ch == '\n') {
            const char *picked = ux_clip_at((uint32_t) g_ux_clip_sel);
            g_ux_clip_open = false;
            if (picked != NULL) {
                strlcpy(g_clipboard_path, picked, sizeof(g_clipboard_path));
                graphics_notification_post("Clipboard", "Pasted from history");
            }
            graphics_draw_shell();
            return;
        }
        return;
    }

    /* ---- Pinyin IME: Ctrl+Space toggles; buffer + candidate selection ---- */
    if (event->type == KEY_EVENT_CHAR && event->status.ctrl_down &&
        (event->ch == ' ' || event->ch == '\0')) {
        g_ime_on = !g_ime_on;
        graphics_me_reset();
        graphics_notification_post("IME", g_ime_on ? "\u4e2d\u6587\u8f93\u5165" : "English");
        graphics_draw_shell();
        return;
    }
    if (g_ime_pinyin[0] != '\0') {
        if (event->type == KEY_EVENT_ESC) {
            graphics_me_reset();
            graphics_draw_shell();
            return;
        }
        if (event->type == KEY_EVENT_CHAR) {
            char c = event->ch;
            if (c == '\b') {
                uint32_t l = (uint32_t) strlen(g_ime_pinyin);
                if (l > 0U) {
                    g_ime_pinyin[l - 1U] = '\0';
                }
                graphics_me_rebuild();
                graphics_draw_shell();
                return;
            }
            if (c >= '1' && c <= '9') {
                uint32_t idx = (uint32_t) (c - '1');
                if (idx < g_ime_cand_count) {
                    graphics_insert_text_into_focused_input(g_ime_cands[idx]);
                }
                graphics_me_reset();
                graphics_draw_shell();
                return;
            }
            if (c == ' ') {
                if (g_ime_cand_count > 0U) {
                    graphics_insert_text_into_focused_input(g_ime_cands[0]);
                }
                graphics_me_reset();
                graphics_draw_shell();
                return;
            }
            if (!event->status.ctrl_down && !event->status.alt_down &&
                ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                uint32_t l = (uint32_t) strlen(g_ime_pinyin);
                if (l + 1U < sizeof(g_ime_pinyin)) {
                    g_ime_pinyin[l] = (char) ((c >= 'A' && c <= 'Z') ? c + 32 : c);
                    g_ime_pinyin[l + 1U] = '\0';
                    graphics_me_rebuild();
                }
                graphics_draw_shell();
                return;
            }
        }
        return;
    }
    if (g_ime_on && event->type == KEY_EVENT_CHAR &&
        !event->status.ctrl_down && !event->status.alt_down &&
        ((event->ch >= 'a' && event->ch <= 'z') || (event->ch >= 'A' && event->ch <= 'Z'))) {
        g_ime_pinyin[0] = (char) ((event->ch >= 'A' && event->ch <= 'Z') ? event->ch + 32 : event->ch);
        g_ime_pinyin[1] = '\0';
        g_ime_caret_x = 120U;
        g_ime_caret_y = (uint16_t) (FB_HEIGHT - 70U);
        graphics_me_rebuild();
        graphics_draw_shell();
        return;
    }

    /* ---- File properties dialog ---- */
    if (g_properties_open) {
        if (event->type == KEY_EVENT_ESC) {
            g_properties_open = false;
            g_properties_editing = false;
        } else if (event->ch == '\n') {
            graphics_properties_commit();
        } else if (g_properties_editing) {
            if (event->ch == '\b') {
                uint32_t l = (uint32_t) strlen(g_properties_edit_name);
                if (l > 0U) {
                    g_properties_edit_name[l - 1U] = '\0';
                }
            } else if (event->type == KEY_EVENT_CHAR && event->ch >= ' ' &&
                       strlen(g_properties_edit_name) + 1U < sizeof(g_properties_edit_name)) {
                g_properties_edit_name[strlen(g_properties_edit_name)] = event->ch;
                g_properties_edit_name[strlen(g_properties_edit_name) + 1U] = '\0';
            }
        }
        graphics_draw_shell();
        return;
    }

    /* ---- New file/folder name dialog ---- */
    if (g_newitem_open) {
        if (event->type == KEY_EVENT_ESC) {
            g_newitem_open = false;
        } else if (event->ch == '\n') {
            graphics_newitem_commit();
        } else if (event->ch == '\b') {
            uint32_t l = (uint32_t) strlen(g_newitem_name);
            if (l > 0U) {
                g_newitem_name[l - 1U] = '\0';
            }
        } else if (event->type == KEY_EVENT_CHAR && event->ch >= ' ' &&
                   strlen(g_newitem_name) + 1U < sizeof(g_newitem_name)) {
            g_newitem_name[strlen(g_newitem_name)] = event->ch;
            g_newitem_name[strlen(g_newitem_name) + 1U] = '\0';
        }
        graphics_draw_shell();
        return;
    }

    /* ---- Explorer search box ---- */
    if (g_file_search_open) {
        if (event->type == KEY_EVENT_ESC) {
            g_file_search_open = false;
            g_file_search_query[0] = '\0';
            if (g_file_searching) {
                g_file_searching = false;
                graphics_fill_file_browser();
            }
        } else if (event->ch == '\n') {
            graphics_explorer_run_search();
        } else if (event->ch == '\b') {
            uint32_t l = (uint32_t) strlen(g_file_search_query);
            if (l > 0U) {
                g_file_search_query[l - 1U] = '\0';
            }
        } else if (event->type == KEY_EVENT_CHAR && event->ch >= ' ' &&
                   strlen(g_file_search_query) + 1U < sizeof(g_file_search_query)) {
            g_file_search_query[strlen(g_file_search_query)] = event->ch;
            g_file_search_query[strlen(g_file_search_query) + 1U] = '\0';
        }
        graphics_draw_shell();
        return;
    }

    if (!g_session_logged_in) {
        if (event->type == KEY_EVENT_UP || event->type == KEY_EVENT_DOWN) {
            uint32_t user_count = session_user_count();

            if (user_count > 0) {
                if (event->type == KEY_EVENT_UP) {
                    g_selected_login_user = g_selected_login_user == 0 ?
                                            user_count - 1U : g_selected_login_user - 1U;
                } else {
                    g_selected_login_user = g_selected_login_user + 1U >= user_count ?
                                            0 : g_selected_login_user + 1U;
                }
                graphics_select_login_user(g_selected_login_user);
                graphics_draw_shell();
            }
            return;
        }
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
        if (g_notepad_focus) {
            graphics_notepad_key(event);
            graphics_draw_shell();
            return;
        }
        if (g_terminal_input_focus) {
            ui_window_t *tw = NULL;
            for (int32_t wi = UI_WINDOW_MAX - 1; wi >= 0; wi--) {
                if (g_windows[wi].visible && graphics_window_is_terminal(&g_windows[wi])) {
                    tw = &g_windows[wi];
                    break;
                }
            }
            /* Shift+PageUp / Shift+PageDown scroll back through history */
            if (tw != NULL && event->type == KEY_EVENT_PAGE_UP && event->status.shift_down) {
                graphics_terminal_scroll_by(-(int32_t)graphics_terminal_visible_rows(tw), tw);
                graphics_draw_shell();
                return;
            }
            if (tw != NULL && event->type == KEY_EVENT_PAGE_DOWN && event->status.shift_down) {
                graphics_terminal_scroll_by((int32_t)graphics_terminal_visible_rows(tw), tw);
                graphics_draw_shell();
                return;
            }
            /* while reviewing history, any other navigation key jumps to newest output */
            if (tw != NULL && tw->console_scroll_manual) {
                tw->console_scroll_manual = false;
                tw->console_scroll_offset = graphics_terminal_max_scroll(tw);
            }
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
        if (event->status.ctrl_down && event->type == KEY_EVENT_CHAR) {
            char cl = event->ch;
            if (cl >= 'A' && cl <= 'Z') { cl = (char)(cl + 32); }
            if (cl == 'v') {
                char pb[GRAPHICS_CLIPBOARD_PATH_MAX];
                graphics_clipboard_get_text(pb, sizeof(pb));
                graphics_insert_text_into_focused_input(pb);
                graphics_draw_shell();
                return;
            }
            if (event->status.shift_down && cl == 'p') {
                console_set_scheme((console_scheme() + 1) % console_scheme_count());
                graphics_draw_shell();
                return;
            }
            if (event->status.shift_down && cl == 't') {
                tty_tab_new();
                graphics_draw_shell();
                return;
            }
            if (event->status.shift_down && cl == 'w') {
                tty_tab_close();
                graphics_draw_shell();
                return;
            }
        }
        if (event->type == KEY_EVENT_TAB && event->status.ctrl_down) {
            tty_tab_next();
            graphics_draw_shell();
            return;
        }
        shell_handle_key_event(event);
        graphics_draw_shell();
        return;
    }

    if (g_notepad_focus) {
        graphics_notepad_key(event);
        graphics_draw_shell();
        return;
    }

    if (g_run_input_focus) {
        if (event->status.ctrl_down && event->ch == 'v') {
            char pb[GRAPHICS_CLIPBOARD_PATH_MAX];
            graphics_clipboard_get_text(pb, sizeof(pb));
            graphics_insert_text_into_focused_input(pb);
            graphics_draw_shell();
            return;
        }
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
    static bool ux_inited;

    if (!g_graphics_active) {
        return;
    }

    if (!ux_inited) {
        ux_inited = true;
        ux_experience_init();
    }

    ui_network_update(now_ticks);

    /* repaint while a notification is about to expire so toasts fade away */
    if (g_session_logged_in && ux_notifications_alive()) {
        ux_notify_expire();
        graphics_draw_shell();
        return;
    }

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
    /*
     * The BIOS and UEFI PE loaders normalize sections in place. Keep the
     * display geometry explicit here so graphics startup does not depend on
     * the loader preserving C static initializers in .data.
     */
    g_graphics_width = GRAPHICS_WIDTH;
    g_graphics_height = GRAPHICS_HEIGHT;
    g_graphics_mode_index = 0;
    g_double_buffer_enabled = true;
    g_vsync_enabled = false;
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
    g_uac_signed = false;
    g_uac_signature_valid = false;
    g_uac_publisher_trusted = false;
    g_secure_desktop = false;
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
         * Program the common Bochs VBE sequence. Keep the controller disabled
         * while changing the mode and select the latest common register set.
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
