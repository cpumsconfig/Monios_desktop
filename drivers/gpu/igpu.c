#include "common.h"
#include "gpu_igpu.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"

#define PCI_COMMAND_MEMORY 0x0002U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define GPU_IGPU_MMIO_WINDOW 0x00200000ULL
#define GPU_IGPU_PROBE_INTERVAL 30U

static gpu_igpu_info_t g_igpu_info;
static bool g_igpu_last_detected;
static uint16_t g_igpu_last_device_id;
static bool g_igpu_last_mmio_ready;
static bool g_igpu_probe_valid;
static bool g_igpu_last_framebuffer_ready;
static uint64_t g_igpu_last_probe_tick;

static bool gpu_igpu_is_supported_vendor(uint16_t vendor_id)
{
    return vendor_id == 0x8086U || vendor_id == 0x1002U;
}

static gpu_igpu_vendor_t gpu_igpu_vendor_from_id(uint16_t vendor_id)
{
    if (vendor_id == 0x8086U) {
        return GPU_IGPU_VENDOR_INTEL;
    }
    if (vendor_id == 0x1002U) {
        return GPU_IGPU_VENDOR_AMD;
    }
    return GPU_IGPU_VENDOR_NONE;
}

static bool gpu_igpu_identity_mapped(uint64_t address)
{
    return address < 0x40000000ULL ||
           (address >= 0xC0000000ULL && address < 0x200000000ULL);
}

static uint64_t gpu_igpu_bar0(const pci_device_info_t *info)
{
    if (info == NULL) {
        return 0;
    }
    if (info->bar_address[0] != 0) {
        return info->bar_address[0];
    }
    return (uint64_t) (info->bar0 & 0xFFFFFFF0U);
}

static void gpu_igpu_log_transition(void)
{
    if (g_igpu_info.detected != g_igpu_last_detected ||
        g_igpu_info.device_id != g_igpu_last_device_id ||
        g_igpu_info.mmio_ready != g_igpu_last_mmio_ready) {
        log_write(g_igpu_info.status);
        g_igpu_last_detected = g_igpu_info.detected;
        g_igpu_last_device_id = g_igpu_info.device_id;
        g_igpu_last_mmio_ready = g_igpu_info.mmio_ready;
    }
}

static bool gpu_igpu_callback(const pci_device_info_t *info, void *ctx)
{
    bool framebuffer_ready = ctx != NULL && *(const bool *) ctx;
    uint16_t command;
    uint64_t mmio_base;

    if (info == NULL ||
        info->class_code != 0x03U ||
        (info->subclass != 0x00U && info->subclass != 0x02U) ||
        !gpu_igpu_is_supported_vendor(info->vendor_id)) {
        return true;
    }

    memset(&g_igpu_info, 0, sizeof(g_igpu_info));
    g_igpu_info.detected = true;
    g_igpu_info.framebuffer_compatible = framebuffer_ready;
    g_igpu_info.native_backend_ready = false;
    g_igpu_info.vendor = gpu_igpu_vendor_from_id(info->vendor_id);
    g_igpu_info.vendor_id = info->vendor_id;
    g_igpu_info.device_id = info->device_id;
    g_igpu_info.bus = info->bus;
    g_igpu_info.slot = info->slot;
    g_igpu_info.func = info->func;
    g_igpu_info.irq = info->interrupt_line;
    g_igpu_info.power_management_capable = info->power_management_capable;
    g_igpu_info.msi_capable = info->msi_capable;
    g_igpu_info.msix_capable = info->msix_capable;
    g_igpu_info.power_state = info->power_state;
    command = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
        pci_config_write16(info->bus, info->slot, info->func, 0x04, command);
    }
    g_igpu_info.command = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    g_igpu_info.memory_enabled = (g_igpu_info.command & PCI_COMMAND_MEMORY) != 0;
    g_igpu_info.bus_master_enabled =
        (g_igpu_info.command & PCI_COMMAND_BUS_MASTER) != 0;
    mmio_base = gpu_igpu_bar0(info);
    g_igpu_info.mmio_base = mmio_base;
    g_igpu_info.mmio_ready = mmio_base != 0 &&
                             gpu_igpu_identity_mapped(mmio_base);
    if (g_igpu_info.mmio_ready && mmu_is_active()) {
        mmu_map_device_identity(mmio_base, GPU_IGPU_MMIO_WINDOW);
    }
    strcpy(g_igpu_info.name,
           g_igpu_info.vendor == GPU_IGPU_VENDOR_INTEL ?
           "Intel integrated graphics" : "AMD integrated graphics");
    strcpy(g_igpu_info.status,
           framebuffer_ready && g_igpu_info.mmio_ready && g_igpu_info.memory_enabled ?
           "igpu: framebuffer/mmio ready; native backend pending" :
           (g_igpu_info.mmio_ready ?
            "igpu: device mmio ready; framebuffer pending" :
            "igpu: device detected; mmio mapping pending"));
    gpu_igpu_log_transition();
    return false;
}

bool gpu_igpu_probe(bool framebuffer_ready)
{
    uint64_t now_ticks = timer_ticks();

    if (g_igpu_probe_valid &&
        g_igpu_last_framebuffer_ready == framebuffer_ready &&
        now_ticks >= g_igpu_last_probe_tick &&
        now_ticks - g_igpu_last_probe_tick < GPU_IGPU_PROBE_INTERVAL) {
        return g_igpu_info.detected;
    }
    memset(&g_igpu_info, 0, sizeof(g_igpu_info));
    g_igpu_info.vendor = GPU_IGPU_VENDOR_NONE;
    strcpy(g_igpu_info.status, "igpu: not detected");
    pci_enumerate(gpu_igpu_callback, &framebuffer_ready);
    g_igpu_probe_valid = true;
    g_igpu_last_framebuffer_ready = framebuffer_ready;
    g_igpu_last_probe_tick = now_ticks;
    gpu_igpu_log_transition();
    return g_igpu_info.detected;
}

void gpu_igpu_invalidate(void)
{
    g_igpu_probe_valid = false;
}

const gpu_igpu_info_t *gpu_igpu_info(void)
{
    return &g_igpu_info;
}

const char *gpu_igpu_status(void)
{
    return g_igpu_info.status;
}
