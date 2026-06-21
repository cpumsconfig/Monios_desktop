#include "ahci.h"
#include "common.h"
#include "kernel.h"
#include "pci.h"

#define PCI_CLASS_STORAGE        0x01
#define PCI_SUBCLASS_SATA        0x06
#define PCI_COMMAND_OFFSET       0x04
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUS_MASTER   0x0004

#define AHCI_REG_CAP             0x00
#define AHCI_REG_PI              0x0C

static ahci_info_t g_ahci_info;

static uint32_t ahci_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static uint32_t ahci_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_ahci_info.abar + offset);

    return *ptr;
}

bool ahci_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;

    memset(&g_ahci_info, 0, sizeof(g_ahci_info));
    strcpy(g_ahci_info.status, "ahci: not found");

    if (!pci_find_first(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, &info)) {
        log_write(g_ahci_info.status);
        return false;
    }

    g_ahci_info.present = true;
    g_ahci_info.vendor_id = info.vendor_id;
    g_ahci_info.device_id = info.device_id;
    g_ahci_info.bus = info.bus;
    g_ahci_info.slot = info.slot;
    g_ahci_info.func = info.func;
    g_ahci_info.prog_if = info.prog_if;
    g_ahci_info.irq = info.interrupt_line;
    g_ahci_info.abar = ahci_bar_base(info.bar5);
    if (g_ahci_info.abar == 0) {
        strcpy(g_ahci_info.status, "ahci: abar missing");
        log_write(g_ahci_info.status);
        return true;
    }

    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_ahci_info.cap = ahci_read32(AHCI_REG_CAP);
    g_ahci_info.ports_implemented = ahci_read32(AHCI_REG_PI);
    g_ahci_info.mmio_ready = true;
    strcpy(g_ahci_info.status, "ahci: controller detected");
    log_write(g_ahci_info.status);
    return true;
}

void ahci_shutdown(void)
{
    if (g_ahci_info.present) {
        strcpy(g_ahci_info.status, "ahci: shutdown");
        log_write(g_ahci_info.status);
    }
}

const ahci_info_t *ahci_info(void)
{
    return &g_ahci_info;
}

const char *ahci_status(void)
{
    return g_ahci_info.status;
}
