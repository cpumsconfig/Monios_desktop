#include "common.h"
#include "hda.h"
#include "kernel.h"
#include "pci.h"

#define PCI_CLASS_MULTIMEDIA     0x04
#define PCI_SUBCLASS_HDA         0x03
#define PCI_COMMAND_OFFSET       0x04
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUS_MASTER   0x0004

#define HDA_REG_GCAP             0x00

static hda_info_t g_hda_info;

static uint32_t hda_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static uint16_t hda_read16(uint32_t offset)
{
    volatile uint16_t *ptr = (volatile uint16_t *) (uint64_t) (g_hda_info.mmio_base + offset);

    return *ptr;
}

bool hda_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;

    memset(&g_hda_info, 0, sizeof(g_hda_info));
    strcpy(g_hda_info.status, "hda: not found");

    if (!pci_find_first(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HDA, &info)) {
        log_write(g_hda_info.status);
        return false;
    }

    g_hda_info.present = true;
    g_hda_info.vendor_id = info.vendor_id;
    g_hda_info.device_id = info.device_id;
    g_hda_info.bus = info.bus;
    g_hda_info.slot = info.slot;
    g_hda_info.func = info.func;
    g_hda_info.irq = info.interrupt_line;
    g_hda_info.mmio_base = hda_bar_base(info.bar0);
    if (g_hda_info.mmio_base == 0) {
        strcpy(g_hda_info.status, "hda: mmio bar missing");
        log_write(g_hda_info.status);
        return true;
    }

    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_hda_info.global_cap = hda_read16(HDA_REG_GCAP);
    g_hda_info.mmio_ready = true;
    strcpy(g_hda_info.status, "hda: controller detected");
    log_write(g_hda_info.status);
    return true;
}

void hda_shutdown(void)
{
    if (g_hda_info.present) {
        strcpy(g_hda_info.status, "hda: shutdown");
        log_write(g_hda_info.status);
    }
}

const hda_info_t *hda_info(void)
{
    return &g_hda_info;
}

const char *hda_status(void)
{
    return g_hda_info.status;
}
