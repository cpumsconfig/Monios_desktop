#ifndef _PCI_H_
#define _PCI_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} pci_device_info_t;

typedef bool (*pci_enum_callback_t)(const pci_device_info_t *info, void *ctx);

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_enumerate(pci_enum_callback_t callback, void *ctx);
bool pci_find_first(uint8_t class_code, uint8_t subclass, pci_device_info_t *out_info);
void pci_log_devices(void);

#endif
