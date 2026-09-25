#include "common.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"
#include "virtio.h"

#define VIRTIO_PCI_VENDOR 0x1AF4U
#define VIRTIO_LEGACY_DEVICE_FIRST 0x1000U
#define VIRTIO_LEGACY_DEVICE_LAST 0x103FU
#define VIRTIO_MODERN_DEVICE_FIRST 0x1040U
#define VIRTIO_MODERN_DEVICE_LAST 0x107FU
#define PCI_COMMAND_OFFSET 0x04U
#define PCI_COMMAND_IO 0x0001U
#define PCI_COMMAND_MEMORY 0x0002U
#define PCI_COMMAND_BUS_MASTER 0x0004U
#define PCI_CAPABILITY_VENDOR 0x09U
#define VIRTIO_LEGACY_STATUS_OFFSET 0x12U
#define VIRTIO_MMIO_WINDOW 0x00100000ULL

static virtio_info_t g_virtio_info;

static bool virtio_identity_mapped(uint64_t address)
{
    return address < 0x40000000ULL ||
           (address >= 0xC0000000ULL && address < 0x200000000ULL);
}

static virtio_device_kind_t virtio_kind_from_id(uint16_t device_id)
{
    switch (device_id) {
    case 0x1000:
    case 0x1041:
        return VIRTIO_DEVICE_NETWORK;
    case 0x1001:
    case 0x1042:
        return VIRTIO_DEVICE_BLOCK;
    case 0x1002:
    case 0x1045:
        return VIRTIO_DEVICE_BALLOON;
    case 0x1003:
    case 0x1043:
        return VIRTIO_DEVICE_CONSOLE;
    case 0x1004:
    case 0x1048:
        return VIRTIO_DEVICE_SCSI;
    case 0x1005:
    case 0x1044:
        return VIRTIO_DEVICE_RNG;
    case 0x1050:
        return VIRTIO_DEVICE_GPU;
    case 0x1052:
        return VIRTIO_DEVICE_INPUT;
    case 0x1055:
        return VIRTIO_DEVICE_SOUND;
    default:
        return VIRTIO_DEVICE_OTHER;
    }
}

static __attribute__((unused)) const char *virtio_kind_name(virtio_device_kind_t kind)
{
    switch (kind) {
    case VIRTIO_DEVICE_NETWORK:
        return "network";
    case VIRTIO_DEVICE_BLOCK:
        return "block";
    case VIRTIO_DEVICE_CONSOLE:
        return "console";
    case VIRTIO_DEVICE_BALLOON:
        return "balloon";
    case VIRTIO_DEVICE_SCSI:
        return "scsi";
    case VIRTIO_DEVICE_RNG:
        return "rng";
    case VIRTIO_DEVICE_GPU:
        return "gpu";
    case VIRTIO_DEVICE_INPUT:
        return "input";
    case VIRTIO_DEVICE_SOUND:
        return "sound";
    default:
        return "other";
    }
}

static void virtio_enable_pci(const pci_device_info_t *info,
                              bool io_bar,
                              bool mmio_bar)
{
    uint16_t command;

    if (info == NULL) {
        return;
    }
    command = pci_config_read16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET);
    if (io_bar) {
        command |= PCI_COMMAND_IO;
    }
    if (mmio_bar) {
        command |= PCI_COMMAND_MEMORY;
    }
    command |= PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET, command);
}

