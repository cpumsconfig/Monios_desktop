#include "common.h"
#include "cpu.h"
#include "kernel.h"

static cpu_info_t g_cpu_info;

bool cpu_cpuid_query(uint32_t leaf, uint32_t subleaf, cpuid_regs_t *regs)
{
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;

    asm volatile (
        "cpuid"
        : "=a" (a), "=b" (b), "=c" (c), "=d" (d)
        : "a" (leaf), "c" (subleaf)
    );

    if (regs != NULL) {
        regs->eax = a;
        regs->ebx = b;
        regs->ecx = c;
        regs->edx = d;
    }
    return true;
}

static void cpu_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    cpuid_regs_t regs;

    cpu_cpuid_query(leaf, subleaf, &regs);
    if (eax != NULL) *eax = regs.eax;
    if (ebx != NULL) *ebx = regs.ebx;
    if (ecx != NULL) *ecx = regs.ecx;
    if (edx != NULL) *edx = regs.edx;
}

static void cpu_copy_reg_string(uint32_t value, char *dst)
{
    dst[0] = (char) (value & 0xFF);
    dst[1] = (char) ((value >> 8) & 0xFF);
    dst[2] = (char) ((value >> 16) & 0xFF);
    dst[3] = (char) ((value >> 24) & 0xFF);
}

void cpu_enable_fpu_sse(void)
{
    uint64_t cr0;
    uint64_t cr4;

    asm volatile ("mov %%cr0, %0" : "=r" (cr0));
    cr0 &= ~(1ull << 2);  /* clear EM */
    cr0 |= (1ull << 1);   /* set MP */
    asm volatile ("mov %0, %%cr0" :: "r" (cr0) : "memory");

    asm volatile ("mov %%cr4, %0" : "=r" (cr4));
    cr4 |= (1ull << 9);   /* OSFXSR */
    cr4 |= (1ull << 10);  /* OSXMMEXCPT */
    asm volatile ("mov %0, %%cr4" :: "r" (cr4) : "memory");

    asm volatile ("fninit");
    g_cpu_info.fpu_enabled = true;
}

void cpu_detect(cpu_info_t *info)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_basic_leaf;
    uint32_t max_extended_leaf;

    if (info == NULL) {
        return;
    }

    memset(info, 0, sizeof(*info));

    cpu_cpuid(0, 0, &max_basic_leaf, &ebx, &ecx, &edx);
    info->max_basic_leaf = max_basic_leaf;
    cpu_copy_reg_string(ebx, &info->vendor[0]);
    cpu_copy_reg_string(edx, &info->vendor[4]);
    cpu_copy_reg_string(ecx, &info->vendor[8]);
    info->vendor[12] = '\0';

    if (max_basic_leaf >= 1) {
        cpu_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
        info->has_fpu = (edx & (1u << 0)) != 0;
        info->has_tsc = (edx & (1u << 4)) != 0;
        info->has_msr = (edx & (1u << 5)) != 0;
        info->has_apic = (edx & (1u << 9)) != 0;
        info->has_mmx = (edx & (1u << 23)) != 0;
        info->has_fxsr = (edx & (1u << 24)) != 0;
        info->has_sse = (edx & (1u << 25)) != 0;
        info->has_sse2 = (edx & (1u << 26)) != 0;
        info->has_sse3 = (ecx & (1u << 0)) != 0;
        info->has_ssse3 = (ecx & (1u << 9)) != 0;
        info->has_sse41 = (ecx & (1u << 19)) != 0;
        info->has_sse42 = (ecx & (1u << 20)) != 0;
        info->has_x2apic = (ecx & (1u << 21)) != 0;
        info->has_xsave = (ecx & (1u << 26)) != 0;
        info->has_avx = (ecx & (1u << 28)) != 0;
        /* 提取 family/model/stepping */
        info->stepping = eax & 0x0F;
        info->model = (eax >> 4) & 0x0F;
        info->family = (eax >> 8) & 0x0F;
        info->ext_model = (eax >> 16) & 0x0F;
        info->ext_family = (eax >> 20) & 0xFF;
    }

    cpu_cpuid(0x80000000u, 0, &max_extended_leaf, NULL, NULL, NULL);
    info->max_extended_leaf = max_extended_leaf;
    if (max_extended_leaf >= 0x80000001u) {
        cpu_cpuid(0x80000001u, 0, &eax, &ebx, &ecx, &edx);
        info->has_long_mode = (edx & (1u << 29)) != 0;
        /* AMD64 特有特性 */
        info->has_syscall = (edx & (1u << 11)) != 0;
        info->has_nx = (edx & (1u << 20)) != 0;
        info->has_mmxext = (edx & (1u << 22)) != 0;
        info->has_fxsr_opt = (edx & (1u << 25)) != 0;
        info->has_gbpages = (edx & (1u << 26)) != 0;
        info->has_rdtscp = (edx & (1u << 27)) != 0;
        info->has_3dnowext = (edx & (1u << 30)) != 0;
        info->has_3dnow = (edx & (1u << 31)) != 0;
        /* ECX 扩展特性 */
        info->has_lahf_lm = (ecx & (1u << 0)) != 0;
        info->has_cmp_legacy = (ecx & (1u << 1)) != 0;
        info->has_svm = (ecx & (1u << 2)) != 0;
        info->has_sse4a = (ecx & (1u << 6)) != 0;
        info->has_misalignsse = (ecx & (1u << 7)) != 0;
        info->has_3dnowprefetch = (ecx & (1u << 8)) != 0;
        info->has_osvw = (ecx & (1u << 12)) != 0;
        info->has_wdt = (ecx & (1u << 13)) != 0;
        info->has_tsc_scale = (ecx & (1u << 27)) != 0;
    }
    if (max_extended_leaf >= 0x80000004u) {
        uint32_t *brand_words = (uint32_t *) info->brand;
        for (uint32_t leaf = 0; leaf < 3; leaf++) {
            cpu_cpuid(0x80000002u + leaf, 0,
                      &brand_words[leaf * 4 + 0],
                      &brand_words[leaf * 4 + 1],
                      &brand_words[leaf * 4 + 2],
                      &brand_words[leaf * 4 + 3]);
        }
        info->brand[48] = '\0';
    }
}

