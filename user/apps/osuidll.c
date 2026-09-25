#define OSUI_DLL_BUILD 1
#include "osui_dll.h"
#include "windows_dll.h"

#define OSUI_DLL_EXPORT __attribute__((dllexport))
#define OSUI_TEXT_BUFFER_MAX 128U
#define OSUI_TEXT_ASCII_W 8U
#define OSUI_TEXT_WIDE_W 16U
#define OSUI_COLOR_SUCCESS 0x002F8E5D
#define OSUI_COLOR_SUCCESS_SOFT 0x00E4F5EC
#define OSUI_COLOR_WARNING 0x00B76E00
#define OSUI_COLOR_WARNING_SOFT 0x00FFF2D8
#define OSUI_COLOR_INFO 0x003974D9
#define OSUI_COLOR_INFO_SOFT 0x00E4EEFF

int DllMainCRTStartup(void *module, uint32_t reason, void *reserved)
{
    (void) module;
    (void) reason;
    (void) reserved;
    return 1;
}

static const osui_palette_t g_osui_default_palette = {
    0x00F3F6F7,
    0x00F9FBFB,
    0x00FFFFFF,
    0x00D5DEE0,
    0x00AEBCC0,
    0x001C2930,
    0x005C6A70,
    0x0000717F,
    0x00DDF3F1,
    0x00C83E50
};

static osui_palette_t g_osui_palette;
static bool g_osui_palette_ready;

static const osui_palette_t *osui_palette(void)
{
    if (!g_osui_palette_ready) {
        g_osui_palette = g_osui_default_palette;
        g_osui_palette_ready = true;
    }
    return &g_osui_palette;
}

static void osui_fill_rect(osui_rect_t rect, uint32_t color)
{
    if (rect.width == 0 || rect.height == 0) {
        return;
    }
    (void) windows_fill_rect(rect.x, rect.y, rect.width, rect.height, color);
}

static void osui_fill_soft_rect(osui_rect_t rect, uint32_t color)
{
    osui_rect_t center;
    osui_rect_t middle;

    if (rect.width < 6 || rect.height < 6) {
        osui_fill_rect(rect, color);
        return;
    }
    center = rect;
    center.x = (uint16_t) (rect.x + 2);
    center.width = (uint16_t) (rect.width - 4);
    middle = rect;
    middle.y = (uint16_t) (rect.y + 2);
    middle.height = (uint16_t) (rect.height - 4);
    osui_fill_rect(center, color);
    osui_fill_rect(middle, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 1), (uint16_t) (rect.y + 1), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 2), (uint16_t) (rect.y + 1), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 1), (uint16_t) (rect.y + rect.height - 2), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 2),
                                   (uint16_t) (rect.y + rect.height - 2), 1, 1 }, color);
}

static void osui_outline(osui_rect_t rect, uint32_t color)
{
    if (rect.width == 0 || rect.height == 0) {
        return;
    }
    osui_fill_rect((osui_rect_t) { rect.x, rect.y, rect.width, 1 }, color);
    osui_fill_rect((osui_rect_t) { rect.x, (uint16_t) (rect.y + rect.height - 1), rect.width, 1 }, color);
    osui_fill_rect((osui_rect_t) { rect.x, rect.y, 1, rect.height }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 1), rect.y, 1, rect.height }, color);
}

static void osui_soft_outline(osui_rect_t rect, uint32_t color)
{
    if (rect.width < 6 || rect.height < 6) {
        osui_outline(rect, color);
        return;
    }
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 2), rect.y, (uint16_t) (rect.width - 4), 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 2), (uint16_t) (rect.y + rect.height - 1),
                                   (uint16_t) (rect.width - 4), 1 }, color);
    osui_fill_rect((osui_rect_t) { rect.x, (uint16_t) (rect.y + 2), 1, (uint16_t) (rect.height - 4) }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 1), (uint16_t) (rect.y + 2),
                                   1, (uint16_t) (rect.height - 4) }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 1), (uint16_t) (rect.y + 1), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 2), (uint16_t) (rect.y + 1), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 1), (uint16_t) (rect.y + rect.height - 2), 1, 1 }, color);
    osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + rect.width - 2),
                                   (uint16_t) (rect.y + rect.height - 2), 1, 1 }, color);
}

static void osui_shadow(osui_rect_t rect)
{
    osui_rect_t shadow = rect;

    shadow.x = (uint16_t) (shadow.x + 6);
    shadow.y = (uint16_t) (shadow.y + 7);
    osui_fill_soft_rect(shadow, 0x002C4055);
    shadow.x = (uint16_t) (rect.x + 3);
    shadow.y = (uint16_t) (rect.y + 4);
    osui_soft_outline(shadow, 0x008EA7C0);
}

