#include "common.h"
#include "gpu_nvidia.h"
#include "gpu_backend.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"

#define NVIDIA_VENDOR_ID 0x10DEU
#define PCI_CLASS_DISPLAY 0x03U
#define PCI_SUBCLASS_VGA 0x00U
#define PCI_SUBCLASS_3D 0x02U
#define PCI_COMMAND_MEMORY 0x0002U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define NVIDIA_MMIO_WINDOW 0x00200000ULL
#define NVIDIA_PROBE_INTERVAL 30U

static gpu_nvidia_info_t g_nvidia_info;
static bool g_nvidia_last_detected;
static uint16_t g_nvidia_last_device_id;
static bool g_nvidia_last_mmio_ready;
static bool g_nvidia_probe_valid;
static bool g_nvidia_last_framebuffer_ready;
static uint64_t g_nvidia_last_probe_tick;

static bool gpu_nvidia_identity_mapped(uint64_t address)
{
    return address < 0x40000000ULL ||
           (address >= 0xC0000000ULL && address < 0x200000000ULL);
}

static uint64_t gpu_nvidia_bar0(const pci_device_info_t *info)
{
    if (info == NULL) {
        return 0;
    }
    if (info->bar_address[0] != 0) {
        return info->bar_address[0];
    }
    return (uint64_t) (info->bar0 & 0xFFFFFFF0U);
}

static void gpu_nvidia_log_transition(void)
{
    if (g_nvidia_info.detected != g_nvidia_last_detected ||
        g_nvidia_info.device_id != g_nvidia_last_device_id ||
        g_nvidia_info.mmio_ready != g_nvidia_last_mmio_ready) {
        log_write(g_nvidia_info.status);
        g_nvidia_last_detected = g_nvidia_info.detected;
        g_nvidia_last_device_id = g_nvidia_info.device_id;
        g_nvidia_last_mmio_ready = g_nvidia_info.mmio_ready;
    }
}

static bool gpu_nvidia_callback(const pci_device_info_t *info, void *ctx)
{
    bool framebuffer_ready = ctx != NULL && *(const bool *) ctx;
    uint16_t command;
    uint64_t mmio_base;

    if (info == NULL ||
        info->vendor_id != NVIDIA_VENDOR_ID ||
        info->class_code != PCI_CLASS_DISPLAY ||
        (info->subclass != PCI_SUBCLASS_VGA && info->subclass != PCI_SUBCLASS_3D)) {
        return true;
    }

    memset(&g_nvidia_info, 0, sizeof(g_nvidia_info));
    g_nvidia_info.detected = true;
    g_nvidia_info.framebuffer_compatible = framebuffer_ready;
    g_nvidia_info.native_backend_ready = false;
    g_nvidia_info.vendor_id = info->vendor_id;
    g_nvidia_info.device_id = info->device_id;
    g_nvidia_info.bus = info->bus;
    g_nvidia_info.slot = info->slot;
    g_nvidia_info.func = info->func;
    g_nvidia_info.irq = info->interrupt_line;
    g_nvidia_info.power_management_capable = info->power_management_capable;
    g_nvidia_info.msi_capable = info->msi_capable;
    g_nvidia_info.msix_capable = info->msix_capable;
    g_nvidia_info.power_state = info->power_state;
    strcpy(g_nvidia_info.name, "NVIDIA display adapter");

    command = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    if ((command & (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) !=
        (PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER)) {
        command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
        pci_config_write16(info->bus, info->slot, info->func, 0x04, command);
    }
    g_nvidia_info.command = pci_config_read16(info->bus, info->slot, info->func, 0x04);
    g_nvidia_info.memory_enabled = (g_nvidia_info.command & PCI_COMMAND_MEMORY) != 0;
    g_nvidia_info.bus_master_enabled =
        (g_nvidia_info.command & PCI_COMMAND_BUS_MASTER) != 0;

    mmio_base = gpu_nvidia_bar0(info);
    g_nvidia_info.mmio_base = mmio_base;
    g_nvidia_info.mmio_ready = mmio_base != 0 &&
                               gpu_nvidia_identity_mapped(mmio_base);
    if (g_nvidia_info.mmio_ready && mmu_is_active()) {
        mmu_map_device_identity(mmio_base, NVIDIA_MMIO_WINDOW);
    }
    strcpy(g_nvidia_info.status,
           g_nvidia_info.framebuffer_compatible && g_nvidia_info.mmio_ready ?
           "nvidia: framebuffer adapter ready; native backend pending" :
           (g_nvidia_info.mmio_ready ?
            "nvidia: device mmio ready; framebuffer pending" :
            "nvidia: device detected; mmio mapping pending"));
    gpu_nvidia_log_transition();
    return false;
}

bool gpu_nvidia_probe(bool framebuffer_ready)
{
    uint64_t now_ticks = timer_ticks();

    if (g_nvidia_probe_valid &&
        g_nvidia_last_framebuffer_ready == framebuffer_ready &&
        now_ticks >= g_nvidia_last_probe_tick &&
        now_ticks - g_nvidia_last_probe_tick < NVIDIA_PROBE_INTERVAL) {
        return g_nvidia_info.detected;
    }
    memset(&g_nvidia_info, 0, sizeof(g_nvidia_info));
    strcpy(g_nvidia_info.status, "nvidia: not detected");
    pci_enumerate(gpu_nvidia_callback, &framebuffer_ready);
    g_nvidia_probe_valid = true;
    g_nvidia_last_framebuffer_ready = framebuffer_ready;
    g_nvidia_last_probe_tick = now_ticks;
    gpu_nvidia_log_transition();
    return g_nvidia_info.detected;
}

void gpu_nvidia_invalidate(void)
{
    g_nvidia_probe_valid = false;
}

bool gpu_nvidia_ready(bool framebuffer_ready, bool nvidia_detected)
{
    return framebuffer_ready &&
           nvidia_detected &&
           g_nvidia_info.mmio_ready &&
           g_nvidia_info.memory_enabled;
}

const gpu_nvidia_info_t *gpu_nvidia_info(void)
{
    return &g_nvidia_info;
}

const char *gpu_nvidia_status(void)
{
    return g_nvidia_info.status;
}
