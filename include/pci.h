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
    uint16_t command;
    uint8_t header_type;
    uint8_t reserved;
    uint32_t bar0;
    uint32_t bar1;
    uint32_t bar2;
    uint32_t bar3;
    uint32_t bar4;
    uint32_t bar5;
    uint64_t bar_address[6];
    uint8_t capability_pointer;
    uint8_t power_state;
    bool power_management_capable;
    bool msi_capable;
    bool msix_capable;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} pci_device_info_t;

typedef bool (*pci_enum_callback_t)(const pci_device_info_t *info, void *ctx);

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
uint8_t pci_find_capability(uint8_t bus, uint8_t slot, uint8_t func, uint8_t capability_id);
void pci_enumerate(pci_enum_callback_t callback, void *ctx);
bool pci_find_first(uint8_t class_code, uint8_t subclass, pci_device_info_t *out_info);
void pci_log_devices(void);

/* ── New unified PCI driver interface ─────────────────────────────── */
/* Full enumeration over bus 0-255 / slot 0-31 / func 0-7 (including
 * PCI-to-PCI bridge recursion). Returns device count. */
uint32_t pci_probe(void);
uint32_t pci_device_count(void);

/* Lookup helpers. prog_if == 0xFF means "don't care". */
bool pci_find_by_vendor_device(uint16_t vendor, uint16_t device,
                               pci_device_info_t *out_info);
bool pci_find_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                      pci_device_info_t *out_info);

/* Command-register helpers */
void pci_enable_bus_master(uint8_t bus, uint8_t slot, uint8_t func);
void pci_enable_memory(uint8_t bus, uint8_t slot, uint8_t func);
void pci_enable_io(uint8_t bus, uint8_t slot, uint8_t func);

/* BAR decoder: returns decoded base address. Sets *out_is_io, *out_is_64bit. */
uint64_t pci_get_bar(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index,
                     bool *out_is_io, bool *out_is_64bit);

/* Power management: state 0 = D0 (fully on), 3 = D3 (off). */
void pci_set_power_state(uint8_t bus, uint8_t slot, uint8_t func, uint8_t state);

/* MSI/MSI-X detection helpers (framework). */
uint8_t pci_msi_cap_offset(uint8_t bus, uint8_t slot, uint8_t func);
uint8_t pci_msix_cap_offset(uint8_t bus, uint8_t slot, uint8_t func);

/* Generic config-space read/write front-ends (32-bit). */
uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

/* Shutdown / status. */
void pci_shutdown(void);
const char *pci_status(void);

#endif
