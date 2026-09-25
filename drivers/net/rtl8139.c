#include "common.h"
#include "kernel.h"
#include "rtl8139.h"

#define RTL8139_VENDOR_REALTEK 0x10EC
#define RTL8139_DEVICE_8139 0x8139
#define RTL8139_PCI_COMMAND 0x04
#define RTL8139_PCI_COMMAND_IO 0x0001
#define RTL8139_PCI_COMMAND_BUS_MASTER 0x0004
#define RTL8139_REG_MAC 0x00
#define RTL8139_REG_COMMAND 0x37
#define RTL8139_COMMAND_RESET 0x10

static uint32_t rtl8139_io_base(const pci_device_info_t *info)
{
    if (info == NULL || (info->bar0 & 1u) == 0) {
        return 0;
    }
    return info->bar0 & 0xFFFFFFFCu;
}

bool rtl8139_supported(const pci_device_info_t *info)
{
    return info != NULL &&
           info->vendor_id == RTL8139_VENDOR_REALTEK &&
           info->device_id == RTL8139_DEVICE_8139;
}

static bool rtl8139_reset(uint32_t io_base)
{
    outb((uint16_t) (io_base + RTL8139_REG_COMMAND), RTL8139_COMMAND_RESET);
    for (uint32_t i = 0; i < 10000; i++) {
        if ((inb((uint16_t) (io_base + RTL8139_REG_COMMAND)) &
             RTL8139_COMMAND_RESET) == 0) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool rtl8139_mac_valid(const uint8_t mac[6])
{
    bool all_zero = true;
    bool all_ff = true;

    for (uint32_t i = 0; i < 6; i++) {
        all_zero = all_zero && mac[i] == 0;
        all_ff = all_ff && mac[i] == 0xFF;
    }
    return !all_zero && !all_ff;
}

bool rtl8139_detect(const pci_device_info_t *info, net_info_t *net, uint8_t mac[6])
{
    uint16_t command;
    uint32_t io_base;

    if (!rtl8139_supported(info) || net == NULL || mac == NULL) {
        return false;
    }
    io_base = rtl8139_io_base(info);
    if (io_base < 0x100U || io_base > 0xFFF0U) {
        return false;
    }
    command = pci_config_read16(info->bus, info->slot, info->func, RTL8139_PCI_COMMAND);
    command |= RTL8139_PCI_COMMAND_IO | RTL8139_PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info->bus, info->slot, info->func, RTL8139_PCI_COMMAND, command);
    if (!rtl8139_reset(io_base)) {
        log_write("rtl8139: controller reset timeout");
        return false;
    }
    for (uint32_t i = 0; i < 6; i++) {
        mac[i] = inb((uint16_t) (io_base + RTL8139_REG_MAC + i));
    }
    if (!rtl8139_mac_valid(mac)) {
        memcpy(mac, (const uint8_t[]) { 0x52, 0x54, 0x00, 0x81, 0x39, 0x00 }, 6);
        log_write("rtl8139: invalid hardware mac, using development fallback");
    }
    net->onboard = true;
    net->io_base = io_base;
    net->mmio_base = 0;
    strcpy(net->driver, "rtl8139-onboard");
    net->connected = false;
    log_write("rtl8139: controller reset and mac ready; rx/tx backend pending");
    return true;
}
