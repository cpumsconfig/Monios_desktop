#include "mcb.h"
#include "common.h"
#include "pci.h"
#include "smbus.h"
#include "kernel.h"

static mcb_info_t g_mcb_info;
static mcb_dimm_info_t g_dimm_info[MCB_MAX_DIMMS];
static uint8_t g_dimm_count;
static bool g_self_refresh;
static uint32_t g_ecc_correctable;
static uint32_t g_ecc_uncorrectable;

/* Detect memory controller via PCI */
static bool mcb_detect_controller(void)
{
    pci_device_info_t info;
    bool found = false;

    /* Look for host bridge / memory controller */
    /* Class 0x06, Subclass 0x00 = Host bridge */
    if (pci_find_first(0x06, 0x00, &info)) {
        found = true;
        log_write("mcb: host bridge found");
    }

    /* Also look for DRAM controller specifically */
    /* Class 0x05, Subclass 0x00 = Memory controller */
    if (!found && pci_find_first(0x05, 0x00, &info)) {
        found = true;
        log_write("mcb: DRAM controller found");
    }

    return found;
}

/* Read SPD data from DIMM via SMBus */
static bool mcb_read_spd(uint8_t dimm_index, mcb_dimm_info_t *dimm)
{
    uint8_t spd_addr;
    uint8_t spd_byte;

    if (!smbus_available()) {
        return false;
    }

    /* SPD addresses are typically 0x50-0x57 for DDR3/DDR4 */
    spd_addr = (uint8_t)(0x50 + dimm_index);

    /* Probe for DIMM at this address */
    if (!smbus_probe_device(spd_addr)) {
        return false;
    }

    memset(dimm, 0, sizeof(mcb_dimm_info_t));
    dimm->present = true;
    dimm->channel = dimm_index / 2;
    dimm->rank = dimm_index % 2;

    /* Read basic SPD information */
    /* Byte 2: Memory type */
    if (smbus_read_byte(spd_addr, 2, &spd_byte)) {
        switch (spd_byte) {
        case 0x07: g_mcb_info.memory_type = MCB_TYPE_DDR3; break;
        case 0x0C: g_mcb_info.memory_type = MCB_TYPE_DDR4; break;
        case 0x12: g_mcb_info.memory_type = MCB_TYPE_DDR5; break;
        default: g_mcb_info.memory_type = 0; break;
        }
    }

    /* Byte 4: SDRAM density and banks */
    uint32_t density_mbits = 0;
    if (smbus_read_byte(spd_addr, 4, &spd_byte)) {
        uint8_t density = spd_byte & 0x0F;
        density_mbits = 256 << density;
        if (density_mbits < 256) density_mbits = 256;
    }

    /* Byte 6: SDRAM package type (ranks) */
    uint8_t ranks = 1;
    if (smbus_read_byte(spd_addr, 6, &spd_byte)) {
        ranks = (uint8_t)((spd_byte & 0x07) + 1);
    }

    /* Byte 17-18: Minimum cycle time */
    uint8_t tck_min;
    uint32_t speed_mhz = 1600;
    if (smbus_read_byte(spd_addr, 18, &tck_min)) {
        if (tck_min > 0) {
            speed_mhz = 2000 / tck_min;
            if (speed_mhz < 100) speed_mhz = 800;
            if (speed_mhz > 4800) speed_mhz = 3200;
        }
    }

    dimm->clock_mhz = speed_mhz;

    /* Calculate total capacity */
    if (density_mbits > 0 && ranks > 0) {
        dimm->size_mb = (density_mbits * ranks) / 8;
    } else {
        dimm->size_mb = 2048; /* Default 2GB */
    }

    /* Read manufacturer name */
    uint8_t mfg_lo, mfg_hi;
    if (smbus_read_byte(spd_addr, 149, &mfg_lo) &&
        smbus_read_byte(spd_addr, 150, &mfg_hi)) {
        uint16_t mfg_id = (uint16_t)((mfg_hi << 8) | mfg_lo);
        switch (mfg_id) {
        case 0x2C00: strcpy(dimm->manufacturer, "Micron"); break;
        case 0xCE00: strcpy(dimm->manufacturer, "Samsung"); break;
        case 0xAD00: strcpy(dimm->manufacturer, "Hynix"); break;
        case 0x987F: strcpy(dimm->manufacturer, "Kingston"); break;
        default: strcpy(dimm->manufacturer, "Unknown"); break;
        }
    } else {
        strcpy(dimm->manufacturer, "Unknown");
    }

    /* Read part number (bytes 128-145 for DDR3) */
    for (int i = 0; i < 18; i++) {
        uint8_t c;
        if (smbus_read_byte(spd_addr, (uint8_t)(128 + i), &c)) {
            if (c >= 0x20 && c <= 0x7E) {
                dimm->part_number[i] = (char)c;
            } else {
                dimm->part_number[i] = ' ';
            }
        } else {
            dimm->part_number[i] = ' ';
        }
    }
    dimm->part_number[18] = '\0';

    /* ECC support - assume not supported for now */
    dimm->ecc_supported = false;

    /* Serial number - not read for now */
    memset(dimm->serial_number, 0, 4);

    log_write("mcb: DIMM detected");

    return true;
}

