#include "appsys.h"
#include "stdio.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    (void) argc;
    (void) argv;

    write_line("rzdrv.rzs");
    if (info == 0 || info->privilege_level > APP_PRIV_R2) {
        if (!app_request_r2("driver install requests R2")) {
            write_line("R2 denied");
            return 1;
        }
    }
    if (!app_request_r0("driver install requests R0")) {
        write_line("R0 not granted");
        return 1;
    }
    write_line("R0 granted by driver install");
    write_line("driver init ok");
    return 0;
}
