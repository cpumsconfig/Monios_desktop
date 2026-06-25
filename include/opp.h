#ifndef _OPP_H_
#define _OPP_H_

#include "stdbool.h"
#include "stdint.h"

#define OPP_MAX_POINTS        16
#define OPP_MAX_DOMAINS       8

/* OPP (Operating Performance Point) states */
#define OPP_STATE_AVAILABLE   0x01
#define OPP_STATE_TURBO       0x02
#define OPP_STATE_SUSPENDED   0x04
#define OPP_STATE_DISABLED    0x08
#define OPP_STATE_DYNAMIC     0x10

/* OPP domain types */
#define OPP_DOMAIN_CPU        0x00
#define OPP_DOMAIN_GPU        0x01
#define OPP_DOMAIN_MEMORY     0x02
#define OPP_DOMAIN_BUS        0x03
#define OPP_DOMAIN_PLATFORM   0x04

typedef struct {
    uint32_t freq_hz;
    uint32_t voltage_uv;
    uint32_t power_mw;
    uint32_t transition_latency_us;
    uint8_t state;
    uint8_t level;
} opp_point_t;

typedef struct {
    bool present;
    uint8_t domain_count;
    uint8_t current_domain;
    uint32_t transition_count;
    bool scaling_enabled;
    char governor[16];
    char status[64];
} opp_info_t;

typedef struct {
    uint8_t type;
    uint8_t point_count;
    uint8_t current_point;
    uint32_t current_freq_hz;
    uint32_t current_voltage_uv;
    uint32_t min_freq_hz;
    uint32_t max_freq_hz;
    opp_point_t points[OPP_MAX_POINTS];
    char name[16];
} opp_domain_info_t;

void opp_init(void);
bool opp_is_present(void);
int32_t opp_add_domain(uint8_t type, const char *name);
int32_t opp_add_point(uint8_t domain_index, uint32_t freq_hz, uint32_t voltage_uv, uint8_t state);
int32_t opp_set_frequency(uint8_t domain_index, uint32_t freq_hz);
int32_t opp_set_voltage(uint8_t domain_index, uint32_t voltage_uv);
int32_t opp_get_domain_info(uint8_t domain_index, opp_domain_info_t *info);
int32_t opp_enable_scaling(bool enable);
int32_t opp_set_governor(const char *governor);
const opp_info_t *opp_info(void);
const char *opp_status(void);

#endif
