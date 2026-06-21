#include "common.h"
#include "driver_manager.h"
#include "kernel.h"

static kernel_driver_t g_drivers[8];
static uint32_t g_driver_count;

extern bool smbus_driver_init(void);
extern bool net_driver_init(void);
extern bool graphics_driver_init(void);
extern void smbus_driver_shutdown(void);
extern void net_shutdown(void);
extern void graphics_shutdown(void);
extern void audio_shutdown(void);

void driver_manager_init(void)
{
    memset(g_drivers, 0, sizeof(g_drivers));
    g_driver_count = 0;

    g_drivers[g_driver_count++] = (kernel_driver_t) { "graphics", graphics_driver_init, graphics_shutdown, false };
    g_drivers[g_driver_count++] = (kernel_driver_t) { "smbus", smbus_driver_init, smbus_driver_shutdown, false };
    g_drivers[g_driver_count++] = (kernel_driver_t) { "onboard-net", net_driver_init, net_shutdown, false };
    g_drivers[g_driver_count++] = (kernel_driver_t) { "onboard-audio", NULL, audio_shutdown, true };

    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_drivers[i].init != NULL) {
            g_drivers[i].loaded = g_drivers[i].init();
        }
    }
}

void driver_manager_shutdown(void)
{
    for (int32_t i = (int32_t) g_driver_count - 1; i >= 0; i--) {
        if (g_drivers[i].shutdown != NULL && g_drivers[i].loaded) {
            g_drivers[i].shutdown();
            g_drivers[i].loaded = false;
        }
    }
}

uint32_t driver_manager_count(void)
{
    return g_driver_count;
}

const kernel_driver_t *driver_manager_at(uint32_t index)
{
    if (index >= g_driver_count) {
        return NULL;
    }
    return &g_drivers[index];
}
