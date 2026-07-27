#include "stdio.h"
#include "appsys.h"
#include "console_dll.h"
#include "string.h"

static int stdio_write_buffer(const void *buffer, uint32_t size)
{
    int32_t written;

    if (buffer == 0 && size != 0) {
        return -1;
    }
    written = console_write_buffer(buffer, size);
    return written == (int32_t) size ? (int) written : -1;
}

static int stdio_write_text(const char *text)
{
    if (text == 0) {
        return -1;
    }
    return stdio_write_buffer(text, (uint32_t) strlen(text));
}

static int stdio_write_unsigned(uint64_t value, uint32_t base, bool uppercase)
{
    char digits[20];
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    uint32_t count = 0;
    int written = 0;

    if (base < 2 || base > 16) {
        return -1;
    }
    if (value == 0) {
        return stdio_write_buffer("0", 1);
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = alphabet[value % base];
        value /= base;
    }
    while (count > 0) {
        int result = stdio_write_buffer(&digits[--count], 1);

        if (result < 0) {
            return -1;
        }
        written += result;
    }
    return written;
}

static int stdio_write_signed(int64_t value)
{
    uint64_t magnitude;
    int written = 0;

    if (value < 0) {
        if (stdio_write_buffer("-", 1) < 0) {
            return -1;
        }
        written++;
        magnitude = (uint64_t) (-(value + 1)) + 1U;
    } else {
        magnitude = (uint64_t) value;
    }
    {
        int digits = stdio_write_unsigned(magnitude, 10, false);

        if (digits < 0) {
            return -1;
        }
        written += digits;
    }
    return written;
}

int putchar(int ch)
{
    char out = (char) ch;

    return stdio_write_buffer(&out, 1) == 1 ? ch : -1;
}

int fputs_handle(const char *text, uint64_t handle)
{
    if (text == 0) {
        return -1;
    }
    return console_write_handle(handle, text, (uint32_t) strlen(text));
}

int fputs(const char *text)
{
    return fputs_handle(text, STDOUT_FILENO);
}

int puts(const char *text)
{
    int written = fputs(text);

    if (written < 0 || fputs("\r\n") < 0) {
        return -1;
    }
    return written + 2;
}

int vprintf(const char *format, va_list args)
{
    const char *cursor = format;
    int written = 0;

    if (format == 0) {
        return -1;
    }
    while (*cursor != '\0') {
        if (*cursor != '%') {
            const char *start = cursor;

            while (*cursor != '\0' && *cursor != '%') {
                cursor++;
            }
            if (stdio_write_buffer(start, (uint32_t) (cursor - start)) < 0) {
                return -1;
            }
            written += (int) (cursor - start);
            continue;
        }

        cursor++;
        if (*cursor == '%') {
            if (stdio_write_buffer("%", 1) < 0) {
                return -1;
            }
            written++;
            cursor++;
            continue;
        }

        {
            uint32_t length = 0;
            char specifier;
            int result;

            if (*cursor == 'l') {
                length = 1;
                cursor++;
                if (*cursor == 'l') {
                    length = 2;
                    cursor++;
                }
            }
            specifier = *cursor;
            if (specifier == '\0') {
                return -1;
            }
            cursor++;
            switch (specifier) {
            case 'c': {
                char value = (char) __builtin_va_arg(args, int);

                result = stdio_write_buffer(&value, 1);
                break;
            }
            case 's': {
                const char *value = __builtin_va_arg(args, const char *);

                result = stdio_write_text(value == 0 ? "(null)" : value);
                break;
            }
            case 'd':
            case 'i':
                if (length == 2) {
                    result = stdio_write_signed(__builtin_va_arg(args, long long));
                } else if (length == 1) {
                    result = stdio_write_signed(__builtin_va_arg(args, long));
                } else {
                    result = stdio_write_signed(__builtin_va_arg(args, int));
                }
                break;
            case 'u':
                if (length == 2) {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned long long), 10, false);
                } else if (length == 1) {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned long), 10, false);
                } else {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned int), 10, false);
                }
                break;
            case 'x':
            case 'X':
                if (length == 2) {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned long long),
                                                  16,
                                                  specifier == 'X');
                } else if (length == 1) {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned long),
                                                  16,
                                                  specifier == 'X');
                } else {
                    result = stdio_write_unsigned(__builtin_va_arg(args, unsigned int),
                                                  16,
                                                  specifier == 'X');
                }
                break;
            default:
                return -1;
            }
            if (result < 0) {
                return -1;
            }
            written += result;
        }
    }
    return written;
}

int printf(const char *format, ...)
{
    va_list args;
    int result;

    __builtin_va_start(args, format);
    result = vprintf(format, args);
    __builtin_va_end(args);
    return result;
}

void print_uint(uint32_t value)
{
    char temp[10];
    uint32_t i = 0;

    if (value == 0) {
        putchar('0');
        return;
    }
    while (value > 0 && i < sizeof(temp)) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        putchar(temp[--i]);
    }
}

void print_int(int32_t value)
{
    if (value < 0) {
        putchar('-');
        print_uint((uint32_t) -value);
        return;
    }
    print_uint((uint32_t) value);
}
