#include "bios.h"
#include "common.h"

static bios_info_t g_bios_info;

void bios_init(void)
{
    volatile uint16_t *bda = (volatile uint16_t *) 0x400;

    memset(&g_bios_info, 0, sizeof(g_bios_info));
    g_bios_info.legacy_boot = true;
    g_bios_info.equipment_word = *((volatile uint16_t *) 0x410);
    g_bios_info.conventional_kb = *((volatile uint16_t *) 0x413);
    g_bios_info.ebda_segment = *((volatile uint16_t *) 0x40E);
    for (uint32_t index = 0; index < 4; index++) {
        if (bda[index] != 0) {
            g_bios_info.com_ports++;
        }
    }
    strcpy(g_bios_info.status, "bios: legacy data area available");
}

const bios_info_t *bios_info(void)
{
    return &g_bios_info;
}

const char *bios_status(void)
{
    return g_bios_info.status;
}
