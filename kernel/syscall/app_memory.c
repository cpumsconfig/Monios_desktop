#include "app_memory.h"
#include "common.h"
#include "exec.h"

bool app_memory_user_range(const void *ptr, uint64_t size)
{
    const exec_launch_info_t *info;

    if (size == 0) {
        return true;
    }
    if (ptr == NULL) {
        return false;
    }
    if (!exec_active()) {
        return true;
    }
    info = exec_current_launch_info();
    if (info != NULL && info->privilege_level == EXEC_PRIV_R0) {
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
    uint32_t index = 0;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return false;
    }
    if (!app_memory_user_range(src, 1)) {
        return false;
    }
    while (index + 1 < dst_size) {
        if (!app_memory_user_range(src + index, 1)) {
            return false;
        }
        dst[index] = src[index];
        if (dst[index] == '\0') {
            return true;
        }
        index++;
    }
    dst[index] = '\0';
    if (!app_memory_user_range(src + index, 1)) {
        return false;
    }
    return src[index] == '\0';
}

bool app_memory_string_readable(const char *src, uint32_t max_size)
{
    if (src == NULL || max_size == 0) {
        return false;
    }
    for (uint32_t index = 0; index < max_size; index++) {
        if (!app_memory_user_range(src + index, 1)) {
            return false;
        }
        if (src[index] == '\0') {
            return true;
        }
    }
    return false;
}
