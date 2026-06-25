#include "stdio.h"
#include "string.h"
#include "unistd.h"

int putchar(int ch)
{
    char out = (char) ch;

    return write(STDOUT_FILENO, &out, 1) == 1 ? ch : -1;
}

int fputs_handle(const char *text, uint64_t handle)
{
    if (text == 0) {
        return -1;
    }
    return write(handle, text, (uint32_t) strlen(text));
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