static bool virtio_callback(const pci_device_info_t *info, void *ctx)
{
    bool io_bar;
    bool mmio_bar;
    uint64_t mmio_base;
    uint16_t io_base;
    uint16_t command;
    (void) ctx;

    if (info == NULL ||
        info->vendor_id != VIRTIO_PCI_VENDOR ||
        (info->device_id < VIRTIO_LEGACY_DEVICE_FIRST ||
         info->device_id > VIRTIO_MODERN_DEVICE_LAST)) {
        return true;
    }

    g_virtio_info.device_count++;
    if (g_virtio_info.present) {
        return true;
    }

    memset(&g_virtio_info, 0, sizeof(g_virtio_info));
    g_virtio_info.present = true;
    g_virtio_info.device_count = 1;
    g_virtio_info.legacy = info->device_id <= VIRTIO_LEGACY_DEVICE_LAST;
    g_virtio_info.modern = info->device_id >= VIRTIO_MODERN_DEVICE_FIRST;
    g_virtio_info.vendor_id = info->vendor_id;
    g_virtio_info.device_id = info->device_id;
    g_virtio_info.bus = info->bus;
    g_virtio_info.slot = info->slot;
    g_virtio_info.func = info->func;
    g_virtio_info.irq = info->interrupt_line;
    g_virtio_info.kind = virtio_kind_from_id(info->device_id);
    strcpy(g_virtio_info.name, "VirtIO PCI device");

    io_bar = (info->bar0 & 1U) != 0;
    mmio_base = info->bar_address[0];
    if (mmio_base == 0 && !io_bar) {
        mmio_base = (uint64_t) (info->bar0 & 0xFFFFFFF0U);
    }
    io_base = io_bar ? (uint16_t) (info->bar0 & 0xFFFFFFFCU) : 0;
    g_virtio_info.io_base = io_base;
    g_virtio_info.mmio_base = mmio_base;
    g_virtio_info.io_ready = io_base >= 0x100U;
    g_virtio_info.mmio_ready = !io_bar && mmio_base != 0 &&
                                virtio_identity_mapped(mmio_base);
    mmio_bar = g_virtio_info.mmio_ready;
    virtio_enable_pci(info, g_virtio_info.io_ready, mmio_bar);
    command = pci_config_read16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET);
    g_virtio_info.command = command;
    g_virtio_info.bus_master_enabled = (command & PCI_COMMAND_BUS_MASTER) != 0;
    g_virtio_info.vendor_capability =
        pci_find_capability(info->bus, info->slot, info->func,
                            PCI_CAPABILITY_VENDOR) != 0;
    if (g_virtio_info.legacy && g_virtio_info.io_ready) {
        g_virtio_info.legacy_status =
            inb((uint16_t) (g_virtio_info.io_base + VIRTIO_LEGACY_STATUS_OFFSET));
    }
    if (g_virtio_info.mmio_ready && mmu_is_active()) {
        mmu_map_device_identity(g_virtio_info.mmio_base, VIRTIO_MMIO_WINDOW);
    }
    if (!g_virtio_info.io_ready && !g_virtio_info.mmio_ready) {
        if (g_virtio_info.modern && g_virtio_info.vendor_capability) {
            strcpy(g_virtio_info.status, "virtio: modern PCI capability ready");
        } else {
            strcpy(g_virtio_info.status, "virtio: PCI resource unavailable");
        }
    } else if (!g_virtio_info.bus_master_enabled) {
        strcpy(g_virtio_info.status, "virtio: bus master unavailable");
    } else {
        strcpy(g_virtio_info.status, "virtio: PCI resources ready");
    }
    log_write(g_virtio_info.status);
    return true;
}

bool virtio_driver_init(void)
{
    memset(&g_virtio_info, 0, sizeof(g_virtio_info));
    strcpy(g_virtio_info.status, "virtio: no PCI device detected");
    pci_enumerate(virtio_callback, NULL);
    if (!g_virtio_info.present) {
        log_write(g_virtio_info.status);
        return false;
    }
    return true;
}

void virtio_shutdown(void)
{
    if (!g_virtio_info.present) {
        return;
    }
    strcpy(g_virtio_info.status, "virtio: shutdown");
    log_write(g_virtio_info.status);
}

const virtio_info_t *virtio_info(void)
{
    return &g_virtio_info;
}

const char *virtio_status(void)
{
    return g_virtio_info.status;
}
