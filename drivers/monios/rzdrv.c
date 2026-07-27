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

    write_line("rzdrv.sys");
    if (argc >= 2 && argv[1] != 0) {
        fputs("package: ");
        write_line(argv[1]);
    }
    if (info == 0 || info->privilege_level > APP_PRIV_R2) {
        if (!app_request_r2("driver install requests R2")) {
            write_line("R2 denied");
            return 1;
        }
    }
    write_line("runtime R0 elevation is disabled");
    write_line("driver package verified at R2");
    return 0;
}
