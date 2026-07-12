#ifndef _APP_MEMORY_H_
#define _APP_MEMORY_H_

#include "stdbool.h"
#include "stdint.h"

bool app_memory_user_range(const void *ptr, uint64_t size);
bool app_memory_copy_from_user(void *dst, const void *src, uint64_t size);
bool app_memory_copy_to_user(void *dst, const void *src, uint64_t size);
bool app_memory_copy_string_from_user(char *dst, uint32_t dst_size, const char *src);
bool app_memory_string_readable(const char *src, uint32_t max_size);

#endif
