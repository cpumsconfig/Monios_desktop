#ifndef _BSOD_H_
#define _BSOD_H_

#include "stdint.h"

typedef struct {
    uint8_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} bsod_exception_info_t;

void bsod_panic(const char *title, const char *detail);
void bsod_unhandled_interrupt(void);
void bsod_exception_panic(const bsod_exception_info_t *info);
void bsod_out_of_memory(uint64_t size);

#endif
