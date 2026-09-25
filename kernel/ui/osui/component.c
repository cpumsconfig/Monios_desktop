#include "common.h"
#include "file.h"
#include "kernel.h"
#include "osui.h"
#include "path.h"
#include "ui.h"

static osui_component_info_t g_components[OSUI_COMPONENT_MAX];
static uint32_t g_component_count;

static bool osui_component_register_internal(const char *name,
                                              uint32_t flags,
                                              const char *path,
                                              const char *version,
                                              uint16_t priority)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    for (uint32_t i = 0; i < g_component_count; i++) {
        if (strcasecmp(g_components[i].name, name) == 0) {
            g_components[i].flags |= flags;
            if (path != NULL && path[0] != '\0') {
                strlcpy(g_components[i].path, path, sizeof(g_components[i].path));
            }
            if (version != NULL && version[0] != '\0') {
                strlcpy(g_components[i].version, version, sizeof(g_components[i].version));
            }
            if (priority != 0) {
                g_components[i].priority = priority;
            }
            g_components[i].loaded = true;
            osui_theme_refresh();
            return true;
        }
    }
    if (g_component_count >= OSUI_COMPONENT_MAX ||
        strlcpy(g_components[g_component_count].name,
                name,
                sizeof(g_components[g_component_count].name)) >=
            sizeof(g_components[g_component_count].name)) {
        return false;
    }
    g_components[g_component_count].flags = flags;
    if (path != NULL) {
        strlcpy(g_components[g_component_count].path,
                path,
                sizeof(g_components[g_component_count].path));
    }
    if (version != NULL) {
        strlcpy(g_components[g_component_count].version,
                version,
                sizeof(g_components[g_component_count].version));
    }
    g_components[g_component_count].priority = priority;
    g_components[g_component_count].loaded = true;
    g_component_count++;
    osui_theme_refresh();
    return true;
}

bool osui_component_register(const char *name, uint32_t flags)
{
    return osui_component_register_internal(name, flags, NULL, NULL, 0);
}

static void osui_trim_line(char *text)
{
    uint32_t start = 0;
    uint32_t length;

    if (text == NULL) {
        return;
    }
    while (text[start] == ' ' || text[start] == '\t' ||
           text[start] == '\r' || text[start] == '\n') {
        start++;
    }
    length = (uint32_t) strlen(text + start);
    if (start > 0) {
        memmove(text, text + start, length + 1);
    }
    while (length > 0 &&
           (text[length - 1] == ' ' || text[length - 1] == '\t' ||
            text[length - 1] == '\r' || text[length - 1] == '\n')) {
        text[--length] = '\0';
    }
}

static uint16_t osui_parse_priority(const char *text)
{
    uint32_t value = 0;

    if (text == NULL) {
        return 0;
    }
    while (*text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t) (*text - '0');
        if (value >= 0xFFFFU) {
            return 0xFFFFU;
        }
        text++;
    }
    return (uint16_t) value;
}

static uint32_t osui_parse_flags(const char *text)
{
    char value[96];
    uint32_t flags = 0;
    char *token;

    if (text == NULL) {
        return 0;
    }
    strlcpy(value, text, sizeof(value));
    token = value;
    while (token != NULL && *token != '\0') {
        char *next = strchr(token, ',');

        if (next != NULL) {
            *next = '\0';
        }
        osui_trim_line(token);
        if (strcasecmp(token, "builtin") == 0) {
            flags |= OSUI_COMPONENT_BUILTIN;
        } else if (strcasecmp(token, "custom") == 0) {
            flags |= OSUI_COMPONENT_CUSTOM;
        } else if (strcasecmp(token, "desktop") == 0) {
            flags |= OSUI_COMPONENT_DESKTOP;
        } else if (strcasecmp(token, "secure") == 0) {
            flags |= OSUI_COMPONENT_SECURE;
        } else if (strcasecmp(token, "widget") == 0) {
            flags |= OSUI_COMPONENT_WIDGET;
        } else if (strcasecmp(token, "layout") == 0) {
            flags |= OSUI_COMPONENT_LAYOUT;
        } else if (strcasecmp(token, "theme") == 0) {
            flags |= OSUI_COMPONENT_THEME;
        } else if (strcasecmp(token, "navigation") == 0) {
            flags |= OSUI_COMPONENT_NAVIGATION;
        } else if (strcasecmp(token, "command") == 0) {
            flags |= OSUI_COMPONENT_COMMAND;
        } else if (strcasecmp(token, "feedback") == 0) {
            flags |= OSUI_COMPONENT_FEEDBACK;
        }
        token = next != NULL ? next + 1 : NULL;
    }
    return flags;
}

