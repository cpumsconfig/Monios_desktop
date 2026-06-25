#include "common.h"
#include "kernel.h"
#include "registry.h"

#define REGISTRY_MAX_ENTRIES 32

typedef struct {
    bool used;
    char key[32];
    char value[64];
} registry_entry_t;

static registry_entry_t g_registry[REGISTRY_MAX_ENTRIES];

void registry_init(void)
{
    memset(g_registry, 0, sizeof(g_registry));
    registry_set("ui.version", "1.0");
    registry_set("shell.default", "/apps/explorar.exe");
}

bool registry_set(const char *key, const char *value)
{
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
    for (uint32_t i = 0; i < REGISTRY_MAX_ENTRIES; i++) {
        if (g_registry[i].used && strcmp(g_registry[i].key, key) == 0) {
            return g_registry[i].value;
        }
    }
    return NULL;
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
