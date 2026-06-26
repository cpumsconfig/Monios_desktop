#include "od.h"
#include "common.h"
#include "kernel.h"
#include "cpu.h"

static od_info_t g_od_info;
static od_domain_info_t g_od_domains[OD_MAX_DOMAINS];

/* Internal helper: read CPU temperature via MSR */
static uint32_t od_read_cpu_temp(void)
{
    /*
     * IA32_THERM_STATUS (0x19C)
     * Digital Readout: bits 22:16
     * Valid: bit 31
     *
     * Temperature = TCC Activation Temp - Digital Readout
     * For simplicity, return a simulated value.
     */
    return 45; /* Placeholder */
}

/* Internal helper: read CPU power via MSR */
static uint32_t od_read_cpu_power(void)
{
    /*
     * IA32_PACKAGE_ENERGY_STATUS (0x611)
     * Energy counter, needs to be divided by energy unit
     *
     * For simplicity, return a simulated value.
     */
    return 35; /* Placeholder */
}

/* Initialize OD (Overdrive) subsystem */
void od_init(void)
{
    memset(&g_od_info, 0, sizeof(g_od_info));
    memset(g_od_domains, 0, sizeof(g_od_domains));

    g_od_info.present = true;
    g_od_info.enabled = false;
    g_od_info.domain_count = 0;
    g_od_info.feature_flags = 0;
    g_od_info.tdp_watts = 65;
    g_od_info.power_limit_watts = 80;
    g_od_info.temp_limit_c = 100;
    g_od_info.current_limit_ma = 100000;

    strcpy(g_od_info.status, "od: initialized");
    log_write("od: overdrive subsystem initialized");

    /* Add CPU domain */
    od_domain_info_t *cpu_domain = &g_od_domains[OD_DOMAIN_CPU];
    cpu_domain->type = OD_DOMAIN_CPU;
    cpu_domain->state = OD_STATE_AUTO;
    cpu_domain->base_freq_mhz = 2000;
    cpu_domain->max_freq_mhz = 2800;
    cpu_domain->current_freq_mhz = 2000;
    cpu_domain->base_voltage_mv = 1200;
    cpu_domain->current_voltage_mv = 1200;
    cpu_domain->temperature_c = 0;
    cpu_domain->power_watts = 0;
    cpu_domain->current_ma = 0;
    cpu_domain->freq_offset_mhz = 0;
    cpu_domain->volt_offset_mv = 0;
    strcpy(cpu_domain->name, "cpu0");

    g_od_info.feature_flags |= OD_FEAT_FREQ_CONTROL;
    g_od_info.feature_flags |= OD_FEAT_VOLT_CONTROL;
    g_od_info.feature_flags |= OD_FEAT_TEMP_MONITOR;
    g_od_info.feature_flags |= OD_FEAT_POWER_MONITOR;
    g_od_info.feature_flags |= OD_FEAT_CURRENT_MONITOR;
    g_od_info.feature_flags |= OD_FEAT_TDP_CONTROL;
    g_od_info.feature_flags |= OD_FEAT_BOOST;
    g_od_info.feature_flags |= OD_FEAT_THROTTLING;

    g_od_info.domain_count++;

    /* Read initial temperature and power */
    cpu_domain->temperature_c = od_read_cpu_temp();
    cpu_domain->power_watts = od_read_cpu_power();
    cpu_domain->current_ma = (cpu_domain->power_watts * 1000) / cpu_domain->current_voltage_mv;

    log_write("od: CPU domain initialized");

    /* Add GPU domain */
    od_domain_info_t *gpu_domain = &g_od_domains[OD_DOMAIN_GPU];
    gpu_domain->type = OD_DOMAIN_GPU;
    gpu_domain->state = OD_STATE_AUTO;
    gpu_domain->base_freq_mhz = 600;
    gpu_domain->max_freq_mhz = 1200;
    gpu_domain->current_freq_mhz = 600;
    gpu_domain->base_voltage_mv = 1000;
    gpu_domain->current_voltage_mv = 1000;
    gpu_domain->temperature_c = 40;
    gpu_domain->power_watts = 20;
    gpu_domain->current_ma = 0;
    gpu_domain->freq_offset_mhz = 0;
    gpu_domain->volt_offset_mv = 0;
    strcpy(gpu_domain->name, "gpu0");

    g_od_info.domain_count++;
    log_write("od: GPU domain initialized");

    /* Add memory domain */
    od_domain_info_t *mem_domain = &g_od_domains[OD_DOMAIN_MEMORY];
    mem_domain->type = OD_DOMAIN_MEMORY;
    mem_domain->state = OD_STATE_AUTO;
    mem_domain->base_freq_mhz = 1333;
    mem_domain->max_freq_mhz = 1600;
    mem_domain->current_freq_mhz = 1333;
    mem_domain->base_voltage_mv = 1200;
    mem_domain->current_voltage_mv = 1200;
    mem_domain->temperature_c = 35;
    mem_domain->power_watts = 5;
    mem_domain->current_ma = 0;
    mem_domain->freq_offset_mhz = 0;
    mem_domain->volt_offset_mv = 0;
    strcpy(mem_domain->name, "mem0");

    g_od_info.domain_count++;
    log_write("od: memory domain initialized");

    strcpy(g_od_info.status, "od: ready");
}