bool osui_component_register_custom(const char *path, const char *manifest)
{
    char text[256];
    char name[OSUI_COMPONENT_NAME_MAX] = "";
    char version[OSUI_COMPONENT_VERSION_MAX] = "";
    uint32_t flags = OSUI_COMPONENT_CUSTOM;
    uint16_t priority = 0;
    uint32_t pos = 0;

    if (path == NULL || path[0] == '\0' || manifest == NULL) {
        return false;
    }
    strlcpy(text, manifest, sizeof(text));
    while (text[pos] != '\0') {
        char *line = text + pos;
        char *end = strchr(line, '\n');
        char *equals;

        if (end != NULL) {
            *end = '\0';
            pos = (uint32_t) (end - text) + 1U;
        } else {
            pos = (uint32_t) strlen(text);
        }
        osui_trim_line(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        equals = strchr(line, '=');
        if (equals == NULL) {
            if (name[0] == '\0') {
                strlcpy(name, line, sizeof(name));
            }
            continue;
        }
        *equals = '\0';
        osui_trim_line(line);
        osui_trim_line(equals + 1);
        if (strcasecmp(line, "name") == 0) {
            strlcpy(name, equals + 1, sizeof(name));
        } else if (strcasecmp(line, "version") == 0) {
            strlcpy(version, equals + 1, sizeof(version));
        } else if (strcasecmp(line, "flags") == 0) {
            flags |= osui_parse_flags(equals + 1);
        } else if (strcasecmp(line, "priority") == 0) {
            priority = osui_parse_priority(equals + 1);
        }
    }
    return name[0] != '\0' &&
           osui_component_register_internal(name,
                                            flags,
                                            path,
                                            version,
                                            priority);
}

bool osui_component_load_custom(void)
{
    char listing[2048];
    uint32_t loaded = 0;

    if (!file_exists(UI_CUSTOM_COMPONENT_DIR) &&
        !file_mkdir(UI_CUSTOM_COMPONENT_DIR)) {
        return false;
    }
    if (!file_list_dir(UI_CUSTOM_COMPONENT_DIR, listing, sizeof(listing))) {
        return true;
    }
    for (uint32_t pos = 0; listing[pos] != '\0';) {
        char name[OSUI_COMPONENT_NAME_MAX];
        char path[128];
        char manifest[256];
        uint32_t start = pos;
        uint32_t length = 0;
        int32_t size;

        while (listing[pos] != '\0' && listing[pos] != '\n') {
            pos++;
        }
        while (start + length < pos && length + 1 < sizeof(name)) {
            name[length] = listing[start + length];
            length++;
        }
        name[length] = '\0';
        if (listing[pos] == '\n') {
            pos++;
        }
        if (length < 4 || length >= sizeof(name) - 1 ||
            name[length - 1] == PATH_SEPARATOR ||
            strcasecmp(name + length - 4, ".osc") != 0) {
            continue;
        }
        strcpy(path, UI_CUSTOM_COMPONENT_DIR);
        strcpy(path + strlen(path), PATH_SEPARATOR_STR);
        strcpy(path + strlen(path), name);
        size = file_read(path, manifest, sizeof(manifest) - 1);
        if (size <= 0) {
            continue;
        }
        manifest[size] = '\0';
        if (osui_component_register_custom(path, manifest)) {
            loaded++;
        }
    }
    return loaded > 0 || file_exists(UI_CUSTOM_COMPONENT_DIR);
}

void osui_init(void)
{
    g_component_count = 0;
    osui_theme_init();
    osui_component_register("desktop", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_DESKTOP | OSUI_COMPONENT_LAYOUT);
    osui_component_register("taskbar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("start-menu", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("window", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("panel", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("card", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("toolbar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("menu", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("navrail", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_NAVIGATION | OSUI_COMPONENT_LAYOUT);
    osui_component_register("tabbar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_NAVIGATION | OSUI_COMPONENT_WIDGET);
    osui_component_register("commandbar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_COMMAND | OSUI_COMPONENT_LAYOUT);
    osui_component_register("icon-button", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_COMMAND | OSUI_COMPONENT_WIDGET);
    osui_component_register("context-menu", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_COMMAND | OSUI_COMPONENT_LAYOUT);
    osui_component_register("statusbar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_FEEDBACK | OSUI_COMPONENT_LAYOUT);
    osui_component_register("toast", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_FEEDBACK | OSUI_COMPONENT_WIDGET);
    osui_component_register("dialog", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_SECURE | OSUI_COMPONENT_FEEDBACK | OSUI_COMPONENT_WIDGET);
    osui_component_register("listview", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("button", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("input", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("checkbox", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("toggle", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("radio", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("slider", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("avatar", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("callout", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("badge", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("chip", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("progress", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_WIDGET);
    osui_component_register("theme", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    osui_component_register("palette", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    osui_component_register("login", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_SECURE | OSUI_COMPONENT_WIDGET);
    osui_component_register("uac", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_SECURE | OSUI_COMPONENT_WIDGET);
    osui_component_register("acrylic", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    osui_component_register("mica", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    osui_component_register("reveal", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_FEEDBACK);
    osui_component_register("accent-color", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    osui_component_register("rounded-corners", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_LAYOUT);
    osui_component_register("elevation", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_FEEDBACK);
    osui_component_register("motion", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_FEEDBACK);
    osui_component_register("typography", OSUI_COMPONENT_BUILTIN | OSUI_COMPONENT_THEME);
    if (file_exists(UI_SYSTEM_DIR)) {
        osui_component_load_custom();
    }
    osui_theme_refresh();
    log_write("osui: component registry ready");
}

uint32_t osui_component_count(void)
{
    return g_component_count;
}

const osui_component_info_t *osui_component_at(uint32_t index)
{
    return index < g_component_count ? &g_components[index] : NULL;
}
