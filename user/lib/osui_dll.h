#ifndef _OSUI_DLL_H_
#define _OSUI_DLL_H_

#include "stdbool.h"
#include "stdint.h"

#define OSUI_DLL_ABI_VERSION 3U

#if !defined(OSUI_DLL_BUILD) && defined(__GNUC__)
#define OSUI_DLL_API __attribute__((dllimport))
#else
#define OSUI_DLL_API
#endif

#define OSUI_BUTTON_PRIMARY  0x00000001U
#define OSUI_BUTTON_DANGER   0x00000002U
#define OSUI_BUTTON_GHOST    0x00000004U
#define OSUI_BUTTON_DISABLED 0x00000008U

#define OSUI_STATE_DISABLED   0x00000001U
#define OSUI_STATE_HOVERED    0x00000002U
#define OSUI_STATE_PRESSED    0x00000004U
#define OSUI_STATE_FOCUSED    0x00000008U
#define OSUI_STATE_SELECTED   0x00000010U
#define OSUI_STATE_CHECKED    0x00000020U
#define OSUI_STATE_MIXED      0x00000040U
#define OSUI_STATE_READONLY   0x00000080U
#define OSUI_STATE_PRIMARY    0x00000100U
#define OSUI_STATE_DANGER     0x00000200U
#define OSUI_STATE_SUCCESS    0x00000400U
#define OSUI_STATE_WARNING    0x00000800U
#define OSUI_STATE_MUTED      0x00001000U

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} osui_rect_t;

typedef struct {
    uint32_t canvas;
    uint32_t surface;
    uint32_t surface_alt;
    uint32_t border;
    uint32_t border_strong;
    uint32_t text;
    uint32_t muted;
    uint32_t accent;
    uint32_t accent_soft;
    uint32_t danger;
} osui_palette_t;

OSUI_DLL_API uint32_t osui_abi_version(void);
OSUI_DLL_API const osui_palette_t *osui_default_palette(void);
OSUI_DLL_API void osui_set_palette(const osui_palette_t *palette);
OSUI_DLL_API void osui_reset_palette(void);
OSUI_DLL_API void osui_canvas(uint32_t color);
OSUI_DLL_API void osui_panel(osui_rect_t rect);
OSUI_DLL_API void osui_card(osui_rect_t rect);
OSUI_DLL_API void osui_titlebar(osui_rect_t rect, const char *title, bool active);
OSUI_DLL_API void osui_label(uint16_t x, uint16_t y, const char *text, bool muted);
OSUI_DLL_API void osui_button(osui_rect_t rect, const char *label, uint32_t flags);
OSUI_DLL_API void osui_button_state(osui_rect_t rect, const char *label,
                                    uint32_t flags, uint32_t state);
OSUI_DLL_API void osui_input(osui_rect_t rect, const char *value, const char *placeholder,
                             bool focused, bool password);
OSUI_DLL_API void osui_progress(osui_rect_t rect, uint32_t value);
OSUI_DLL_API void osui_avatar(osui_rect_t rect, const char *name, uint32_t state);
OSUI_DLL_API void osui_callout(osui_rect_t rect, const char *title, const char *message, uint32_t state);
OSUI_DLL_API void osui_badge(osui_rect_t rect, const char *text, uint32_t state);
OSUI_DLL_API void osui_chip(osui_rect_t rect, const char *text, uint32_t state);
OSUI_DLL_API void osui_checkbox(osui_rect_t rect, const char *label, uint32_t state);
OSUI_DLL_API void osui_toggle(osui_rect_t rect, const char *label, uint32_t state);
OSUI_DLL_API void osui_radio(osui_rect_t rect, const char *label, uint32_t state);
OSUI_DLL_API void osui_slider(osui_rect_t rect, uint32_t value, uint32_t state);
OSUI_DLL_API void osui_tabbar(osui_rect_t rect, const char *const *tabs, uint32_t count, uint32_t selected);
OSUI_DLL_API void osui_list_item(osui_rect_t rect, const char *title, const char *subtitle, uint32_t state);
OSUI_DLL_API void osui_divider(uint16_t x, uint16_t y, uint16_t width);
OSUI_DLL_API void osui_separator(uint16_t x, uint16_t y, uint16_t width);
OSUI_DLL_API void osui_navrail(osui_rect_t rect, const char *const *items,
                               uint32_t count, uint32_t selected);
OSUI_DLL_API void osui_commandbar(osui_rect_t rect, const char *title,
                                  const char *const *actions, uint32_t count,
                                  uint32_t selected);
OSUI_DLL_API void osui_statusbar(osui_rect_t rect, const char *left,
                                 const char *right, uint32_t state);
OSUI_DLL_API void osui_present(void);

#endif
