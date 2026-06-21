#include "common.h"

void *memset(void *dst_, uint8_t value, uint64_t size)
{
    uint8_t *dst = (uint8_t *) dst_;
    while (size-- > 0) {
        *dst++ = value;
    }
    return dst_;
}

void *memcpy(void *dst_, const void *src_, uint64_t size)
{
    uint8_t *dst = (uint8_t *) dst_;
    const uint8_t *src = (const uint8_t *) src_;
    while (size-- > 0) {
        *dst++ = *src++;
    }
    return dst_;
}

int memcmp(const void *a_, const void *b_, uint64_t size)
{
    const uint8_t *a = (const uint8_t *) a_;
    const uint8_t *b = (const uint8_t *) b_;
    while (size-- > 0) {
        if (*a != *b) {
            return *a > *b ? 1 : -1;
        }
        a++;
        b++;
    }
    return 0;
}

char *strcpy(char *dst_, const char *src_)
{
    char *ret = dst_;
    while ((*dst_++ = *src_++) != '\0') {
    }
    return ret;
}

char *strcat(char *dst_, const char *src_)
{
    char *ret = dst_;
    while (*dst_ != '\0') dst_++;
    while ((*dst_++ = *src_++) != '\0') {
    }
    return ret;
}

uint64_t strlen(const char *str)
{
    const char *p = str;
    while (*p != '\0') {
        p++;
    }
    return (uint64_t) (p - str);
}

int8_t strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a < *b ? -1 : (*a > *b ? 1 : 0);
}

int8_t strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        if (ca >= 'A' && ca <= 'Z') ca = (char) (ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char) (cb - 'A' + 'a');
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a == '\0' && *b == '\0') {
        return 0;
    }
    return *a == '\0' ? -1 : 1;
}

char *strchr(const char *str, const uint8_t ch)
{
    while (*str) {
        if ((uint8_t) *str == ch) {
            return (char *) str;
        }
        str++;
    }
    return NULL;
}
