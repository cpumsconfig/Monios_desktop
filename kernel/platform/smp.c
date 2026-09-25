#include "common.h"
#include "acpi.h"
#include "cpu.h"
#include "kernel.h"
#include "mmu.h"
#include "smp.h"
#include "spinlock.h"

static smp_info_t g_smp_info;

static void smp_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    asm volatile ("cpuid"
                  : "=a" (*eax), "=b" (*ebx), "=c" (*ecx), "=d" (*edx)
                  : "a" (leaf), "c" (subleaf));
}

static bool smp_lapic_mmio_prepare(uint32_t lapic_address)
{
    if (lapic_address == 0) {
        return false;
    }
    mmu_map_device_identity((uint64_t) lapic_address, 0x1000ULL);
    return true;
}

static void smp_copy_firmware_lapic_ids(const acpi_info_t *acpi)
{
    uint32_t count;

    if (acpi == NULL) {
        return;
    }
    count = acpi->madt_enabled_lapic_id_count;
    if (count > SMP_CPU_MAX) {
        count = SMP_CPU_MAX;
    }
    g_smp_info.firmware_enabled_lapic_id_count = count;
    memset(g_smp_info.firmware_enabled_lapic_ids, 0, sizeof(g_smp_info.firmware_enabled_lapic_ids));
    for (uint32_t i = 0; i < count; i++) {
        g_smp_info.firmware_enabled_lapic_ids[i] = acpi->madt_enabled_lapic_ids[i];
    }
}

static uint32_t smp_count_ap_targets(void)
{
    uint32_t targets = 0;

    if (g_smp_info.firmware_enabled_lapic_id_count == 0) {
        if (g_smp_info.firmware_enabled_processors > g_smp_info.online_processors) {
            return g_smp_info.firmware_enabled_processors - g_smp_info.online_processors;
        }
        return 0;
    }
    for (uint32_t i = 0; i < g_smp_info.firmware_enabled_lapic_id_count; i++) {
        if (g_smp_info.firmware_enabled_lapic_ids[i] != g_smp_info.bootstrap_lapic_id) {
            targets++;
        }
    }
    return targets;
}

static bool smp_targets_fit_xapic(void)
{
    for (uint32_t i = 0; i < g_smp_info.firmware_enabled_lapic_id_count; i++) {
        if (g_smp_info.firmware_enabled_lapic_ids[i] > 0xFFU) {
            return false;
        }
    }
    return true;
}

void smp_init(void)
{
    const cpu_info_t *cpu = cpu_current_info();
    const acpi_info_t *acpi = acpi_info();
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    memset(&g_smp_info, 0, sizeof(g_smp_info));
    g_smp_info.supported = cpu->has_apic;
    g_smp_info.bootstrap_only = true;
    g_smp_info.online_processors = 1;
    g_smp_info.logical_processors = 1;
    g_smp_info.firmware_processors = acpi->madt_processor_count;
    g_smp_info.firmware_enabled_processors = acpi->madt_enabled_processor_count;
    g_smp_info.lapic_address = acpi->madt_lapic_address;
    smp_copy_firmware_lapic_ids(acpi);
    smp_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    g_smp_info.bootstrap_lapic_id = (ebx >> 24) & 0xFFu;
    if ((edx & (1u << 28)) != 0) {
        g_smp_info.logical_processors = (ebx >> 16) & 0xFFu;
        if (g_smp_info.logical_processors == 0) {
            g_smp_info.logical_processors = 1;
        }
    }
    if (g_smp_info.firmware_enabled_processors > g_smp_info.logical_processors) {
        g_smp_info.logical_processors = g_smp_info.firmware_enabled_processors;
    }
    g_smp_info.lapic_mmio_ready = g_smp_info.supported &&
                                  smp_lapic_mmio_prepare(g_smp_info.lapic_address);
    g_smp_info.ap_startup_targets = smp_count_ap_targets();
    g_smp_info.ap_startup_pending = g_smp_info.ap_startup_targets;
    g_smp_info.ap_startup_supported = g_smp_info.lapic_mmio_ready &&
                                      acpi->madt_ready &&
                                      g_smp_info.ap_startup_targets > 0 &&
                                      smp_targets_fit_xapic();
    if (!g_smp_info.supported) {
        strcpy(g_smp_info.status, "smp: apic unavailable");
    } else if (!acpi->madt_ready) {
        strcpy(g_smp_info.status, "smp: apic detected, madt unavailable");
    } else if (g_smp_info.ap_startup_targets == 0) {
        strcpy(g_smp_info.status, "smp: single processor topology");
    } else if (!g_smp_info.lapic_mmio_ready) {
        strcpy(g_smp_info.status, "smp: lapic mmio unavailable, ap startup pending");
    } else if (!g_smp_info.ap_startup_supported) {
        strcpy(g_smp_info.status, "smp: x2apic startup pending");
    } else if (acpi->madt_ready) {
        strcpy(g_smp_info.status, "smp: apic ids ready, ap startup pending");
    } else {
        strcpy(g_smp_info.status, "smp: ap startup pending");
    }
    for (uint32_t i = 0; i < g_smp_info.firmware_enabled_lapic_id_count; i++) {
        kernel_log_hex_u32("smp: enabled apic id=", g_smp_info.firmware_enabled_lapic_ids[i]);
    }
    log_write(g_smp_info.status);

    /* Sanity-probe the spinlock primitives before APs are released: a lock
     * that cannot be acquired/released on the BSP would corrupt every shared
     * structure (heap, scheduler, sockets, mount table) once APs boot. */
    {
        static spinlock_t smp_probe_lock = SPINLOCK_INITIALIZER;
        uint64_t probe_flags = 0;
        spin_lock_irqsave(&smp_probe_lock, &probe_flags);
        spin_unlock_irqrestore(&smp_probe_lock, probe_flags);
    }
}

const smp_info_t *smp_info(void)
{
    return &g_smp_info;
}
