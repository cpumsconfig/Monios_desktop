#ifndef _STDIO_H_
#define _STDIO_H_

#include "stdint.h"

typedef __builtin_va_list va_list;

int putchar(int ch);
int puts(const char *text);
int fputs(const char *text);
int fputs_handle(const char *text, uint64_t handle);
int printf(const char *format, ...);
int vprintf(const char *format, va_list args);
int vsprintf(char *buf, const char *format, va_list args);
int sprintf(char *buf, const char *format, ...);
void print_uint(uint32_t value);
void print_int(int32_t value);

#endif
