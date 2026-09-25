#include "common.h"
#include "osui.h"
#include "registry.h"
#include "ui.h"

static osui_info_t g_osui_info;

static void osui_write_count(const char *key, uint32_t value)
{
    char text[16];
    uint32_t index = 0;

    if (key == NULL) {
        return;
    }
    if (value == 0) {
        text[index++] = '0';
    } else {
        char reverse[16];

        while (value > 0 && index < sizeof(reverse)) {
            reverse[index++] = (char) ('0' + (value % 10U));
            value /= 10U;
        }
        for (uint32_t i = 0; i < index; i++) {
            text[i] = reverse[index - 1U - i];
        }
    }
    text[index] = '\0';
    registry_set(key, text);
}

void osui_theme_init(void)
{
    memset(&g_osui_info, 0, sizeof(g_osui_info));
    g_osui_info.initialized = true;
    strlcpy(g_osui_info.theme_name, "MoniOS Fluent", sizeof(g_osui_info.theme_name));
    strlcpy(g_osui_info.status, "osui: WinUI 3 fluent visual system pending", sizeof(g_osui_info.status));
}

void osui_theme_refresh(void)
{
    uint32_t total = 0;
    uint32_t builtin = 0;
    uint32_t custom = 0;
    uint32_t widget = 0;
    uint32_t layout = 0;
    uint32_t theme = 0;
    uint32_t secure = 0;

    if (!g_osui_info.initialized) {
        return;
    }

    total = osui_component_count();
    for (uint32_t i = 0; i < total; i++) {
        const osui_component_info_t *component = osui_component_at(i);

        if (component == NULL) {
            continue;
        }
        if ((component->flags & OSUI_COMPONENT_BUILTIN) != 0) {
            builtin++;
        }
        if ((component->flags & OSUI_COMPONENT_CUSTOM) != 0) {
            custom++;
        }
        if ((component->flags & OSUI_COMPONENT_WIDGET) != 0) {
            widget++;
        }
        if ((component->flags & OSUI_COMPONENT_LAYOUT) != 0) {
            layout++;
        }
        if ((component->flags & OSUI_COMPONENT_THEME) != 0) {
            theme++;
        }
        if ((component->flags & OSUI_COMPONENT_SECURE) != 0) {
            secure++;
        }
    }

    g_osui_info.theme_ready = true;
    g_osui_info.component_count = total;
    g_osui_info.builtin_count = builtin;
    g_osui_info.custom_count = custom;
    g_osui_info.widget_count = widget;
    g_osui_info.layout_count = layout;
    g_osui_info.theme_count = theme;
    g_osui_info.secure_count = secure;
    strlcpy(g_osui_info.status, "osui: WinUI 3 fluent visual system ready", sizeof(g_osui_info.status));
    registry_set("ui.osui.theme", g_osui_info.theme_name);
    registry_set("ui.osui.status", g_osui_info.status);
    registry_set("ui.osui.style", "fluent");
    registry_set("ui.osui.density", "comfortable");
    registry_set("ui.osui.accent", "0078D4");
    registry_set("ui.osui.corner_radius", "8");
    registry_set("ui.osui.card_radius", "12");
    registry_set("ui.osui.popup_radius", "16");
    registry_set("ui.osui.spacing", "8");
    registry_set("ui.osui.font_family", "Segoe UI");
    registry_set("ui.osui.shadow", "soft");
    osui_write_count("ui.osui.components", total);
    osui_write_count("ui.osui.builtins", builtin);
    osui_write_count("ui.osui.custom", custom);
    osui_write_count("ui.osui.widgets", widget);
    osui_write_count("ui.osui.layout", layout);
    osui_write_count("ui.osui.theme.components", theme);
    osui_write_count("ui.osui.secure", secure);
}

const osui_info_t *osui_info(void)
{
    osui_theme_refresh();
    return &g_osui_info;
}

const char *osui_status(void)
{
    osui_theme_refresh();
    return g_osui_info.status;
}
