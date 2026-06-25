#include "common.h"
#include "cpu.h"
#include "kernel.h"
#include "smp.h"

static smp_info_t g_smp_info;

static void smp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    asm volatile ("cpuid"
                  : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
                  : "a" (leaf), "c" (subleaf));
}

void smp_init(void)
{
    const cpu_info_t *cpu = cpu_current_info();
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    memset(&g_smp_info, 0, sizeof(g_smp_info));
    g_smp_info.supported = cpu->has_apic;
    g_smp_info.bootstrap_only = true;
    g_smp_info.online_processors = 1;
    g_smp_info.logical_processors = 1;
    smp_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if ((edx & (1u << 28)) != 0) {
        g_smp_info.logical_processors = (ebx >> 16) & 0xFFu;
        if (g_smp_info.logical_processors == 0) {
            g_smp_info.logical_processors = 1;
        }
    }
    strcpy(g_smp_info.status, g_smp_info.supported ? "smp: apic detected, bsp online" : "smp: apic unavailable");
    log_write(g_smp_info.status);
}

const smp_info_t *smp_info(void)
{
    return &g_smp_info;
}