/* Initialize MCB */
void mcb_init(void)
{
    memset(&g_mcb_info, 0, sizeof(g_mcb_info));
    memset(g_dimm_info, 0, sizeof(g_dimm_info));
    g_dimm_count = 0;
    g_self_refresh = false;
    g_ecc_correctable = 0;
    g_ecc_uncorrectable = 0;

    g_mcb_info.present = false;
    g_mcb_info.channel_count = 2;
    g_mcb_info.dimm_count = 0;
    g_mcb_info.memory_type = 0;
    g_mcb_info.data_width = 64;
    g_mcb_info.total_size_mb = 0;
    g_mcb_info.clock_mhz = 0;
    g_mcb_info.ecc_enabled = false;
    g_mcb_info.thermal_throttling = false;
    g_mcb_info.command_count = 0;

    log_write("mcb: initializing memory controller...");

    /* Detect memory controller */
    if (mcb_detect_controller()) {
        g_mcb_info.present = true;

        /* Try to read SPD data from DIMMs */
        if (smbus_available()) {
            for (uint8_t i = 0; i < MCB_MAX_DIMMS; i++) {
                if (mcb_read_spd(i, &g_dimm_info[i])) {
                    g_dimm_count++;
                    g_mcb_info.total_size_mb += g_dimm_info[i].size_mb;

                    /* Use first DIMM's speed as system clock */
                    if (g_dimm_count == 1) {
                        g_mcb_info.clock_mhz = g_dimm_info[i].clock_mhz;
                    }
                }
            }
        }

        g_mcb_info.dimm_count = g_dimm_count;

        if (g_dimm_count > 0) {
            strcpy(g_mcb_info.status, "mcb: online, DIMMs detected");
            log_write("mcb: DIMMs found, total memory calculated");
        } else {
            strcpy(g_mcb_info.status, "mcb: online, no SPD data");
            log_write("mcb: controller detected but no DIMM SPD data available");
        }
    } else {
        strcpy(g_mcb_info.status, "mcb: no controller detected");
        log_write("mcb: no memory controller found");
    }
}

/* Check if MCB is present */
bool mcb_is_present(void)
{
    return g_mcb_info.present;
}

/* Get DIMM information */
int32_t mcb_get_dimm_info(uint8_t dimm_index, mcb_dimm_info_t *info)
{
    if (info == NULL || dimm_index >= g_dimm_count) {
        return -1;
    }

    memcpy(info, &g_dimm_info[dimm_index], sizeof(mcb_dimm_info_t));
    return 0;
}

/* Set memory clock frequency */
int32_t mcb_set_clock(uint32_t clock_mhz)
{
    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: not present");
        return -1;
    }

    if (clock_mhz == 0) {
        return -1;
    }

    g_mcb_info.clock_mhz = clock_mhz;
    g_mcb_info.command_count++;

    strcpy(g_mcb_info.status, "mcb: clock updated");
    log_write("mcb: clock frequency updated");
    return 0;
}

/* Enter self refresh mode */
int32_t mcb_enter_self_refresh(void)
{
    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: not present");
        return -1;
    }

    g_self_refresh = true;
    g_mcb_info.command_count++;

    strcpy(g_mcb_info.status, "mcb: self-refresh entered");
    log_write("mcb: entered self-refresh mode");
    return 0;
}

/* Exit self refresh mode */
int32_t mcb_exit_self_refresh(void)
{
    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: not present");
        return -1;
    }

    g_self_refresh = false;
    g_mcb_info.command_count++;

    strcpy(g_mcb_info.status, "mcb: self-refresh exited");
    log_write("mcb: exited self-refresh mode");
    return 0;
}

/* Get thermal reading */
int32_t mcb_get_thermal(uint8_t channel, uint32_t *temperature)
{
    if (temperature == NULL || channel >= MCB_MAX_CHANNELS) {
        return -1;
    }

    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: not present");
        return -1;
    }

    /*
     * In a real implementation, this would read from
     * the memory controller's thermal sensor registers.
     * For now, return a simulated temperature.
     */
    *temperature = 45 + channel * 5; /* Simulated: 45C, 50C, etc. */

    g_mcb_info.command_count++;
    strcpy(g_mcb_info.status, "mcb: thermal read");
    return 0;
}

/* Get ECC error counts */
int32_t mcb_get_ecc_errors(uint8_t channel, uint32_t *correctable, uint32_t *uncorrectable)
{
    if (correctable == NULL || uncorrectable == NULL || channel >= MCB_MAX_CHANNELS) {
        return -1;
    }

    if (!g_mcb_info.present) {
        strcpy(g_mcb_info.status, "mcb: not present");
        return -1;
    }

    /*
     * In a real implementation, this would read from
     * the memory controller's ECC error registers.
     */
    *correctable = g_ecc_correctable;
    *uncorrectable = g_ecc_uncorrectable;

    g_mcb_info.command_count++;
    strcpy(g_mcb_info.status, "mcb: ECC status read");
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