static uint32_t osui_utf8_char_len(unsigned char ch)
{
    if ((ch & 0x80U) == 0) {
        return 1;
    }
    if ((ch & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((ch & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((ch & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

static uint16_t osui_text_width(const char *text)
{
    uint16_t width = 0;

    while (text != 0 && *text != '\0') {
        unsigned char ch = (unsigned char) *text;
        uint32_t len = osui_utf8_char_len(ch);

        if (ch == '\n' || ch == '\r') {
            text++;
            continue;
        }
        width = (uint16_t) (width + (len == 1 ? OSUI_TEXT_ASCII_W : OSUI_TEXT_WIDE_W));
        while (len > 0 && *text != '\0') {
            text++;
            len--;
        }
    }
    return width;
}

static void osui_text_fit(char *out, uint32_t out_size, const char *text, uint16_t max_width)
{
    const char *source = text;
    uint32_t used = 0;
    uint16_t width = 0;
    bool clipped = false;

    if (out == 0 || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (text == 0 || max_width == 0) {
        return;
    }
    while (*text != '\0') {
        unsigned char ch = (unsigned char) *text;
        uint32_t len = osui_utf8_char_len(ch);
        uint16_t char_width = (uint16_t) (len == 1 ? OSUI_TEXT_ASCII_W : OSUI_TEXT_WIDE_W);

        if (ch == '\r' || ch == '\n') {
            break;
        }
        if (width + char_width > max_width || used + len >= out_size) {
            clipped = true;
            break;
        }
        for (uint32_t i = 0; i < len && text[i] != '\0' && used + 1 < out_size; i++) {
            out[used++] = text[i];
        }
        text += len;
        width = (uint16_t) (width + char_width);
    }
    if (clipped && max_width >= 24 && out_size >= 4) {
        text = source;
        used = 0;
        width = 0;
        while (*text != '\0') {
            unsigned char ch = (unsigned char) *text;
            uint32_t len = osui_utf8_char_len(ch);
            uint16_t char_width = (uint16_t) (len == 1 ? OSUI_TEXT_ASCII_W : OSUI_TEXT_WIDE_W);

            if (ch == '\r' || ch == '\n') {
                break;
            }
            if (width + char_width + 24 > max_width || used + len + 3 >= out_size) {
                break;
            }
            for (uint32_t i = 0; i < len && text[i] != '\0' && used + 4 < out_size; i++) {
                out[used++] = text[i];
            }
            text += len;
            width = (uint16_t) (width + char_width);
        }
        if (used + 3 < out_size) {
            out[used++] = '.';
            out[used++] = '.';
            out[used++] = '.';
        }
    }
    out[used] = '\0';
}

static void osui_draw_text_fit(uint16_t x, uint16_t y, uint16_t width, const char *text, uint32_t color)
{
    char fitted[OSUI_TEXT_BUFFER_MAX];

    osui_text_fit(fitted, sizeof(fitted), text != 0 ? text : "", width);
    (void) windows_draw_text(x, y, fitted, color);
}

static void osui_draw_centered_text(osui_rect_t rect, const char *text, uint32_t color)
{
    char fitted[OSUI_TEXT_BUFFER_MAX];
    uint16_t available = rect.width > 16 ? (uint16_t) (rect.width - 16) : rect.width;
    uint16_t text_width;
    uint16_t x = (uint16_t) (rect.x + 12);
    uint16_t y = (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0));

    osui_text_fit(fitted, sizeof(fitted), text != 0 ? text : "", available);
    text_width = osui_text_width(fitted);
    if (text_width < rect.width) {
        x = (uint16_t) (rect.x + (rect.width - text_width) / 2);
    }
    (void) windows_draw_text(x, y, fitted, color);
}

static uint32_t osui_state_accent(const osui_palette_t *palette, uint32_t state)
{
    if ((state & OSUI_STATE_DANGER) != 0) {
        return palette->danger;
    }
    if ((state & OSUI_STATE_SUCCESS) != 0) {
        return OSUI_COLOR_SUCCESS;
    }
    if ((state & OSUI_STATE_WARNING) != 0) {
        return OSUI_COLOR_WARNING;
    }
    if ((state & OSUI_STATE_MUTED) != 0) {
        return palette->muted;
    }
    return palette->accent;
}

static uint32_t osui_state_soft_fill(const osui_palette_t *palette, uint32_t state)
{
    if ((state & OSUI_STATE_DISABLED) != 0) {
        return palette->surface;
    }
    if ((state & OSUI_STATE_DANGER) != 0) {
        return 0x00FFF0F2;
    }
    if ((state & OSUI_STATE_SUCCESS) != 0) {
        return OSUI_COLOR_SUCCESS_SOFT;
    }
    if ((state & OSUI_STATE_WARNING) != 0) {
        return OSUI_COLOR_WARNING_SOFT;
    }
    if ((state & (OSUI_STATE_SELECTED | OSUI_STATE_PRIMARY)) != 0) {
        return palette->accent_soft;
    }
    if ((state & OSUI_STATE_HOVERED) != 0) {
        return OSUI_COLOR_INFO_SOFT;
    }
    return palette->surface_alt;
}

static uint32_t osui_state_text(const osui_palette_t *palette, uint32_t state)
{
    if ((state & OSUI_STATE_DISABLED) != 0) {
        return palette->muted;
    }
    if ((state & OSUI_STATE_DANGER) != 0) {
        return palette->danger;
    }
    if ((state & OSUI_STATE_SUCCESS) != 0) {
        return OSUI_COLOR_SUCCESS;
    }
    if ((state & OSUI_STATE_WARNING) != 0) {
        return OSUI_COLOR_WARNING;
    }
    return palette->text;
}

static void osui_focus_rect(osui_rect_t rect, uint32_t state, uint32_t color)
{
    if ((state & OSUI_STATE_FOCUSED) == 0 || rect.width < 6 || rect.height < 6) {
        return;
    }
    osui_soft_outline((osui_rect_t) { (uint16_t) (rect.x + 2), (uint16_t) (rect.y + 2),
                                      (uint16_t) (rect.width - 4), (uint16_t) (rect.height - 4) },
                      color);
}

static void osui_make_initials(char *out, uint32_t out_size, const char *name)
{
    uint32_t used = 0;
    bool next_word = true;

    if (out == 0 || out_size == 0) {
        return;
    }
    out[0] = '\0';
    while (name != 0 && *name != '\0' && used + 1 < out_size) {
        unsigned char ch = (unsigned char) *name++;

        if (ch == ' ' || ch == '\t' || ch == '-' || ch == '_') {
            next_word = true;
            continue;
        }
        if (!next_word && used > 0) {
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            ch = (unsigned char) (ch - ('a' - 'A'));
        }
        out[used++] = (char) ch;
        out[used] = '\0';
        next_word = false;
        if (used == 2) {
            break;
        }
    }
    if (used == 0) {
        out[0] = '?';
        out[1] = '\0';
    }
}

static uint16_t osui_min_u16(uint16_t a, uint16_t b)
{
    return a < b ? a : b;
}

static uint16_t osui_sat_sub_u16(uint16_t a, uint16_t b)
{
    return a > b ? (uint16_t) (a - b) : 0;
}

static uint32_t osui_button_state_from_flags(uint32_t flags, uint32_t state)
{
    if ((flags & OSUI_BUTTON_DISABLED) != 0) {
        state |= OSUI_STATE_DISABLED;
    }
    if ((flags & OSUI_BUTTON_PRIMARY) != 0) {
        state |= OSUI_STATE_PRIMARY;
    }
    if ((flags & OSUI_BUTTON_DANGER) != 0) {
        state |= OSUI_STATE_DANGER;
    }
    return state;
}

OSUI_DLL_EXPORT
uint32_t osui_abi_version(void)
{
    return OSUI_DLL_ABI_VERSION;
}

OSUI_DLL_EXPORT
const osui_palette_t *osui_default_palette(void)
{
    return &g_osui_default_palette;
}

OSUI_DLL_EXPORT
void osui_set_palette(const osui_palette_t *palette)
{
    if (palette == 0) {
        return;
    }
    g_osui_palette = *palette;
    g_osui_palette_ready = true;
}

OSUI_DLL_EXPORT
void osui_reset_palette(void)
{
    g_osui_palette = g_osui_default_palette;
    g_osui_palette_ready = true;
}

OSUI_DLL_EXPORT
void osui_canvas(uint32_t color)
{
    uint16_t width = windows_screen_width();
    uint16_t height = windows_screen_height();

    if (width == 0 || height == 0) {
        width = 1024;
        height = 768;
    }
    (void) windows_fill_rect(0, 0, width, height, color);
}

OSUI_DLL_EXPORT
void osui_panel(osui_rect_t rect)
{
    const osui_palette_t *palette = osui_palette();

    osui_shadow(rect);
    osui_fill_soft_rect(rect, palette->surface);
    osui_soft_outline(rect, palette->border_strong);
}

OSUI_DLL_EXPORT
void osui_card(osui_rect_t rect)
{
    const osui_palette_t *palette = osui_palette();

    osui_fill_soft_rect(rect, palette->surface_alt);
    osui_soft_outline(rect, palette->border);
}

OSUI_DLL_EXPORT
void osui_titlebar(osui_rect_t rect, const char *title, bool active)
{
    const osui_palette_t *palette = osui_palette();
    osui_rect_t accent;
    osui_rect_t control;
    uint16_t title_width;

    osui_fill_soft_rect(rect, active ? palette->surface_alt : palette->surface);
    osui_soft_outline(rect, palette->border);
    accent = rect;
    accent.x = (uint16_t) (rect.x + 10);
    accent.y = (uint16_t) (rect.y + 8);
    accent.width = 10;
    accent.height = 10;
    osui_fill_soft_rect(accent, active ? palette->accent : palette->muted);
    title_width = rect.width > 116 ? (uint16_t) (rect.width - 116) : 0;
    osui_draw_text_fit((uint16_t) (rect.x + 28), (uint16_t) (rect.y + 7), title_width,
                       title != 0 ? title : "", active ? palette->text : palette->muted);

    control = rect;
    control.x = (uint16_t) (rect.x + rect.width - 74);
    control.y = (uint16_t) (rect.y + 5);
    control.width = 16;
    control.height = 18;
    osui_card(control);
    (void) windows_draw_text((uint16_t) (control.x + 5), (uint16_t) (control.y + 1), "-", palette->muted);
    control.x = (uint16_t) (control.x + 20);
    osui_card(control);
    osui_fill_rect((osui_rect_t) { (uint16_t) (control.x + 5), (uint16_t) (control.y + 5), 6, 6 }, palette->muted);
    control.x = (uint16_t) (control.x + 20);
    osui_fill_soft_rect(control, 0x00FFF2F4);
    osui_soft_outline(control, 0x00E3A9B1);
    (void) windows_draw_text((uint16_t) (control.x + 4), (uint16_t) (control.y + 1), "x", palette->danger);
}

OSUI_DLL_EXPORT
void osui_label(uint16_t x, uint16_t y, const char *text, bool muted)
{
    const osui_palette_t *palette = osui_palette();

    (void) windows_draw_text(x, y, text != 0 ? text : "", muted ? palette->muted : palette->text);
}

OSUI_DLL_EXPORT
void osui_button(osui_rect_t rect, const char *label, uint32_t flags)
{
    osui_button_state(rect, label, flags, 0);
}

OSUI_DLL_EXPORT
void osui_button_state(osui_rect_t rect, const char *label, uint32_t flags, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t fill = palette->surface_alt;
    uint32_t border = palette->border_strong;
    uint32_t text = palette->text;
    bool solid;

    state = osui_button_state_from_flags(flags, state);
    solid = (state & (OSUI_STATE_PRIMARY | OSUI_STATE_DANGER)) != 0 &&
            (flags & OSUI_BUTTON_GHOST) == 0;

    if ((state & OSUI_STATE_DISABLED) != 0) {
        fill = palette->surface;
        border = palette->border;
        text = palette->muted;
    } else if (solid) {
        fill = osui_state_accent(palette, state);
        border = fill;
        text = 0x00FFFFFF;
        if ((state & OSUI_STATE_PRESSED) != 0) {
            fill = 0x002E5CC8;
            border = fill;
        }
    } else {
        fill = osui_state_soft_fill(palette, state);
        text = osui_state_text(palette, state);
        if ((state & (OSUI_STATE_HOVERED | OSUI_STATE_SELECTED | OSUI_STATE_FOCUSED |
                      OSUI_STATE_PRIMARY | OSUI_STATE_DANGER | OSUI_STATE_SUCCESS |
                      OSUI_STATE_WARNING)) != 0 ||
            (flags & OSUI_BUTTON_GHOST) != 0) {
            border = osui_state_accent(palette, state);
        }
        if ((state & OSUI_STATE_PRESSED) != 0) {
            fill = palette->border;
        }
    }
    osui_fill_soft_rect(rect, fill);
    osui_soft_outline(rect, border);
    osui_draw_centered_text(rect, label, text);
    osui_focus_rect(rect, state, solid ? 0x00FFFFFF : osui_state_accent(palette, state));
}

OSUI_DLL_EXPORT
void osui_input(osui_rect_t rect, const char *value, const char *placeholder,
                bool focused, bool password)
{
    const osui_palette_t *palette = osui_palette();
    const char *text = value != 0 && value[0] != '\0' ? value : placeholder;
    uint32_t border = focused ? palette->accent : palette->border_strong;

    osui_fill_soft_rect(rect, palette->surface_alt);
    osui_soft_outline(rect, border);
    if (value != 0 && value[0] != '\0' && password) {
        uint16_t max_dots = rect.width > 28 ? (uint16_t) ((rect.width - 24) / 9) : 0;

        for (uint16_t i = 0; value[i] != '\0' && i < max_dots; i++) {
            osui_fill_rect((osui_rect_t) { (uint16_t) (rect.x + 12 + i * 9),
                                           (uint16_t) (rect.y + rect.height / 2 - 2), 5, 5 }, palette->text);
        }
    } else {
        osui_draw_text_fit((uint16_t) (rect.x + 12),
                           (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                           rect.width > 24 ? (uint16_t) (rect.width - 24) : 0,
                           text != 0 ? text : "",
                           value != 0 && value[0] != '\0' ? palette->text : palette->muted);
    }
    osui_focus_rect(rect, focused ? OSUI_STATE_FOCUSED : 0, palette->accent);
}

OSUI_DLL_EXPORT
void osui_progress(osui_rect_t rect, uint32_t value)
{
    const osui_palette_t *palette = osui_palette();
    osui_rect_t fill;

    if (value > 100) {
        value = 100;
    }
    osui_fill_soft_rect(rect, palette->surface);
    osui_soft_outline(rect, palette->border);
    if (rect.width <= 4 || value == 0) {
        return;
    }
    fill = rect;
    fill.x = (uint16_t) (rect.x + 2);
    fill.y = (uint16_t) (rect.y + 2);
    fill.width = (uint16_t) (((uint32_t) (rect.width - 4) * value) / 100U);
    fill.height = rect.height > 4 ? (uint16_t) (rect.height - 4) : rect.height;
    osui_fill_soft_rect(fill, palette->accent);
}

OSUI_DLL_EXPORT
void osui_avatar(osui_rect_t rect, const char *name, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    char initials[3];
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fill = osui_state_soft_fill(palette, state);
    uint32_t text = osui_state_text(palette, state);
    uint16_t size = osui_min_u16(rect.width, rect.height);
    osui_rect_t avatar;
    bool solid = (state & (OSUI_STATE_PRIMARY | OSUI_STATE_SELECTED | OSUI_STATE_CHECKED)) != 0;

    if (size == 0) {
        return;
    }
    avatar.x = (uint16_t) (rect.x + (rect.width - size) / 2);
    avatar.y = (uint16_t) (rect.y + (rect.height - size) / 2);
    avatar.width = size;
    avatar.height = size;
    osui_make_initials(initials, sizeof(initials), name);
    if ((state & OSUI_STATE_DISABLED) != 0) {
        fill = palette->surface;
        text = palette->muted;
    } else if (solid) {
        fill = accent;
        text = 0x00FFFFFF;
    }
    osui_fill_soft_rect(avatar, fill);
    osui_soft_outline(avatar, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : accent);
    osui_draw_centered_text(avatar, initials, text);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_callout(osui_rect_t rect, const char *title, const char *message, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fill = osui_state_soft_fill(palette, state);
    uint32_t text = osui_state_text(palette, state);
    osui_rect_t stripe;
    uint16_t text_x;
    uint16_t text_width;

    if (rect.width == 0 || rect.height == 0) {
        return;
    }
    if ((state & OSUI_STATE_DISABLED) != 0) {
        fill = palette->surface;
    }
    osui_fill_soft_rect(rect, fill);
    osui_soft_outline(rect, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : accent);
    stripe.x = (uint16_t) (rect.x + 6);
    stripe.y = (uint16_t) (rect.y + 7);
    stripe.width = 4;
    stripe.height = rect.height > 14 ? (uint16_t) (rect.height - 14) : rect.height;
    osui_fill_soft_rect(stripe, (state & OSUI_STATE_DISABLED) != 0 ? palette->muted : accent);
    text_x = (uint16_t) (rect.x + 18);
    text_width = rect.width > 26 ? (uint16_t) (rect.width - 26) : 0;
    if (rect.height >= 18) {
        osui_draw_text_fit(text_x, (uint16_t) (rect.y + 7), text_width,
                           title != 0 ? title : "", text);
    }
    if (rect.height >= 36) {
        osui_draw_text_fit(text_x, (uint16_t) (rect.y + 25), text_width,
                           message != 0 ? message : "", palette->muted);
    }
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_badge(osui_rect_t rect, const char *text, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fill = osui_state_soft_fill(palette, state);
    uint32_t fg = osui_state_text(palette, state);

    if ((state & OSUI_STATE_PRIMARY) != 0 && (state & OSUI_STATE_DISABLED) == 0) {
        fill = accent;
        fg = 0x00FFFFFF;
    }
    osui_fill_soft_rect(rect, fill);
    osui_soft_outline(rect, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : accent);
    osui_draw_centered_text(rect, text, fg);
}

OSUI_DLL_EXPORT
void osui_chip(osui_rect_t rect, const char *text, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fill = osui_state_soft_fill(palette, state);
    uint32_t fg = osui_state_text(palette, state);
    osui_rect_t dot = { (uint16_t) (rect.x + 10), (uint16_t) (rect.y + rect.height / 2 - 3), 6, 6 };

    osui_fill_soft_rect(rect, fill);
    osui_soft_outline(rect, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : accent);
    osui_fill_soft_rect(dot, (state & OSUI_STATE_DISABLED) != 0 ? palette->muted : accent);
    osui_draw_text_fit((uint16_t) (rect.x + 24),
                       (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                       rect.width > 34 ? (uint16_t) (rect.width - 34) : 0,
                       text != 0 ? text : "", fg);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_checkbox(osui_rect_t rect, const char *label, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t border = (state & OSUI_STATE_DISABLED) != 0 ? palette->border : palette->border_strong;
    uint32_t fg = osui_state_text(palette, state);
    uint16_t box_size = osui_min_u16(rect.height, 18);
    osui_rect_t box = { rect.x, (uint16_t) (rect.y + (rect.height - box_size) / 2), box_size, box_size };

    osui_fill_soft_rect(box, (state & OSUI_STATE_CHECKED) != 0 ? accent : palette->surface_alt);
    osui_soft_outline(box, (state & OSUI_STATE_CHECKED) != 0 ? accent : border);
    if ((state & OSUI_STATE_MIXED) != 0 && box.width > 8 && box.height > 4) {
        osui_fill_rect((osui_rect_t) { (uint16_t) (box.x + 4), (uint16_t) (box.y + box.height / 2 - 1),
                                       (uint16_t) (box.width - 8), 2 }, 0x00FFFFFF);
    } else if ((state & OSUI_STATE_CHECKED) != 0 && box.width >= 15 && box.height >= 15) {
        osui_fill_rect((osui_rect_t) { (uint16_t) (box.x + 4), (uint16_t) (box.y + 9), 4, 2 }, 0x00FFFFFF);
        osui_fill_rect((osui_rect_t) { (uint16_t) (box.x + 7), (uint16_t) (box.y + 11), 3, 2 }, 0x00FFFFFF);
        osui_fill_rect((osui_rect_t) { (uint16_t) (box.x + 10), (uint16_t) (box.y + 6), 5, 2 }, 0x00FFFFFF);
    }
    osui_draw_text_fit((uint16_t) (rect.x + box_size + 10),
                       (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                       rect.width > box_size + 12 ? (uint16_t) (rect.width - box_size - 12) : 0,
                       label != 0 ? label : "", fg);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_toggle(osui_rect_t rect, const char *label, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fg = osui_state_text(palette, state);
    uint16_t track_h = osui_min_u16(rect.height, 22);
    uint16_t track_w = osui_min_u16(rect.width, 44);
    uint16_t knob = track_h > 8 ? (uint16_t) (track_h - 8) : track_h;
    osui_rect_t track = { rect.x, (uint16_t) (rect.y + (rect.height - track_h) / 2), track_w, track_h };
    osui_rect_t handle = { (uint16_t) (track.x + 4), (uint16_t) (track.y + 4), knob, knob };
    bool on = (state & OSUI_STATE_CHECKED) != 0;

    if (on && track_w > knob + 8) {
        handle.x = (uint16_t) (track.x + track_w - knob - 4);
    }
    osui_fill_soft_rect(track, on && (state & OSUI_STATE_DISABLED) == 0 ? accent : palette->surface);
    osui_soft_outline(track, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : (on ? accent : palette->border_strong));
    osui_fill_soft_rect(handle, (state & OSUI_STATE_DISABLED) != 0 ? palette->border_strong : palette->surface_alt);
    if (rect.width > track_w + 10) {
        osui_draw_text_fit((uint16_t) (rect.x + track_w + 10),
                           (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                           (uint16_t) (rect.width - track_w - 10), label != 0 ? label : "", fg);
    }
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_radio(osui_rect_t rect, const char *label, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fg = osui_state_text(palette, state);
    uint16_t box_size = osui_min_u16(rect.height, 18);
    osui_rect_t ring = { rect.x, (uint16_t) (rect.y + (rect.height - box_size) / 2), box_size, box_size };

    osui_fill_soft_rect(ring, palette->surface_alt);
    osui_soft_outline(ring, (state & OSUI_STATE_DISABLED) != 0 ? palette->border : accent);
    if ((state & OSUI_STATE_CHECKED) != 0 && ring.width > 10 && ring.height > 10) {
        osui_fill_soft_rect((osui_rect_t) { (uint16_t) (ring.x + 5), (uint16_t) (ring.y + 5),
                                            (uint16_t) (ring.width - 10), (uint16_t) (ring.height - 10) },
                            (state & OSUI_STATE_DISABLED) != 0 ? palette->muted : accent);
    }
    osui_draw_text_fit((uint16_t) (rect.x + box_size + 10),
                       (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                       rect.width > box_size + 12 ? (uint16_t) (rect.width - box_size - 12) : 0,
                       label != 0 ? label : "", fg);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_slider(osui_rect_t rect, uint32_t value, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = (state & OSUI_STATE_DISABLED) != 0 ? palette->muted : osui_state_accent(palette, state);
    osui_rect_t track;
    osui_rect_t fill;
    osui_rect_t knob;
    uint16_t usable;
    uint16_t fill_w;

    if (value > 100) {
        value = 100;
    }
    if (rect.width < 18 || rect.height < 8) {
        return;
    }
    track.x = (uint16_t) (rect.x + 6);
    track.y = (uint16_t) (rect.y + rect.height / 2 - 3);
    track.width = (uint16_t) (rect.width - 12);
    track.height = 6;
    osui_fill_soft_rect(track, palette->surface);
    osui_soft_outline(track, palette->border);

    usable = track.width > 6 ? (uint16_t) (track.width - 6) : track.width;
    fill_w = (uint16_t) (((uint32_t) usable * value) / 100U);
    fill = track;
    fill.width = osui_min_u16(track.width, (uint16_t) (fill_w + 3));
    osui_fill_soft_rect(fill, accent);

    knob.width = 12;
    knob.height = osui_min_u16(rect.height, 20);
    knob.x = (uint16_t) (track.x + fill_w);
    knob.y = (uint16_t) (rect.y + (rect.height - knob.height) / 2);
    if (knob.x + knob.width > rect.x + rect.width) {
        knob.x = (uint16_t) (rect.x + rect.width - knob.width);
    }
    osui_fill_soft_rect(knob, palette->surface_alt);
    osui_soft_outline(knob, accent);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_tabbar(osui_rect_t rect, const char *const *tabs, uint32_t count, uint32_t selected)
{
    const osui_palette_t *palette = osui_palette();
    uint16_t tab_width;

    if (tabs == 0 || count == 0 || rect.width == 0 || rect.height == 0) {
        return;
    }
    if (count > 12) {
        count = 12;
    }
    tab_width = (uint16_t) (rect.width / count);
    if (tab_width == 0) {
        return;
    }
    osui_fill_soft_rect(rect, palette->surface);
    osui_soft_outline(rect, palette->border);
    for (uint32_t i = 0; i < count; i++) {
        osui_rect_t tab = { (uint16_t) (rect.x + i * tab_width), rect.y, tab_width, rect.height };
        bool active = i == selected;

        if (i + 1 == count) {
            tab.width = osui_sat_sub_u16((uint16_t) (rect.x + rect.width), tab.x);
        }
        if (active) {
            osui_fill_soft_rect(tab, palette->surface_alt);
            osui_fill_rect((osui_rect_t) { (uint16_t) (tab.x + 8), (uint16_t) (tab.y + tab.height - 3),
                                           tab.width > 16 ? (uint16_t) (tab.width - 16) : tab.width, 3 },
                           palette->accent);
        }
        if (i > 0) {
            osui_fill_rect((osui_rect_t) { tab.x, (uint16_t) (tab.y + 8), 1,
                                           tab.height > 16 ? (uint16_t) (tab.height - 16) : tab.height },
                           palette->border);
        }
        osui_draw_centered_text(tab, tabs[i] != 0 ? tabs[i] : "", active ? palette->text : palette->muted);
    }
}

OSUI_DLL_EXPORT
void osui_list_item(osui_rect_t rect, const char *title, const char *subtitle, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint32_t fill = ((state & (OSUI_STATE_SELECTED | OSUI_STATE_HOVERED)) != 0) ?
                    osui_state_soft_fill(palette, state) : palette->surface_alt;
    uint16_t text_x = (uint16_t) (rect.x + 14);
    uint16_t text_width = rect.width > 28 ? (uint16_t) (rect.width - 28) : 0;

    osui_fill_soft_rect(rect, fill);
    osui_soft_outline(rect, palette->border);
    if ((state & OSUI_STATE_SELECTED) != 0) {
        osui_fill_soft_rect((osui_rect_t) { (uint16_t) (rect.x + 6), (uint16_t) (rect.y + 10), 4,
                                            rect.height > 20 ? (uint16_t) (rect.height - 20) : rect.height },
                            accent);
    }
    osui_draw_text_fit(text_x, (uint16_t) (rect.y + 10), text_width, title != 0 ? title : "",
                       osui_state_text(palette, state));
    osui_draw_text_fit(text_x, (uint16_t) (rect.y + 32), text_width, subtitle != 0 ? subtitle : "",
                       (state & OSUI_STATE_DISABLED) != 0 ? palette->muted : palette->muted);
    osui_focus_rect(rect, state, accent);
}

OSUI_DLL_EXPORT
void osui_divider(uint16_t x, uint16_t y, uint16_t width)
{
    const osui_palette_t *palette = osui_palette();

    osui_fill_rect((osui_rect_t) { x, y, width, 1 }, palette->border);
}

OSUI_DLL_EXPORT
void osui_separator(uint16_t x, uint16_t y, uint16_t width)
{
    osui_divider(x, y, width);
}

OSUI_DLL_EXPORT
void osui_navrail(osui_rect_t rect, const char *const *items,
                  uint32_t count, uint32_t selected)
{
    const osui_palette_t *palette = osui_palette();
    uint16_t item_height;

    if (items == 0 || count == 0 || rect.width < 32 || rect.height < 24) {
        return;
    }
    if (count > 12) {
        count = 12;
    }
    {
        uint16_t inner_height = rect.height > 12 ? (uint16_t) (rect.height - 12) : rect.height;

        item_height = (uint16_t) (inner_height / count);
        if (item_height == 0) {
            item_height = 1;
        }
    }
    osui_fill_soft_rect(rect, palette->surface);
    osui_soft_outline(rect, palette->border);
    for (uint32_t i = 0; i < count; i++) {
        osui_rect_t item;
        uint32_t text_color = i == selected ? palette->text : palette->muted;

        item.x = (uint16_t) (rect.x + 6);
        item.y = (uint16_t) (rect.y + 6 + i * item_height);
        item.width = rect.width > 12 ? (uint16_t) (rect.width - 12) : rect.width;
        item.height = item_height > 4 ? (uint16_t) (item_height - 4) : item_height;
        if (i == selected) {
            osui_fill_soft_rect(item, palette->accent_soft);
            osui_fill_rect((osui_rect_t) { item.x, (uint16_t) (item.y + 4), 3,
                                           item.height > 8 ? (uint16_t) (item.height - 8) : item.height },
                           palette->accent);
        }
        osui_draw_text_fit((uint16_t) (item.x + 14),
                           (uint16_t) (item.y + (item.height > 16 ? (item.height - 16) / 2 : 0)),
                           item.width > 22 ? (uint16_t) (item.width - 22) : 0,
                           items[i] != 0 ? items[i] : "",
                           text_color);
    }
}

OSUI_DLL_EXPORT
void osui_commandbar(osui_rect_t rect, const char *title,
                     const char *const *actions, uint32_t count,
                     uint32_t selected)
{
    const osui_palette_t *palette = osui_palette();
    uint16_t title_width;
    uint16_t action_width;

    if (rect.width < 96 || rect.height < 24) {
        return;
    }
    osui_fill_soft_rect(rect, palette->surface_alt);
    osui_soft_outline(rect, palette->border);
    osui_fill_rect((osui_rect_t) { rect.x, rect.y, 4, rect.height }, palette->accent);
    title_width = rect.width > 220 ? 190 : (uint16_t) (rect.width / 3);
    osui_draw_text_fit((uint16_t) (rect.x + 16), (uint16_t) (rect.y + 7),
                       title_width, title != 0 ? title : "", palette->text);
    if (actions == 0 || count == 0 || rect.width <= title_width + 24) {
        return;
    }
    if (count > 6) {
        count = 6;
    }
    action_width = (uint16_t) ((rect.width - title_width - 24) / count);
    if (action_width < 24) {
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        osui_rect_t action = {
            (uint16_t) (rect.x + title_width + 12 + i * action_width),
            (uint16_t) (rect.y + 4),
            action_width > 4 ? (uint16_t) (action_width - 4) : action_width,
            rect.height > 8 ? (uint16_t) (rect.height - 8) : rect.height
        };

        if (i == selected) {
            osui_fill_soft_rect(action, palette->accent_soft);
            osui_fill_rect((osui_rect_t) { (uint16_t) (action.x + 8),
                                           (uint16_t) (action.y + action.height - 3),
                                           action.width > 16 ? (uint16_t) (action.width - 16) : action.width,
                                           2 }, palette->accent);
        }
        osui_draw_centered_text(action,
                                actions[i] != 0 ? actions[i] : "",
                                i == selected ? palette->text : palette->muted);
    }
}

OSUI_DLL_EXPORT
void osui_statusbar(osui_rect_t rect, const char *left,
                    const char *right, uint32_t state)
{
    const osui_palette_t *palette = osui_palette();
    uint32_t accent = osui_state_accent(palette, state);
    uint16_t right_width = rect.width > 160 ? 140 : (uint16_t) (rect.width / 3);

    if (rect.width < 48 || rect.height < 12) {
        return;
    }
    osui_fill_rect(rect, palette->surface);
    osui_fill_rect((osui_rect_t) { rect.x, rect.y, rect.width, 1 }, palette->border);
    osui_fill_soft_rect((osui_rect_t) { (uint16_t) (rect.x + 10),
                                        (uint16_t) (rect.y + rect.height / 2 - 3),
                                        6, 6 }, accent);
    osui_draw_text_fit((uint16_t) (rect.x + 24),
                       (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                       rect.width > right_width + 34 ? (uint16_t) (rect.width - right_width - 34) : 0,
                       left != 0 ? left : "", palette->muted);
    if (right != 0 && right_width > 0) {
        osui_draw_text_fit((uint16_t) (rect.x + rect.width - right_width - 12),
                           (uint16_t) (rect.y + (rect.height > 16 ? (rect.height - 16) / 2 : 0)),
                           right_width, right, palette->text);
    }
}

OSUI_DLL_EXPORT
void osui_present(void)
{
    windows_present();
}
