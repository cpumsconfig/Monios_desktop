#ifndef _OSUI_H_
#define _OSUI_H_

#include "stdbool.h"
#include "stdint.h"

#define OSUI_COMPONENT_NAME_MAX 32
#define OSUI_COMPONENT_PATH_MAX 96
#define OSUI_COMPONENT_VERSION_MAX 16
#define OSUI_COMPONENT_MAX 64

typedef enum {
    OSUI_COMPONENT_BUILTIN = 0x00000001U,
    OSUI_COMPONENT_CUSTOM = 0x00000002U,
    OSUI_COMPONENT_DESKTOP = 0x00000004U,
    OSUI_COMPONENT_SECURE = 0x00000008U,
    OSUI_COMPONENT_WIDGET = 0x00000010U,
    OSUI_COMPONENT_LAYOUT = 0x00000020U,
    OSUI_COMPONENT_THEME = 0x00000040U,
    OSUI_COMPONENT_NAVIGATION = 0x00000080U,
    OSUI_COMPONENT_COMMAND = 0x00000100U,
    OSUI_COMPONENT_FEEDBACK = 0x00000200U
} osui_component_flags_t;

typedef struct {
    char name[OSUI_COMPONENT_NAME_MAX];
    char path[OSUI_COMPONENT_PATH_MAX];
    char version[OSUI_COMPONENT_VERSION_MAX];
    uint32_t flags;
    uint16_t priority;
    uint16_t reserved;
    bool loaded;
} osui_component_info_t;

typedef struct {
    bool initialized;
    bool theme_ready;
    uint32_t component_count;
    uint32_t builtin_count;
    uint32_t custom_count;
    uint32_t widget_count;
    uint32_t layout_count;
    uint32_t theme_count;
    uint32_t secure_count;
    char theme_name[32];
    char status[64];
} osui_info_t;

void osui_theme_init(void);
void osui_theme_refresh(void);
const osui_info_t *osui_info(void);
const char *osui_status(void);
void osui_init(void);
bool osui_component_register(const char *name, uint32_t flags);
bool osui_component_register_custom(const char *path, const char *manifest);
bool osui_component_load_custom(void);
uint32_t osui_component_count(void);
const osui_component_info_t *osui_component_at(uint32_t index);
void osui_desktop_tick(void);

#endif
