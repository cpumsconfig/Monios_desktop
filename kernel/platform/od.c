#include "od.h"
#include "common.h"
#include "cpu.h"
#include "power.h"
#include "opp.h"

static od_info_t g_od_info;
static od_domain_info_t g_od_domains[OD_MAX_DOMAINS];

void od_init(void)
{
    memset(&g_od_info, 0, sizeof(g_od_info));
    memset(g_od_domains, 0, sizeof(g_od_domains));
    g_od_info.present = false;
    g_od_info.enabled = false;
    g_od_info.domain_count = 0;
    g_od_info.feature_flags = 0;
    g_od_info.tdp_watts = 0;
    g_od_info.power_limit_watts = 0;
    g_od_info.temp_limit_c = 105; /* typical 105C */
    g_od_info.current_limit_ma = 0;
    strcpy(g_od_info.status, "od: not detected");

    /* TODO: detect Overdrive capability from CPU/GPU */
    /* Check for AMD Overdrive */
    /* Check for Intel Turbo Boost */
}

bool od_is_present(void)
{
    return g_od_info.present;
}

bool od_is_enabled(void)
{
    return g_od_info.enabled;
}

int32_t od_enable(bool enable)
{
    if (!g_od_info.present) {
        strcpy(g_od_info.status, "od: not available");
        return -1;
    }

    g_od_info.enabled = enable;
    strcpy(g_od_info.status, enable ? "od: enabled" : "od: disabled");
    return 0;
}

int32_t od_get_domain_info(uint8_t domain_index, od_domain_info_t *info)
{
    if (info == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }

    memcpy(info, &g_od_domains[domain_index], sizeof(od_domain_info_t));
    return 0;
}

int32_t od_set_freq_offset(uint8_t domain_index, int32_t offset_mhz)
{
    if (domain_index >= g_od_info.domain_count) {
        return -1;
    }
    if (!g_od_info.enabled) {
        strcpy(g_od_info.status, "od: disabled");
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_FREQ_CONTROL)) {
        strcpy(g_od_info.status, "od: freq control not supported");
        return -1;
    }

    /* TODO: implement frequency offset setting */
    g_od_domains[domain_index].freq_offset_mhz = offset_mhz;
    strcpy(g_od_info.status, "od: freq offset set");
    return 0;
}

int32_t od_set_volt_offset(uint8_t domain_index, int32_t offset_mv)
{
    if (domain_index >= g_od_info.domain_count) {
        return -1;
    }
    if (!g_od_info.enabled) {
        strcpy(g_od_info.status, "od: disabled");
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_VOLT_CONTROL)) {
        strcpy(g_od_info.status, "od: volt control not supported");
        return -1;
    }

    /* TODO: implement voltage offset setting */
    g_od_domains[domain_index].volt_offset_mv = offset_mv;
    strcpy(g_od_info.status, "od: volt offset set");
    return 0;
}

int32_t od_set_power_limit(uint32_t watts)
{
    if (!g_od_info.present) {
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_TDP_CONTROL)) {
        strcpy(g_od_info.status, "od: power limit not supported");
        return -1;
    }

    g_od_info.power_limit_watts = watts;
    /* TODO: implement power limit setting */
    strcpy(g_od_info.status, "od: power limit set");
    return 0;
}

int32_t od_set_temp_limit(uint32_t celsius)
{
    if (!g_od_info.present) {
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_TEMP_MONITOR)) {
        strcpy(g_od_info.status, "od: temp limit not supported");
        return -1;
    }

    g_od_info.temp_limit_c = celsius;
    /* TODO: implement temperature limit setting */
    strcpy(g_od_info.status, "od: temp limit set");
    return 0;
}

int32_t od_set_tdp(uint32_t watts)
{
    if (!g_od_info.present) {
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_TDP_CONTROL)) {
        strcpy(g_od_info.status, "od: TDP control not supported");
        return -1;
    }

    g_od_info.tdp_watts = watts;
    /* TODO: implement TDP setting */
    strcpy(g_od_info.status, "od: TDP set");
    return 0;
}

int32_t od_get_temperature(uint8_t domain_index, uint32_t *temperature)
{
    if (temperature == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_TEMP_MONITOR)) {
        return -1;
    }

    *temperature = g_od_domains[domain_index].temperature_c;
    return 0;
}

int32_t od_get_power(uint8_t domain_index, uint32_t *power)
{
    if (power == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }
    if (!(g_od_info.feature_flags & OD_FEAT_POWER_MONITOR)) {
        return -1;
    }

    *power = g_od_domains[domain_index].power_watts;
    return 0;
}

const od_info_t *od_info(void)
{
    return &g_od_info;
}

const char *od_status(void)
{
    return g_od_info.status;
}