const cpu_info_t *cpu_current_info(void)
{
    return &g_cpu_info;
}

bool cpu_fpu_enabled(void)
{
    return g_cpu_info.fpu_enabled;
}

uint64_t cpu_read_msr(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;

    asm volatile (
        "rdmsr"
        : "=a" (lo), "=d" (hi)
        : "c" (msr)
    );

    return ((uint64_t) hi << 32) | lo;
}

void cpu_write_msr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t) (value & 0xFFFFFFFF);
    uint32_t hi = (uint32_t) (value >> 32);

    asm volatile (
        "wrmsr"
        :
        : "a" (lo), "d" (hi), "c" (msr)
        : "memory"
    );
}

void cpu_log_info(void)
{
    char line[80] = "cpu: vendor=";
    char brand_line[80] = "cpu: brand=";

    cpu_detect(&g_cpu_info);
    strcpy(line + 12, g_cpu_info.vendor);
    log_write(line);

    if (g_cpu_info.brand[0] != '\0') {
        strcpy(brand_line + 11, g_cpu_info.brand);
        log_write(brand_line);
    }

    log_write_bool_event("cpu fpu", g_cpu_info.has_fpu);
    log_write_bool_event("cpu fpu enabled", g_cpu_info.fpu_enabled);
    log_write_bool_event("cpu fxsr", g_cpu_info.has_fxsr);
    log_write_bool_event("cpu mmx", g_cpu_info.has_mmx);
    log_write_bool_event("cpu sse", g_cpu_info.has_sse);
    log_write_bool_event("cpu sse2", g_cpu_info.has_sse2);
    log_write_bool_event("cpu sse3", g_cpu_info.has_sse3);
    log_write_bool_event("cpu ssse3", g_cpu_info.has_ssse3);
    log_write_bool_event("cpu sse4.1", g_cpu_info.has_sse41);
    log_write_bool_event("cpu sse4.2", g_cpu_info.has_sse42);
    log_write_bool_event("cpu xsave", g_cpu_info.has_xsave);
    log_write_bool_event("cpu avx", g_cpu_info.has_avx);
    log_write_bool_event("cpu apic", g_cpu_info.has_apic);
    log_write_bool_event("cpu x2apic", g_cpu_info.has_x2apic);
    log_write_bool_event("cpu tsc", g_cpu_info.has_tsc);
    log_write_bool_event("cpu msr", g_cpu_info.has_msr);
    log_write_bool_event("cpu longmode", g_cpu_info.has_long_mode);
}