/* Check if OD is present */
bool od_is_present(void)
{
    return g_od_info.present;
}

/* Check if OD is enabled */
bool od_is_enabled(void)
{
    return g_od_info.enabled;
}

/* Enable/disable overdrive */
int32_t od_enable(bool enable)
{
    g_od_info.enabled = enable;

    if (enable) {
        strcpy(g_od_info.status, "od: enabled");
        log_write("od: overdrive enabled");
    } else {
        /* Reset all offsets when disabling */
        for (uint8_t i = 0; i < g_od_info.domain_count; i++) {
            g_od_domains[i].freq_offset_mhz = 0;
            g_od_domains[i].volt_offset_mv = 0;
            g_od_domains[i].current_freq_mhz = g_od_domains[i].base_freq_mhz;
            g_od_domains[i].current_voltage_mv = g_od_domains[i].base_voltage_mv;
        }
        strcpy(g_od_info.status, "od: disabled");
        log_write("od: overdrive disabled");
    }

    return 0;
}

/* Get domain information */
int32_t od_get_domain_info(uint8_t domain_index, od_domain_info_t *info)
{
    if (info == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }

    memcpy(info, &g_od_domains[domain_index], sizeof(od_domain_info_t));
    return 0;
}

/* Set frequency offset for a domain */
int32_t od_set_freq_offset(uint8_t domain_index, int32_t offset_mhz)
{
    if (domain_index >= g_od_info.domain_count) {
        return -1;
    }

    if (!g_od_info.enabled) {
        strcpy(g_od_info.status, "od: not enabled");
        return -1;
    }

    if (!(g_od_info.feature_flags & OD_FEAT_FREQ_CONTROL)) {
        strcpy(g_od_info.status, "od: freq control not supported");
        return -1;
    }

    od_domain_info_t *domain = &g_od_domains[domain_index];

    /* Clamp offset to reasonable range (-500 to +500 MHz) */
    if (offset_mhz < -500) {
        offset_mhz = -500;
    }
    if (offset_mhz > 500) {
        offset_mhz = 500;
    }

    domain->freq_offset_mhz = offset_mhz;
    domain->current_freq_mhz = domain->base_freq_mhz + offset_mhz;

    /* Update state flags */
    if (offset_mhz > 0) {
        domain->state |= OD_STATE_OVERCLOCK;
    } else {
        domain->state &= ~OD_STATE_OVERCLOCK;
    }

    /*
     * In a real implementation, this would:
     * 1. For AMD: program P-state MSRs (0xC0010064-0x6B)
     * 2. For Intel: program IA32_PERF_CTL (0x199)
     * 3. Wait for frequency change to complete
     */

    strcpy(g_od_info.status, "od: frequency offset set");
    return 0;
}

