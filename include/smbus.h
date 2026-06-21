#ifndef _SMBUS_H_
#define _SMBUS_H_

#include "stdbool.h"

/* SMBus Driver Functions */
void smbus_init(void);
void smbus_driver_shutdown(void);
bool smbus_available(void);
const char *smbus_status(void);
bool smbus_driver_init(void);

/* SMBus Operations */
bool smbus_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data);
bool smbus_write_byte(uint8_t addr, uint8_t cmd, uint8_t data);
bool smbus_read_word(uint8_t addr, uint8_t cmd, uint16_t *data);
bool smbus_write_word(uint8_t addr, uint8_t cmd, uint16_t data);
bool smbus_quick_command(uint8_t addr);
bool smbus_probe_device(uint8_t addr);
bool smbus_is_intel(void);

/* Intel SMBus Functions */
bool intelbus1_probe(const pci_device_info_t *info);
bool intelbus1_wait_ready(uint32_t timeout_ms);
bool intelbus1_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data);
bool intelbus1_write_byte(uint8_t addr, uint8_t cmd, uint8_t data);
bool intelbus1_read_word(uint8_t addr, uint8_t cmd, uint16_t *data);
bool intelbus1_write_word(uint8_t addr, uint8_t cmd, uint16_t data);
bool intelbus1_quick_command(uint8_t addr);
uint16_t intelbus1_get_io_base(void);
bool intelbus1_is_present(void);

/* AMD SMBus Functions */
bool amdbus1_probe(const pci_device_info_t *info);
bool amdbus1_wait_ready(uint32_t timeout_ms);
bool amdbus1_read_byte(uint8_t addr, uint8_t cmd, uint8_t *data);
bool amdbus1_write_byte(uint8_t addr, uint8_t cmd, uint8_t data);
bool amdbus1_read_word(uint8_t addr, uint8_t cmd, uint16_t *data);
bool amdbus1_write_word(uint8_t addr, uint8_t cmd, uint16_t data);
bool amdbus1_quick_command(uint8_t addr);
uint16_t amdbus1_get_io_base(void);
bool amdbus1_is_present(void);

#endif