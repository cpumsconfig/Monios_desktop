#include "appsys.h"

extern void app_runtime_set_launch_info(const app_launch_info_t *info);

extern int main(int argc, char **argv);

int _start(const app_launch_info_t *info)
{
    app_runtime_set_launch_info(info);
    if (info == 0 || info->abi_version != APP_ABI_VERSION) {
        app_exit(-1);
    }
    app_exit(main((int) info->argc, info->argv));
    return 0;
}
