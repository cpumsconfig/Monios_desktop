#ifndef _CPU_H_
#define _CPU_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;
    char vendor[13];
    char brand[49];
    bool has_fpu;
    bool fpu_enabled;
    bool has_fxsr;
    bool has_mmx;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_ssse3;
    bool has_sse41;
    bool has_sse42;
    bool has_xsave;
    bool has_avx;
    bool has_apic;
    bool has_x2apic;
    bool has_tsc;
    bool has_msr;
    bool has_long_mode;
} cpu_info_t;

typedef struct {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
} cpuid_regs_t;

bool cpu_cpuid_query(uint32_t leaf, uint32_t subleaf, cpuid_regs_t *regs);
void cpu_detect(cpu_info_t *info);
void cpu_enable_fpu_sse(void);
bool cpu_fpu_enabled(void);
const cpu_info_t *cpu_current_info(void);
void cpu_log_info(void);

#endif
