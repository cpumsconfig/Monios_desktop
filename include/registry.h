#ifndef _REGISTRY_H_
#define _REGISTRY_H_

#include "stdbool.h"
#include "stdint.h"

#define REGISTRY_KEY_MAX    48U
#define REGISTRY_VALUE_MAX 128U

void registry_init(void);
bool registry_set(const char *key, const char *value);
const char *registry_get(const char *key);
bool registry_get_copy(const char *key, char *value, uint32_t value_size);
bool registry_delete(const char *key);
bool registry_set_default_app(const char *extension, const char *app_path);
bool registry_default_app_for_extension(const char *extension, char *app_path, uint32_t app_path_size);
bool registry_default_app_for_path(const char *path, char *app_path, uint32_t app_path_size);
uint32_t registry_count(void);

#endif
