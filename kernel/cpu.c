#include "common.h"
#include "cpu.h"
#include "kernel.h"

static cpu_info_t g_cpu_info;

static void cpu_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
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

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
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
        info->has_sse = (edx & (1u << 25)) != 0;
        info->has_sse2 = (edx & (1u << 26)) != 0;
        info->has_sse3 = (ecx & (1u << 0)) != 0;
        info->has_x2apic = (ecx & (1u << 21)) != 0;
    }

    cpu_cpuid(0x80000000u, 0, &max_extended_leaf, NULL, NULL, NULL);
    if (max_extended_leaf >= 0x80000001u) {
        cpu_cpuid(0x80000001u, 0, &eax, &ebx, &ecx, &edx);
        info->has_long_mode = (edx & (1u << 29)) != 0;
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
    log_write_bool_event("cpu mmx", g_cpu_info.has_mmx);
    log_write_bool_event("cpu sse", g_cpu_info.has_sse);
    log_write_bool_event("cpu sse2", g_cpu_info.has_sse2);
    log_write_bool_event("cpu sse3", g_cpu_info.has_sse3);
    log_write_bool_event("cpu apic", g_cpu_info.has_apic);
    log_write_bool_event("cpu x2apic", g_cpu_info.has_x2apic);
    log_write_bool_event("cpu tsc", g_cpu_info.has_tsc);
    log_write_bool_event("cpu msr", g_cpu_info.has_msr);
    log_write_bool_event("cpu longmode", g_cpu_info.has_long_mode);
}
