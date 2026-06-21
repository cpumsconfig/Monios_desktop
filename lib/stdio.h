#ifndef _STDIO_H_
#define _STDIO_H_

#include "stdint.h"

int putchar(int ch);
int puts(const char *text);
int fputs(const char *text);
int fputs_handle(const char *text, uint64_t handle);
void print_uint(uint32_t value);
void print_int(int32_t value);

#endif
