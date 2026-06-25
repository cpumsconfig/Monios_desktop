#ifndef _OD_H_
#define _OD_H_

#include "stdbool.h"
#include "stdint.h"

#define OD_MAX_STATES         8
#define OD_MAX_DOMAINS        4

/* OD (Overdrive) states */
#define OD_STATE_DISABLED     0x00
#define OD_STATE_AUTO         0x01
#define OD_STATE_MANUAL       0x02
#define OD_STATE_TURBO        0x04
#define OD_STATE_OVERCLOCK    0x08
#define OD_STATE_UNDERVOLT    0x10
#define OD_STATE_THERMAL_LIMIT 0x20
#define OD_STATE_POWER_LIMIT  0x40

/* OD domain types */
#define OD_DOMAIN_CPU         0x00
#define OD_DOMAIN_GPU         0x01
#define OD_DOMAIN_MEMORY      0x02
#define OD_DOMAIN_SOC         0x03

/* OD feature flags */
#define OD_FEAT_FREQ_CONTROL  0x0001
#define OD_FEAT_VOLT_CONTROL  0x0002
#define OD_FEAT_TEMP_MONITOR  0x0004
#define OD_FEAT_POWER_MONITOR 0x0008
#define OD_FEAT_CURRENT_MONITOR 0x0010
#define OD_FEAT_TDP_CONTROL   0x0020
#define OD_FEAT_BOOST         0x0040
#define OD_FEAT_THROTTLING    0x0080

typedef struct {
    bool present;
    bool enabled;
    uint8_t domain_count;
    uint32_t feature_flags;
    uint32_t tdp_watts;
    uint32_t power_limit_watts;
    uint32_t temp_limit_c;
    uint32_t current_limit_ma;
    char status[64];
} od_info_t;

typedef struct {
    uint8_t type;
    uint8_t state;
    uint32_t base_freq_mhz;
    uint32_t max_freq_mhz;
    uint32_t current_freq_mhz;
    uint32_t base_voltage_mv;
    uint32_t current_voltage_mv;
    uint32_t temperature_c;
    uint32_t power_watts;
    uint32_t current_ma;
    int32_t freq_offset_mhz;
    int32_t volt_offset_mv;
    char name[16];
} od_domain_info_t;

void od_init(void);
bool od_is_present(void);
bool od_is_enabled(void);
int32_t od_enable(bool enable);
int32_t od_get_domain_info(uint8_t domain_index, od_domain_info_t *info);
int32_t od_set_freq_offset(uint8_t domain_index, int32_t offset_mhz);
int32_t od_set_volt_offset(uint8_t domain_index, int32_t offset_mv);
int32_t od_set_power_limit(uint32_t watts);
int32_t od_set_temp_limit(uint32_t celsius);
int32_t od_set_tdp(uint32_t watts);
int32_t od_get_temperature(uint8_t domain_index, uint32_t *temperature);
int32_t od_get_power(uint8_t domain_index, uint32_t *power);
const od_info_t *od_info(void);
const char *od_status(void);

#endif
