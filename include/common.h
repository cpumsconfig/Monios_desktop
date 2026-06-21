#ifndef COMMON_H
#define COMMON_H

typedef unsigned long long uint64_t;
typedef signed long long   int64_t;
typedef unsigned int   uint32_t;
typedef          int   int32_t;
typedef unsigned short uint16_t;
typedef          short int16_t;
typedef unsigned char  uint8_t;
typedef          char  int8_t;
typedef unsigned long   uintptr_t;
typedef   signed long   intptr_t;

#include "stdbool.h"

void outb(uint16_t port, uint8_t value);
void outw(uint16_t port, uint16_t value);
void outl(uint16_t port, uint32_t value);
uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint16_t port);
void io_wait(void);

#define NULL ((void *) 0)

#include "string.h"

#endif
