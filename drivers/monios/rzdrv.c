#include "driver_api.h"

static monios_driver_log_fn_t g_log;
static bool g_loaded;
static uint64_t g_last_tick;

__declspec(dllexport)
bool DriverEntry(const monios_driver_runtime_t *runtime)
{
    if (runtime == 0 ||
        runtime->abi_version != MONIOS_DRIVER_ABI_VERSION ||
        runtime->log == 0) {
        return false;
    }
    g_log = runtime->log;
    g_loaded = true;
    g_last_tick = 0;
    g_log("rzdrv native compatibility driver online");
    return true;
}

__declspec(dllexport)
void DriverTick(uint64_t now_ticks)
{
    if (!g_loaded || g_log == 0 || now_ticks < g_last_tick + 300U) {
        return;
    }
    g_last_tick = now_ticks;
    g_log("rzdrv periodic health tick");
}

__declspec(dllexport)
void DriverUnload(void)
{
    if (!g_loaded) {
        return;
    }
    if (g_log != 0) {
        g_log("rzdrv native compatibility driver offline");
    }
    g_log = 0;
    g_loaded = false;
    g_last_tick = 0;
}
