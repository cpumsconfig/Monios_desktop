#ifndef _MCB_H_
#define _MCB_H_

#include "stdbool.h"
#include "stdint.h"

#define MCB_MAX_CHANNELS      8
#define MCB_MAX_RANKS         4
#define MCB_MAX_DIMMS         8

/* MCB (Memory Controller Buffer) interface types */
#define MCB_TYPE_DDR3         0x03
#define MCB_TYPE_DDR4         0x04
#define MCB_TYPE_DDR5         0x05
#define MCB_TYPE_LPDDR4       0x14
#define MCB_TYPE_LPDDR5       0x15

/* MCB command codes */
#define MCB_CMD_STATUS        0x00
#define MCB_CMD_SET_MODE      0x01
#define MCB_CMD_TRAINING      0x02
#define MCB_CMD_REFRESH       0x03
#define MCB_CMD_SELF_REFRESH  0x04
#define MCB_CMD_POWER_DOWN    0x05
#define MCB_CMD_THERMAL       0x06
#define MCB_CMD_ECC_STATUS    0x07
#define MCB_CMD_ECC_INJECT    0x08

typedef struct {
    bool present;
    uint8_t channel_count;
    uint8_t dimm_count;
    uint8_t memory_type;
    uint32_t data_width;
    uint32_t total_size_mb;
    uint32_t clock_mhz;
    bool ecc_enabled;
    bool thermal_throttling;
    uint32_t command_count;
    char status[64];
} mcb_info_t;

typedef struct {
    uint8_t channel;
    uint8_t rank;
    uint32_t size_mb;
    uint32_t clock_mhz;
    bool present;
    bool ecc_supported;
    char manufacturer[16];
    char part_number[20];
    uint8_t serial_number[4];
} mcb_dimm_info_t;

void mcb_init(void);
bool mcb_is_present(void);
int32_t mcb_get_dimm_info(uint8_t dimm_index, mcb_dimm_info_t *info);
int32_t mcb_set_clock(uint32_t clock_mhz);
int32_t mcb_enter_self_refresh(void);
int32_t mcb_exit_self_refresh(void);
int32_t mcb_get_thermal(uint8_t channel, uint32_t *temperature);
int32_t mcb_get_ecc_errors(uint8_t channel, uint32_t *correctable, uint32_t *uncorrectable);
const mcb_info_t *mcb_info(void);
const char *mcb_status(void);

#endif
