#include "common.h"
#include "kernel.h"
#include "file.h"
#include "registry.h"
#include "ui.h"

#define REGISTRY_MAX_ENTRIES 64

typedef struct {
    bool used;
    char key[REGISTRY_KEY_MAX];
    char value[REGISTRY_VALUE_MAX];
} registry_entry_t;

static registry_entry_t g_registry[REGISTRY_MAX_ENTRIES];

static bool registry_copy_limited(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t index = 0;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return false;
    }
    while (src[index] != '\0' && index + 1 < dst_size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
    return src[index] == '\0';
}

static bool registry_build_assoc_key(const char *extension, char key[REGISTRY_KEY_MAX])
{
    uint32_t out = 0;

    if (extension == NULL || extension[0] == '\0') {
        return false;
    }
    if (!registry_copy_limited(key, REGISTRY_KEY_MAX, "assoc.")) {
        return false;
    }
    out = (uint32_t) strlen(key);
    for (uint32_t i = extension[0] == '.' ? 1u : 0u; extension[i] != '\0'; i++) {
        char ch = extension[i];

        if (out + 1 >= REGISTRY_KEY_MAX) {
            return false;
        }
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char) (ch - 'A' + 'a');
        }
        key[out++] = ch;
    }
    key[out] = '\0';
    return true;
}

static const char *registry_extension_for_path(const char *path)
{
    const char *slash;
    const char *dot;

    if (path == NULL) {
        return NULL;
    }
    slash = strrchr(path, '/');
    dot = strrchr(path, '.');
    if (dot == NULL || dot[1] == '\0' || (slash != NULL && dot < slash)) {
        return NULL;
    }
    return dot;
}

void registry_init(void)
{
    memset(g_registry, 0, sizeof(g_registry));
    registry_set("ui.version", "1.0");
    registry_set("system.drive", "C:");
    registry_set("system.root", UI_SYSTEM_ROOT);
    registry_set("shell.default", UI_EXPLORER_PATH);
    registry_set("shell.desktop", UI_ROOT_DESKTOP);
    registry_set("ui.wallpaper", UI_WALLPAPER_PATH);
    registry_set("ui.cursor", UI_CURSOR_ASSET_PATH);
    registry_set("ui.osui", UI_OSUI_DLL_PATH);
    registry_set("ui.osui.theme", "MoniOS OSUI");
    registry_set("default.filemanager", UI_EXPLORER_PATH);
    registry_set("default.player", UI_PLAYER_PATH);
    registry_set("default.browser", UI_BROWSER_PATH);
    registry_set("default.editor", UI_NOTEPAD_PATH);
    registry_set("drivers.boot.count", "0");
    registry_set_default_app(".sys", UI_SYSINST_PATH);
    registry_set_default_app(".wav", UI_PLAYER_PATH);
    registry_set_default_app(".m4a", UI_PLAYER_PATH);
    registry_set_default_app(".txt", UI_NOTEPAD_PATH);
}

bool registry_set(const char *key, const char *value)
{
    if (key == NULL || value == NULL ||
        strlen(key) >= REGISTRY_KEY_MAX || strlen(value) >= REGISTRY_VALUE_MAX) {
        return false;
    }
    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (g_registry[i].used && strcmp(g_registry[i].key, key) == 0) {
            strcpy(g_registry[i].value, value);
            return true;
        }
    }
    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (!g_registry[i].used) {
            g_registry[i].used = true;
            strcpy(g_registry[i].key, key);
            strcpy(g_registry[i].value, value);
            return true;
        }
    }
    return false;
}

const char *registry_get(const char *key)
{
    if (key == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (g_registry[i].used && strcmp(g_registry[i].key, key) == 0) {
            return g_registry[i].value;
        }
    }
    return NULL;
}

bool registry_get_copy(const char *key, char *value, uint32_t value_size)
{
    const char *found = registry_get(key);

    if (found == NULL) {
        return false;
    }
    return registry_copy_limited(value, value_size, found);
}

bool registry_delete(const char *key)
{
    if (key == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (g_registry[i].used && strcmp(g_registry[i].key, key) == 0) {
            memset(&g_registry[i], 0, sizeof(g_registry[i]));
            return true;
        }
    }
    return false;
}

bool registry_set_default_app(const char *extension, const char *app_path)
{
    char key[REGISTRY_KEY_MAX];

    if (!registry_build_assoc_key(extension, key)) {
        return false;
    }
    return registry_set(key, app_path);
}

bool registry_default_app_for_extension(const char *extension, char *app_path, uint32_t app_path_size)
{
    char key[REGISTRY_KEY_MAX];

    if (!registry_build_assoc_key(extension, key)) {
        return false;
    }
    return registry_get_copy(key, app_path, app_path_size);
}

bool registry_default_app_for_path(const char *path, char *app_path, uint32_t app_path_size)
{
    const char *extension = registry_extension_for_path(path);

    if (extension == NULL) {
        return false;
    }
    return registry_default_app_for_extension(extension, app_path, app_path_size);
}

uint32_t registry_count(void)
{
    uint32_t count = 0;

    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (g_registry[i].used) {
            count++;
        }
    }
    return count;
}
