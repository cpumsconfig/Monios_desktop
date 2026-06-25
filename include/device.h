#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "stdbool.h"
#include "stdint.h"

void device_init(void);
bool device_exists(const char *name);
bool device_list(char *buffer, uint32_t size);
int32_t device_read(const char *name, char *buffer, uint32_t size);
int32_t device_write(const char *name, const char *buffer, uint32_t size);

#endif
