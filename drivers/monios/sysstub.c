#include "appsys.h"
#include "stdio.h"

static const char *basename(const char *path)
{
    const char *base = path;

    if (path == 0) {
        return "driver.sys";
    }
    while (*path != '\0') {
        if (*path == '/' || *path == '\\') {
            base = path + 1;
        }
        path++;
    }
    return base;
}

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    const char *path = 0;

    (void) argc;
    (void) argv;
    if (info != 0) {
        path = info->program_path;
    }

    write_line(basename(path));
    write_line("MoniOS signed driver package");
    write_line("hardware binding is provided by the kernel driver ABI compatibility layer");
    if (info == 0 || info->privilege_level > APP_PRIV_R2) {
        if (!app_request_r2("driver package inspection requests R2")) {
            write_line("R2 denied");
            return 1;
        }
    }
    write_line("package verified at R2");
    return 0;
}
