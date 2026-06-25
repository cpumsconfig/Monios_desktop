#include "common.h"
#include "kernel.h"
#include "mmu.h"
#include "nvme.h"
#include "pci.h"

#define PCI_CLASS_STORAGE       0x01
#define PCI_SUBCLASS_NVM        0x08
#define PCI_COMMAND_OFFSET      0x04
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_BUS_MASTER  0x0004

#define NVME_REG_CAP            0x00
#define NVME_REG_VS             0x08
#define NVME_REG_CSTS           0x1C

static nvme_info_t g_nvme_info;

static uint32_t nvme_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static uint32_t nvme_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_nvme_info.mmio_base + offset);

    return *ptr;
}

bool nvme_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;
    uint64_t cap;

    memset(&g_nvme_info, 0, sizeof(g_nvme_info));
    strcpy(g_nvme_info.status, "nvme: not found");

    if (!pci_find_first(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVM, &info)) {
        log_write(g_nvme_info.status);
        return false;
    }

    g_nvme_info.present = true;
    g_nvme_info.vendor_id = info.vendor_id;
    g_nvme_info.device_id = info.device_id;
    g_nvme_info.bus = info.bus;
    g_nvme_info.slot = info.slot;
    g_nvme_info.func = info.func;
    g_nvme_info.irq = info.interrupt_line;
    g_nvme_info.mmio_base = nvme_bar_base(info.bar0);
    if (g_nvme_info.mmio_base == 0) {
        strcpy(g_nvme_info.status, "nvme: mmio bar missing");
        log_write(g_nvme_info.status);
        return true;
    }

    mmu_map_identity(g_nvme_info.mmio_base, 0x4000);
    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_nvme_info.cap_lo = nvme_read32(NVME_REG_CAP);
    g_nvme_info.cap_hi = nvme_read32(NVME_REG_CAP + 4);
    g_nvme_info.version = nvme_read32(NVME_REG_VS);
    g_nvme_info.csts = nvme_read32(NVME_REG_CSTS);
    cap = (uint64_t) g_nvme_info.cap_lo | ((uint64_t) g_nvme_info.cap_hi << 32);
    g_nvme_info.max_queue_entries = (uint16_t) ((cap & 0xFFFFu) + 1u);
    g_nvme_info.doorbell_stride = (uint8_t) ((cap >> 32) & 0xFu);
    g_nvme_info.mmio_ready = true;
    strcpy(g_nvme_info.status, "nvme: controller detected");
    log_write(g_nvme_info.status);
    return true;
}

void nvme_shutdown(void)
{
    if (g_nvme_info.present) {
        strcpy(g_nvme_info.status, "nvme: shutdown");
        log_write(g_nvme_info.status);
    }
}

const nvme_info_t *nvme_info(void)
{
    return &g_nvme_info;
}

const char *nvme_status(void)
{
    return g_nvme_info.status;
}
