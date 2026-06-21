#ifndef _CPU_H_
#define _CPU_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    char vendor[13];
    char brand[49];
    bool has_fpu;
    bool has_mmx;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_apic;
    bool has_x2apic;
    bool has_tsc;
    bool has_msr;
    bool has_long_mode;
} cpu_info_t;

void cpu_detect(cpu_info_t *info);
void cpu_enable_fpu_sse(void);
const cpu_info_t *cpu_current_info(void);
void cpu_log_info(void);

#endif
