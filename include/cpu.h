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
    /* AMD64 特有特性 */
    bool has_syscall;
    bool has_svm;
    bool has_nx;
    bool has_gbpages;
    bool has_rdtscp;
    bool has_3dnow;
    bool has_3dnowext;
    bool has_mmxext;
    bool has_fxsr_opt;
    bool has_lahf_lm;
    bool has_cmp_legacy;
    bool has_sse4a;
    bool has_misalignsse;
    bool has_3dnowprefetch;
    bool has_osvw;
    bool has_wdt;
    bool has_tsc_scale;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t ext_family;
    uint32_t ext_model;
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

/* MSR 操作 */
uint64_t cpu_read_msr(uint32_t msr);
void cpu_write_msr(uint32_t msr, uint64_t value);

/* MSR 寄存器地址 */
#define IA32_EFER_MSR         0xC0000080
#define IA32_STAR_MSR         0xC0000081
#define IA32_LSTAR_MSR        0xC0000082
#define IA32_CSTAR_MSR        0xC0000083
#define IA32_SFMASK_MSR       0xC0000084
#define IA32_FS_BASE_MSR      0xC0000100
#define IA32_GS_BASE_MSR      0xC0000101
#define IA32_KERNEL_GS_BASE_MSR 0xC0000102

/* EFER 寄存器位 */
#define EFER_SCE              (1 << 0)   /* SYSCALL 启用 */
#define EFER_LME              (1 << 8)   /* 长模式启用 */
#define EFER_LMA              (1 << 10)  /* 长模式激活 */
#define EFER_NXE              (1 << 11)  /* 不执行启用 */
#define EFER_SVME             (1 << 12)  /* SVM 启用 */

#endif
