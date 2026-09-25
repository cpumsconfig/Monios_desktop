#include "driver_api.h"

static monios_driver_log_fn_t g_log;
static bool g_loaded;

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
    runtime->log("monios native driver online");
    return true;
}

__declspec(dllexport)
void DriverUnload(void)
{
    if (!g_loaded) {
        return;
    }
    if (g_log != 0) {
        g_log("monios native driver offline");
    }
    g_log = 0;
    g_loaded = false;
}
