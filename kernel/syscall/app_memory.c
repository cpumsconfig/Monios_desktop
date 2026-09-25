#include "app_memory.h"
#include "common.h"
#include "exec.h"

bool app_memory_user_range(const void *ptr, uint64_t size)
{
    if (size == 0) {
        return true;
    }
    if (ptr == NULL) {
        return false;
    }
    if (!exec_active()) {
        return true;
    }
    return exec_user_range_valid(ptr, size);
}

bool app_memory_copy_from_user(void *dst, const void *src, uint64_t size)
{
    if (size == 0) {
        return true;
    }
    if (dst == NULL || !app_memory_user_range(src, size)) {
        return false;
    }
    memcpy(dst, src, size);
    return true;
}

bool app_memory_copy_to_user(void *dst, const void *src, uint64_t size)
{
    if (size == 0) {
        return true;
    }
    if (src == NULL || !app_memory_user_range(dst, size)) {
        return false;
    }
    memcpy(dst, src, size);
    return true;
}

bool app_memory_copy_string_from_user(char *dst, uint32_t dst_size, const char *src)
{
    uint64_t source_address;
    uint32_t index = 0;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return false;
    }
    source_address = (uint64_t) src;
    if (!app_memory_user_range(src, 1)) {
        return false;
    }
    while (index + 1 < dst_size) {
        if (source_address > 0xFFFFFFFFFFFFFFFFULL - index ||
            !app_memory_user_range((const void *) (source_address + index), 1)) {
            return false;
        }
        dst[index] = *((const char *) (source_address + index));
        if (dst[index] == '\0') {
            return true;
        }
        index++;
    }
    dst[index] = '\0';
    if (source_address > 0xFFFFFFFFFFFFFFFFULL - index ||
        !app_memory_user_range((const void *) (source_address + index), 1)) {
        return false;
    }
    return *((const char *) (source_address + index)) == '\0';
}

bool app_memory_string_readable(const char *src, uint32_t max_size)
{
    uint64_t source_address;

    if (src == NULL || max_size == 0) {
        return false;
    }
    source_address = (uint64_t) src;
    for (uint32_t index = 0; index < max_size; index++) {
        if (source_address > 0xFFFFFFFFFFFFFFFFULL - index ||
            !app_memory_user_range((const void *) (source_address + index), 1)) {
            return false;
        }
        if (*((const char *) (source_address + index)) == '\0') {
            return true;
        }
    }
    return false;
}
