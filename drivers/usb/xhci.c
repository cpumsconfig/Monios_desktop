#include "common.h"
#include "kernel.h"
#include "mmu.h"
#include "pci.h"
#include "xhci.h"

#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_SUBCLASS_USB        0x03
#define PCI_PROGIF_XHCI         0x30
#define PCI_COMMAND_OFFSET      0x04
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004

static xhci_info_t g_xhci_info;

static uint32_t xhci_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static uint32_t xhci_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_xhci_info.mmio_base + offset);

    return *ptr;
}

static uint8_t xhci_read8(uint32_t offset)
{
    volatile uint8_t *ptr = (volatile uint8_t *) (uint64_t) (g_xhci_info.mmio_base + offset);

    return *ptr;
}

static bool xhci_find_callback(const pci_device_info_t *info, void *ctx)
{
    pci_device_info_t *out = (pci_device_info_t *) ctx;

    if (info->class_code == PCI_CLASS_SERIAL_BUS &&
        info->subclass == PCI_SUBCLASS_USB &&
        info->prog_if == PCI_PROGIF_XHCI) {
        *out = *info;
        return false;
    }
    return true;
}

bool xhci_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;

    memset(&g_xhci_info, 0, sizeof(g_xhci_info));
    memset(&info, 0, sizeof(info));
    strcpy(g_xhci_info.status, "xhci: not found");

    pci_enumerate(xhci_find_callback, &info);
    if (info.vendor_id == 0) {
        log_write(g_xhci_info.status);
        return false;
    }

    g_xhci_info.present = true;
    g_xhci_info.vendor_id = info.vendor_id;
    g_xhci_info.device_id = info.device_id;
    g_xhci_info.bus = info.bus;
    g_xhci_info.slot = info.slot;
    g_xhci_info.func = info.func;
    g_xhci_info.irq = info.interrupt_line;
    g_xhci_info.mmio_base = xhci_bar_base(info.bar0);
    if (g_xhci_info.mmio_base == 0) {
        strcpy(g_xhci_info.status, "xhci: mmio bar missing");
        log_write(g_xhci_info.status);
        return true;
    }

    mmu_map_identity(g_xhci_info.mmio_base, 0x10000);
    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_xhci_info.cap_length = xhci_read8(0x00);
    g_xhci_info.hci_version = (uint16_t) (xhci_read32(0x00) >> 16);
    g_xhci_info.hcsparams1 = xhci_read32(0x04);
    g_xhci_info.hccparams1 = xhci_read32(0x10);
    g_xhci_info.max_slots = (uint8_t) (g_xhci_info.hcsparams1 & 0xFFu);
    g_xhci_info.max_ports = (uint8_t) ((g_xhci_info.hcsparams1 >> 24) & 0xFFu);
    g_xhci_info.mmio_ready = true;
    strcpy(g_xhci_info.status, "xhci: controller detected");
    log_write(g_xhci_info.status);
    return true;
}

void xhci_shutdown(void)
{
    if (g_xhci_info.present) {
        strcpy(g_xhci_info.status, "xhci: shutdown");
        log_write(g_xhci_info.status);
    }
}

const xhci_info_t *xhci_info(void)
{
    return &g_xhci_info;
}

const char *xhci_status(void)
{
    return g_xhci_info.status;
}
