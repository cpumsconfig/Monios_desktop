#include "opp.h"
#include "common.h"
#include "kernel.h"
#include "cpu.h"

static opp_info_t g_opp_info;
static opp_domain_info_t g_opp_domains[OPP_MAX_DOMAINS];
static uint8_t g_opp_domain_count;

/* Initialize OPP subsystem */
void opp_init(void)
{
    memset(&g_opp_info, 0, sizeof(g_opp_info));
    memset(g_opp_domains, 0, sizeof(g_opp_domains));
    g_opp_domain_count = 0;

    g_opp_info.present = false;
    g_opp_info.domain_count = 0;
    g_opp_info.current_domain = 0;
    g_opp_info.transition_count = 0;
    g_opp_info.scaling_enabled = false;

    strncpy(g_opp_info.governor, "ondemand", sizeof(g_opp_info.governor) - 1);
    g_opp_info.governor[sizeof(g_opp_info.governor) - 1] = '\0';

    strcpy(g_opp_info.status, "opp: initializing...");
    log_write("opp: initializing OPP subsystem...");

    /*
     * Add default CPU domain with standard OPP points.
     * These are typical values for x86_64 processors.
     */
    int32_t cpu_domain = opp_add_domain(OPP_DOMAIN_CPU, "cpu0");
    if (cpu_domain >= 0) {
        opp_add_point(cpu_domain,  800000000,  900000, OPP_STATE_AVAILABLE);
        opp_add_point(cpu_domain, 1200000000, 1000000, OPP_STATE_AVAILABLE);
        opp_add_point(cpu_domain, 1600000000, 1100000, OPP_STATE_AVAILABLE);
        opp_add_point(cpu_domain, 2000000000, 1200000, OPP_STATE_AVAILABLE);
        opp_add_point(cpu_domain, 2400000000, 1300000, OPP_STATE_AVAILABLE | OPP_STATE_TURBO);
        opp_add_point(cpu_domain, 2800000000, 1400000, OPP_STATE_AVAILABLE | OPP_STATE_TURBO);
    }

    /* Add default GPU domain */
    int32_t gpu_domain = opp_add_domain(OPP_DOMAIN_GPU, "gpu0");
    if (gpu_domain >= 0) {
        opp_add_point(gpu_domain,  300000000,  850000, OPP_STATE_AVAILABLE);
        opp_add_point(gpu_domain,  600000000,  950000, OPP_STATE_AVAILABLE);
        opp_add_point(gpu_domain,  900000000, 1050000, OPP_STATE_AVAILABLE);
        opp_add_point(gpu_domain, 1200000000, 1150000, OPP_STATE_AVAILABLE | OPP_STATE_TURBO);
    }

    /* Add default memory domain */
    int32_t mem_domain = opp_add_domain(OPP_DOMAIN_MEMORY, "mem0");
    if (mem_domain >= 0) {
        opp_add_point(mem_domain,  800000000, 1200000, OPP_STATE_AVAILABLE);
        opp_add_point(mem_domain, 1333000000, 1200000, OPP_STATE_AVAILABLE);
        opp_add_point(mem_domain, 1600000000, 1200000, OPP_STATE_AVAILABLE);
        opp_add_point(mem_domain, 1866000000, 1200000, OPP_STATE_AVAILABLE);
    }

    g_opp_info.present = true;
    g_opp_info.scaling_enabled = true;

    strcpy(g_opp_info.status, "opp: initialized");
    log_write("opp: OPP subsystem initialized");
}

/* Check if OPP is present */
bool opp_is_present(void)
{
    return g_opp_info.present;
}

/* Add a new OPP domain */
int32_t opp_add_domain(uint8_t type, const char *name)
{
    if (g_opp_domain_count >= OPP_MAX_DOMAINS) {
        return -1;
    }

    /* Find free domain slot */
    int32_t domain_id = -1;
    for (uint8_t i = 0; i < OPP_MAX_DOMAINS; i++) {
        if (g_opp_domains[i].point_count == 0 && g_opp_domains[i].current_freq_hz == 0) {
            domain_id = i;
            break;
        }
    }

    if (domain_id < 0) {
        return -1;
    }

    /* Initialize domain */
    opp_domain_info_t *domain = &g_opp_domains[domain_id];
    memset(domain, 0, sizeof(opp_domain_info_t));

    domain->type = type;
    domain->point_count = 0;
    domain->current_point = 0;
    domain->current_freq_hz = 0;
    domain->current_voltage_uv = 0;
    domain->min_freq_hz = 0xFFFFFFFF;
    domain->max_freq_hz = 0;

    if (name != NULL) {
        strncpy(domain->name, name, sizeof(domain->name) - 1);
        domain->name[sizeof(domain->name) - 1] = '\0';
    }

    g_opp_domain_count++;
    g_opp_info.domain_count = g_opp_domain_count;

    return domain_id;
}

