#include "stddef.h"
#include "stdint.h"
#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();

    write_line("explorar.exe");
    write_line("MONIOS user app");
    fputs("argc: ");
    if (argc == 0) {
        write_line("0");
    } else {
        char text[12];
        uint32_t value = (uint32_t) argc;
        uint32_t i = 0;
        uint32_t j = 0;
        char temp[12];

        while (value > 0) {
            temp[i++] = (char) ('0' + (value % 10));
            value /= 10;
        }
        while (i > 0) {
            text[j++] = temp[--i];
        }
        text[j] = '\0';
        write_line(text);
    }
    if (argv != NULL && argc > 0 && argv[0] != NULL) {
        fputs("program: ");
        write_line(argv[0]);
    }
    if (info != NULL && info->user_name != NULL) {
        fputs("user: ");
        write_line(info->user_name);
    }
    if (info != NULL && info->cwd != NULL) {
        fputs("cwd: ");
        write_line(info->cwd);
    }
    return 0;
}
