#include "opp.h"
#include "common.h"
#include "cpu.h"
#include "power.h"

static opp_info_t g_opp_info;
static opp_domain_info_t g_opp_domains[OPP_MAX_DOMAINS];

void opp_init(void)
{
    memset(&g_opp_info, 0, sizeof(g_opp_info));
    memset(g_opp_domains, 0, sizeof(g_opp_domains));
    g_opp_info.present = true;
    g_opp_info.domain_count = 0;
    g_opp_info.current_domain = 0;
    g_opp_info.transition_count = 0;
    g_opp_info.scaling_enabled = false;
    strcpy(g_opp_info.governor, "performance");
    strcpy(g_opp_info.status, "opp: subsystem initialized");

    /* TODO: detect available OPP domains from hardware */
    /* Add CPU domain if available */
    /* Add GPU domain if available */
}

bool opp_is_present(void)
{
    return g_opp_info.present;
}

int32_t opp_add_domain(uint8_t type, const char *name)
{
    uint8_t index;

    if (name == NULL) {
        return -1;
    }
    if (g_opp_info.domain_count >= OPP_MAX_DOMAINS) {
        strcpy(g_opp_info.status, "opp: too many domains");
        return -1;
    }

    index = g_opp_info.domain_count;
    memset(&g_opp_domains[index], 0, sizeof(opp_domain_info_t));
    g_opp_domains[index].type = type;
    g_opp_domains[index].point_count = 0;
    g_opp_domains[index].current_point = 0;
    g_opp_domains[index].current_freq_hz = 0;
    g_opp_domains[index].current_voltage_uv = 0;
    g_opp_domains[index].min_freq_hz = 0xFFFFFFFF;
    g_opp_domains[index].max_freq_hz = 0;
    strncpy(g_opp_domains[index].name, name, 15);
    g_opp_domains[index].name[15] = '\0';

    g_opp_info.domain_count++;
    strcpy(g_opp_info.status, "opp: domain added");
    return (int32_t) index;
}

int32_t opp_add_point(uint8_t domain_index, uint32_t freq_hz, uint32_t voltage_uv, uint8_t state)
{
    opp_domain_info_t *domain;
    opp_point_t *point;

    if (domain_index >= g_opp_info.domain_count) {
        return -1;
    }

    domain = &g_opp_domains[domain_index];
    if (domain->point_count >= OPP_MAX_POINTS) {
        strcpy(g_opp_info.status, "opp: too many points");
        return -1;
    }

    point = &domain->points[domain->point_count];
    memset(point, 0, sizeof(opp_point_t));
    point->freq_hz = freq_hz;
    point->voltage_uv = voltage_uv;
    point->power_mw = 0; /* TODO: calculate power */
    point->transition_latency_us = 1000; /* typical 1ms */
    point->state = state;
    point->level = domain->point_count;

    /* Update min/max frequency */
    if (freq_hz < domain->min_freq_hz) {
        domain->min_freq_hz = freq_hz;
    }
    if (freq_hz > domain->max_freq_hz) {
        domain->max_freq_hz = freq_hz;
    }

    domain->point_count++;
    strcpy(g_opp_info.status, "opp: point added");
    return (int32_t) domain->point_count - 1;
}

int32_t opp_set_frequency(uint8_t domain_index, uint32_t freq_hz)
{
    opp_domain_info_t *domain;
    int32_t best_point = -1;
    uint32_t best_diff = 0xFFFFFFFF;

    if (domain_index >= g_opp_info.domain_count) {
        return -1;
    }

    domain = &g_opp_domains[domain_index];

    /* Find closest available OPP point */
    for (uint8_t i = 0; i < domain->point_count; i++) {
        if (!(domain->points[i].state & OPP_STATE_AVAILABLE)) {
            continue;
        }
        uint32_t diff = freq_hz > domain->points[i].freq_hz
            ? freq_hz - domain->points[i].freq_hz
            : domain->points[i].freq_hz - freq_hz;
        if (diff < best_diff) {
            best_diff = diff;
            best_point = i;
        }
    }

    if (best_point < 0) {
        strcpy(g_opp_info.status, "opp: no available point");
        return -1;
    }

    /* TODO: actually change frequency and voltage */
    domain->current_point = (uint8_t) best_point;
    domain->current_freq_hz = domain->points[best_point].freq_hz;
    domain->current_voltage_uv = domain->points[best_point].voltage_uv;

    g_opp_info.transition_count++;
    strcpy(g_opp_info.status, "opp: frequency set");
    return 0;
}

int32_t opp_set_voltage(uint8_t domain_index, uint32_t voltage_uv)
{
    (void) domain_index;
    (void) voltage_uv;
    if (domain_index >= g_opp_info.domain_count) {
        return -1;
    }
    /* TODO: implement voltage scaling */
    strcpy(g_opp_info.status, "opp: voltage set stub");
    return 0;
}

int32_t opp_get_domain_info(uint8_t domain_index, opp_domain_info_t *info)
{
    if (info == NULL || domain_index >= g_opp_info.domain_count) {
        return -1;
    }

    memcpy(info, &g_opp_domains[domain_index], sizeof(opp_domain_info_t));
    return 0;
}

int32_t opp_enable_scaling(bool enable)
{
    g_opp_info.scaling_enabled = enable;
    strcpy(g_opp_info.status, enable ? "opp: scaling enabled" : "opp: scaling disabled");
    return 0;
}

int32_t opp_set_governor(const char *governor)
{
    if (governor == NULL) {
        return -1;
    }
    strncpy(g_opp_info.governor, governor, 15);
    g_opp_info.governor[15] = '\0';
    strcpy(g_opp_info.status, "opp: governor set");
    return 0;
}

const opp_info_t *opp_info(void)
{
    return &g_opp_info;
}

const char *opp_status(void)
{
    return g_opp_info.status;
}