/* Add an OPP point to a domain */
int32_t opp_add_point(uint8_t domain_index, uint32_t freq_hz, uint32_t voltage_uv, uint8_t state)
{
    if (domain_index >= OPP_MAX_DOMAINS) {
        return -1;
    }

    opp_domain_info_t *domain = &g_opp_domains[domain_index];
    if (domain->point_count >= OPP_MAX_POINTS) {
        return -1;
    }

    /* Find insertion point (sorted by frequency ascending) */
    uint8_t insert_pos = domain->point_count;
    for (uint8_t i = 0; i < domain->point_count; i++) {
        if (domain->points[i].freq_hz > freq_hz) {
            insert_pos = i;
            break;
        }
    }

    /* Shift existing points up */
    for (int j = domain->point_count; j > insert_pos; j--) {
        memcpy(&domain->points[j], &domain->points[j - 1], sizeof(opp_point_t));
    }

    /* Initialize new point */
    opp_point_t *point = &domain->points[insert_pos];
    point->freq_hz = freq_hz;
    point->voltage_uv = voltage_uv;
    point->state = state;
    point->level = insert_pos;

    /*
     * Calculate estimated power consumption.
     * Power ≈ C * V² * f, simplified to V² * f
     */
    uint64_t v = voltage_uv / 1000; /* Convert to mV for calculation */
    point->power_mw = (uint32_t)((v * v * freq_hz) / 1000000000ULL);

    /* Default transition latency (microseconds) */
    point->transition_latency_us = 1000; /* 1ms default */

    domain->point_count++;

    /* Update min/max frequency */
    if (freq_hz < domain->min_freq_hz) {
        domain->min_freq_hz = freq_hz;
    }
    if (freq_hz > domain->max_freq_hz) {
        domain->max_freq_hz = freq_hz;
    }

    /* Reassign levels */
    for (uint8_t i = 0; i < domain->point_count; i++) {
        domain->points[i].level = i;
    }

    return insert_pos;
}

/* Set frequency for a domain */
int32_t opp_set_frequency(uint8_t domain_index, uint32_t freq_hz)
{
    if (domain_index >= OPP_MAX_DOMAINS) {
        return -1;
    }

    opp_domain_info_t *domain = &g_opp_domains[domain_index];
    if (domain->point_count == 0) {
        return -1;
    }

    /* Find the closest available OPP point */
    int32_t best_point = -1;
    uint32_t best_diff = 0xFFFFFFFF;

    for (uint8_t i = 0; i < domain->point_count; i++) {
        if (!(domain->points[i].state & OPP_STATE_AVAILABLE)) {
            continue;
        }

        uint32_t diff = (domain->points[i].freq_hz > freq_hz) ?
                         (domain->points[i].freq_hz - freq_hz) :
                         (freq_hz - domain->points[i].freq_hz);

        if (diff < best_diff) {
            best_diff = diff;
            best_point = i;
        }
    }

    if (best_point < 0) {
        return -1;
    }

    opp_point_t *point = &domain->points[best_point];

    /*
     * For CPU domains, attempt to set frequency via MSR.
     * IA32_PERF_CTL (0x199) controls the target performance state.
     *
     * Note: In a real OS, this would require proper CPU frequency
     * scaling infrastructure. For now, we simulate the transition.
     */
    if (domain->type == OPP_DOMAIN_CPU) {
        /*
         * Calculate the performance state ratio.
         * On Intel CPUs, the ratio is frequency / base clock (100MHz).
         *
         * We would write to MSR IA32_PERF_CTL here, but in a
         * freestanding environment without proper MSR access,
         * we just simulate success.
         */
    }

    /* Update current state */
    domain->current_point = (uint8_t)best_point;
    domain->current_freq_hz = point->freq_hz;
    domain->current_voltage_uv = point->voltage_uv;

    g_opp_info.transition_count++;
    g_opp_info.current_domain = domain_index;

    strcpy(g_opp_info.status, "opp: frequency changed");
    return 0;
}

/* Set voltage for a domain */
int32_t opp_set_voltage(uint8_t domain_index, uint32_t voltage_uv)
{
    if (domain_index >= OPP_MAX_DOMAINS) {
        return -1;
    }

    opp_domain_info_t *domain = &g_opp_domains[domain_index];

    /*
     * In a real implementation, this would adjust the voltage
     * regulator for the domain. For now, we just update the
     * current voltage value.
     */
    domain->current_voltage_uv = voltage_uv;

    strcpy(g_opp_info.status, "opp: voltage changed");
    return 0;
}

/* Get domain information */
int32_t opp_get_domain_info(uint8_t domain_index, opp_domain_info_t *info)
{
    if (domain_index >= OPP_MAX_DOMAINS || info == NULL) {
        return -1;
    }

    memcpy(info, &g_opp_domains[domain_index], sizeof(opp_domain_info_t));
    return 0;
}

/* Enable or disable frequency scaling */
int32_t opp_enable_scaling(bool enable)
{
    g_opp_info.scaling_enabled = enable;

    if (enable) {
        strcpy(g_opp_info.status, "opp: scaling enabled");
        log_write("opp: frequency scaling enabled");
    } else {
        strcpy(g_opp_info.status, "opp: scaling disabled");
        log_write("opp: frequency scaling disabled");
    }

    return 0;
}

/* Set the OPP governor */
int32_t opp_set_governor(const char *governor)
{
    if (governor == NULL) {
        return -1;
    }

    strncpy(g_opp_info.governor, governor, sizeof(g_opp_info.governor) - 1);
    g_opp_info.governor[sizeof(g_opp_info.governor) - 1] = '\0';

    strcpy(g_opp_info.status, "opp: governor changed");
    log_write("opp: governor changed");

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
