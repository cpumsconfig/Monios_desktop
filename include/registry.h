#ifndef _REGISTRY_H_
#define _REGISTRY_H_

#include "stdbool.h"
#include "stdint.h"

void registry_init(void);
bool registry_set(const char *key, const char *value);
const char *registry_get(const char *key);
uint32_t registry_count(void);

#endif
