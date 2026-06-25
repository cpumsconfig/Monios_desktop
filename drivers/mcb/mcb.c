#include "mcb.h"
#include "common.h"
#include "pci.h"
#include "cpu.h"

static mcb_info_t g_mcb_info;
static mcb_dimm_info_t g_mcb_dimms[MCB_MAX_DIMMS];

void mcb_init(void)
{
    memset(&g_mcb_info, 0, sizeof(g_mcb_info));
    memset(g_mcb_dimms, 0, sizeof(g_mcb_dimms));
    g_mcb_info.present = false;
    g_mcb_info.channel_count = 0;
    g_mcb_info.dimm_count = 0;
    g_mcb_info.memory_type = 0;
    g_mcb_info.data_width = 64;
    g_mcb_info.total_size_mb = 0;
    g_mcb_info.clock_mhz = 0;
    g_mcb_info.ecc_enabled = false;
    g_mcb_info.thermal_throttling = false;
    g_mcb_info.command_count = 0;
    strcpy(g_mcb_info.status, "mcb: not detected");

    /* TODO: detect memory controller via PCI or SMBus */
    /* Check for Intel MCH (Memory Controller Hub) */
    /* Check for AMD Memory Controller */
}

bool mcb_is_present(void)
{
    return g_mcb_info.present;
}

int32_t mcb_get_dimm_info(uint8_t dimm_index, mcb_dimm_info_t *info)
{
    if (info == NULL || dimm_index >= MCB_MAX_DIMMS) {
        return -1;
    }
    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: controller unavailable");
        return -1;
    }
    if (!g_mcb_dimms[dimm_index].present) {
        strcpy(g_mcb_info.status, "mcb: dimm not present");
        return -1;
    }

    memcpy(info, &g_mcb_dimms[dimm_index], sizeof(mcb_dimm_info_t));
    strcpy(g_mcb_info.status, "mcb: dimm info retrieved");
    return 0;
}

int32_t mcb_set_clock(uint32_t clock_mhz)
{
    if (!g_mcb_info.present) {
        return -1;
    }
    if (clock_mhz == 0) {
        return -1;
    }

    g_mcb_info.clock_mhz = clock_mhz;
    g_mcb_info.command_count++;
    /* TODO: implement actual clock setting */
    strcpy(g_mcb_info.status, "mcb: clock set stub");
    return 0;
}

int32_t mcb_enter_self_refresh(void)
{
    if (!g_mcb_info.present) {
        return -1;
    }
    g_mcb_info.command_count++;
    /* TODO: implement self-refresh entry */
    strcpy(g_mcb_info.status, "mcb: self-refresh enter stub");
    return 0;
}

int32_t mcb_exit_self_refresh(void)
{
    if (!g_mcb_info.present) {
        return -1;
    }
    g_mcb_info.command_count++;
    /* TODO: implement self-refresh exit */
    strcpy(g_mcb_info.status, "mcb: self-refresh exit stub");
    return 0;
}

int32_t mcb_get_thermal(uint8_t channel, uint32_t *temperature)
{
    (void) channel;
    if (temperature == NULL) {
        return -1;
    }
    if (!g_mcb_info.present) {
        return -1;
    }
    /* TODO: implement thermal reading */
    *temperature = 0;
    strcpy(g_mcb_info.status, "mcb: thermal read stub");
    return 0;
}

int32_t mcb_get_ecc_errors(uint8_t channel, uint32_t *correctable, uint32_t *uncorrectable)
{
    (void) channel;
    if (correctable == NULL || uncorrectable == NULL) {
        return -1;
    }
    if (!g_mcb_info.present || !g_mcb_info.ecc_enabled) {
        return -1;
    }
    /* TODO: implement ECC error counting */
    *correctable = 0;
    *uncorrectable = 0;
    strcpy(g_mcb_info.status, "mcb: ecc status stub");
    return 0;
}

const mcb_info_t *mcb_info(void)
{
    return &g_mcb_info;
}

const char *mcb_status(void)
{
    return g_mcb_info.status;
}
