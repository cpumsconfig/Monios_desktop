#include "common.h"
#include "kernel.h"
#include "pcnet.h"

#define PCI_VENDOR_AMD            0x1022
#define PCI_DEVICE_PCNET          0x2000
#define PCI_COMMAND_OFFSET        0x04
#define PCI_COMMAND_IO            0x0001
#define PCI_COMMAND_BUS_MASTER    0x0004

static pcnet_info_t g_pcnet_info;

static uint32_t pcnet_bar_base(uint32_t bar)
{
    if ((bar & 1u) == 0) {
        return 0;
    }
    return bar & 0xFFFFFFFCu;
}

static void pcnet_hex_byte(char *out, uint8_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    out[0] = hex[(value >> 4) & 0xF];
    out[1] = hex[value & 0xF];
}

static void pcnet_set_mac_text(const uint8_t mac[6])
{
    for (uint32_t i = 0; i < 6; i++) {
        pcnet_hex_byte(&g_pcnet_info.mac_text[i * 3], mac[i]);
        if (i < 5) {
            g_pcnet_info.mac_text[i * 3 + 2] = ':';
        }
    }
    g_pcnet_info.mac_text[17] = '\0';
}

bool pcnet_supported(const pci_device_info_t *info)
{
    return info != NULL && info->vendor_id == PCI_VENDOR_AMD && info->device_id == PCI_DEVICE_PCNET;
}

bool pcnet_init(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6])
{
    uint16_t command;

    memset(&g_pcnet_info, 0, sizeof(g_pcnet_info));
    strcpy(g_pcnet_info.status, "pcnet: not found");
    if (!pcnet_supported(info) || net == NULL || mac == NULL) {
        return false;
    }

    g_pcnet_info.present = true;
    g_pcnet_info.vendor_id = info->vendor_id;
    g_pcnet_info.device_id = info->device_id;
    g_pcnet_info.bus = info->bus;
    g_pcnet_info.slot = info->slot;
    g_pcnet_info.func = info->func;
    g_pcnet_info.irq = info->interrupt_line;
    g_pcnet_info.io_base = pcnet_bar_base(info->bar0);
    if (g_pcnet_info.io_base == 0 || g_pcnet_info.io_base > 0xFFF0u) {
        strcpy(g_pcnet_info.status, "pcnet: io bar invalid");
        return true;
    }

    command = pci_config_read16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info->bus, info->slot, info->func, PCI_COMMAND_OFFSET, command);

    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = inb((uint16_t) (g_pcnet_info.io_base + i));
    }
    pcnet_set_mac_text(mac);
    strcpy(net->driver, "pcnet");
    net->io_base = g_pcnet_info.io_base;
    net->mmio_base = 0;
    g_pcnet_info.io_ready = true;
    net->connected = false;
    strcpy(g_pcnet_info.status, "pcnet: detected (backend pending)");
    return true;
}

bool pcnet_ready(void)
{
    return false;
}

void pcnet_shutdown(void)
{
    if (g_pcnet_info.present) {
        strcpy(g_pcnet_info.status, "pcnet: shutdown");
        log_write(g_pcnet_info.status);
    }
}

const pcnet_info_t *pcnet_info(void)
{
    return &g_pcnet_info;
}

const char *pcnet_status(void)
{
    if (g_pcnet_info.status[0] == '\0') {
        return "pcnet: not initialized";
    }
    return g_pcnet_info.status;
}
