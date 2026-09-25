#ifndef MONIOS_TINY_TOOLCHAIN_UTIL_H
#define MONIOS_TINY_TOOLCHAIN_UTIL_H

#include "appsys.h"
#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#define TOOLCHAIN_FILE_MAX 65536U
#define TOOLCHAIN_TEXT_MAX 65536U
#define TOOLCHAIN_PATH_MAX 256U
#define TOOLCHAIN_ASM_MAX 131072U

static bool tool_read_file(const char *path,
                           uint8_t *buffer,
                           uint32_t capacity,
                           uint32_t *size_out)
{
    int32_t size;

    if (path == 0 || buffer == 0 || size_out == 0 || capacity == 0) {
        return false;
    }
    size = app_file_size(path);
    if (size < 0 || (uint32_t) size >= capacity) {
        return false;
    }
    if (size > 0 && app_file_read(path, buffer, (uint32_t) size) != size) {
        return false;
    }
    buffer[size] = 0;
    *size_out = (uint32_t) size;
    return true;
}

static bool tool_write_file(const char *path, const void *buffer, uint32_t size)
{
    return path != 0 &&
           buffer != 0 &&
           app_file_write(path, buffer, size) == (int) size;
}

static const char *tool_find_literal(const char *text, const char *needle)
{
    uint32_t needle_length;

    if (text == 0 || needle == 0) {
        return 0;
    }
    needle_length = (uint32_t) strlen(needle);
    if (needle_length == 0) {
        return text;
    }
    while (*text != '\0') {
        if (strncmp(text, needle, needle_length) == 0) {
            return text;
        }
        text++;
    }
    return 0;
}

static const char *tool_skip_space(const char *text)
{
    while (text != 0 && (*text == ' ' || *text == '\t' ||
                         *text == '\r' || *text == '\n')) {
        text++;
    }
    return text;
}

static bool tool_parse_i32(const char *text, int32_t *value_out)
{
    int32_t sign = 1;
    int32_t value = 0;
    bool found = false;

    if (text == 0 || value_out == 0) {
        return false;
    }
    text = tool_skip_space(text);
    if (*text == '-') {
        sign = -1;
        text++;
    }
    while (*text >= '0' && *text <= '9') {
        found = true;
        value = value * 10 + (int32_t) (*text - '0');
        text++;
    }
    if (!found) {
        return false;
    }
    *value_out = value * sign;
    return true;
}

static bool tool_decode_quoted(const char *text,
                               char *output,
                               uint32_t output_size,
                               uint32_t *length_out)
{
    uint32_t length = 0;
    bool escaped = false;

    if (text == 0 || output == 0 || output_size == 0) {
        return false;
    }
    while (*text != '\0' && *text != '"') {
        text++;
    }
    if (*text != '"') {
        return false;
    }
    text++;
    while (*text != '\0') {
        char ch = *text++;

        if (escaped) {
            switch (ch) {
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            case '\\': ch = '\\'; break;
            case '"': ch = '"'; break;
            default:
                return false;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
            continue;
        } else if (ch == '"') {
            output[length] = '\0';
            if (length_out != 0) {
                *length_out = length;
            }
            return true;
        }
        if (length + 1 >= output_size) {
            return false;
        }
        output[length++] = ch;
    }
    return false;
}

static bool tool_append_char(char *buffer,
                             uint32_t capacity,
                             uint32_t *used,
                             char value)
{
    if (buffer == 0 || used == 0 || *used + 1 >= capacity) {
        return false;
    }
    buffer[(*used)++] = value;
    buffer[*used] = '\0';
    return true;
}

static bool tool_append_text(char *buffer,
                             uint32_t capacity,
                             uint32_t *used,
                             const char *text)
{
    uint32_t length;

    if (text == 0) {
        return false;
    }
    length = (uint32_t) strlen(text);
    if (*used + length >= capacity) {
        return false;
    }
    memcpy(buffer + *used, text, length);
    *used += length;
    buffer[*used] = '\0';
    return true;
}

static bool tool_append_u32(char *buffer,
                            uint32_t capacity,
                            uint32_t *used,
                            uint32_t value)
{
    char digits[12];
    uint32_t count = 0;

    if (value == 0) {
        return tool_append_char(buffer, capacity, used, '0');
    }
    while (value > 0 && count < sizeof(digits)) {
        digits[count++] = (char) ('0' + value % 10U);
        value /= 10U;
    }
    while (count > 0) {
        if (!tool_append_char(buffer, capacity, used, digits[--count])) {
            return false;
        }
    }
    return true;
}

static bool tool_append_i32(char *buffer,
                            uint32_t capacity,
                            uint32_t *used,
                            int32_t value)
{
    uint32_t magnitude;

    if (value < 0) {
        if (!tool_append_char(buffer, capacity, used, '-')) {
            return false;
        }
        magnitude = (uint32_t) (-(value + 1)) + 1U;
    } else {
        magnitude = (uint32_t) value;
    }
    return tool_append_u32(buffer, capacity, used, magnitude);
}

static bool tool_default_output(const char *input,
                                const char *suffix,
                                char *output,
                                uint32_t output_size)
{
    uint32_t length;
    uint32_t base_length;

    if (input == 0 || suffix == 0 || output == 0 || output_size == 0) {
        return false;
    }
    length = (uint32_t) strlen(input);
    base_length = length;
    while (base_length > 0 &&
           input[base_length - 1] != '\\' &&
           input[base_length - 1] != ':') {
        if (input[base_length - 1] == '.') {
            break;
        }
        base_length--;
    }
    if (base_length == 0 || input[base_length - 1] != '.') {
        base_length = length;
    } else {
        base_length--;
    }
    if (base_length + strlen(suffix) + 1 > output_size) {
        return false;
    }
    memcpy(output, input, base_length);
    output[base_length] = '\0';
    strcpy(output + base_length, suffix);
    return true;
}

static void tool_usage(const char *name, const char *usage)
{
    fputs(name);
    fputs(": ");
    fputs(usage);
    fputs("\r\n");
}

#endif