/* Set voltage offset for a domain */
int32_t od_set_volt_offset(uint8_t domain_index, int32_t offset_mv)
{
    if (domain_index >= g_od_info.domain_count) {
        return -1;
    }

    if (!g_od_info.enabled) {
        strcpy(g_od_info.status, "od: not enabled");
        return -1;
    }

    if (!(g_od_info.feature_flags & OD_FEAT_VOLT_CONTROL)) {
        strcpy(g_od_info.status, "od: volt control not supported");
        return -1;
    }

    od_domain_info_t *domain = &g_od_domains[domain_index];

    /* Clamp offset to reasonable range (-300 to +200 mV) */
    if (offset_mv < -300) {
        offset_mv = -300;
    }
    if (offset_mv > 200) {
        offset_mv = 200;
    }

    domain->volt_offset_mv = offset_mv;
    domain->current_voltage_mv = domain->base_voltage_mv + offset_mv;

    /* Update state flags */
    if (offset_mv < 0) {
        domain->state |= OD_STATE_UNDERVOLT;
    } else {
        domain->state &= ~OD_STATE_UNDERVOLT;
    }

    /*
     * In a real implementation, this would:
     * 1. For AMD: program SVI2 telemetry or P-state voltage
     * 2. For Intel: program VR voltage via mailbox
     * 3. Wait for voltage to stabilize
     */

    strcpy(g_od_info.status, "od: voltage offset set");
    return 0;
}

/* Set power limit */
int32_t od_set_power_limit(uint32_t watts)
{
    if (watts < 10 || watts > 250) {
        strcpy(g_od_info.status, "od: power limit out of range");
        return -1;
    }

    g_od_info.power_limit_watts = watts;

    /*
     * In a real implementation, this would:
     * 1. For Intel: program IA32_PACKAGE_POWER_LIMIT (0x610)
     * 2. For AMD: program P-state power limit
     */

    strcpy(g_od_info.status, "od: power limit set");
    return 0;
}

/* Set temperature limit */
int32_t od_set_temp_limit(uint32_t celsius)
{
    if (celsius < 50 || celsius > 110) {
        strcpy(g_od_info.status, "od: temp limit out of range");
        return -1;
    }

    g_od_info.temp_limit_c = celsius;

    /*
     * In a real implementation, this would:
     * 1. Program thermal throttling thresholds
     * 2. Configure PROCHOT/PROCHOT# behavior
     */

    strcpy(g_od_info.status, "od: temperature limit set");
    return 0;
}

/* Set TDP */
int32_t od_set_tdp(uint32_t watts)
{
    if (watts < 10 || watts > 250) {
        strcpy(g_od_info.status, "od: TDP out of range");
        return -1;
    }

    g_od_info.tdp_watts = watts;

    /*
     * In a real implementation, this would:
     * 1. Configure PL1/PL2 power limits
     * 2. Adjust turbo power budget
     */

    strcpy(g_od_info.status, "od: TDP set");
    return 0;
}

/* Get temperature for a domain */
int32_t od_get_temperature(uint8_t domain_index, uint32_t *temperature)
{
    if (temperature == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }

    if (!(g_od_info.feature_flags & OD_FEAT_TEMP_MONITOR)) {
        strcpy(g_od_info.status, "od: temp monitor not supported");
        return -1;
    }

    od_domain_info_t *domain = &g_od_domains[domain_index];

    /* Read actual temperature */
    if (domain->type == OD_DOMAIN_CPU) {
        domain->temperature_c = od_read_cpu_temp();
    }

    *temperature = domain->temperature_c;

    /* Check thermal throttling */
    if (domain->temperature_c >= g_od_info.temp_limit_c) {
        domain->state |= OD_STATE_THERMAL_LIMIT;
    } else {
        domain->state &= ~OD_STATE_THERMAL_LIMIT;
    }

    strcpy(g_od_info.status, "od: temperature read");
    return 0;
}

/* Get power for a domain */
int32_t od_get_power(uint8_t domain_index, uint32_t *power)
{
    if (power == NULL || domain_index >= g_od_info.domain_count) {
        return -1;
    }

    if (!(g_od_info.feature_flags & OD_FEAT_POWER_MONITOR)) {
        strcpy(g_od_info.status, "od: power monitor not supported");
        return -1;
    }

    od_domain_info_t *domain = &g_od_domains[domain_index];

    /* Read actual power */
    if (domain->type == OD_DOMAIN_CPU) {
        domain->power_watts = od_read_cpu_power();
    }

    *power = domain->power_watts;

    /* Check power limit */
    if (domain->power_watts >= g_od_info.power_limit_watts) {
        domain->state |= OD_STATE_POWER_LIMIT;
    } else {
        domain->state &= ~OD_STATE_POWER_LIMIT;
    }

    strcpy(g_od_info.status, "od: power read");
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
