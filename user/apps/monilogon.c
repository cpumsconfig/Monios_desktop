#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    app_enter_graphics_mode();
    app_graphics_fill_rect(0, 0, 1024, 768, 0x0010171F);
    app_graphics_fill_rect(332, 238, 360, 180, 0x001B2A38);
    app_graphics_fill_rect(356, 262, 312, 132, 0x0027B3A5);
    app_graphics_fill_rect(386, 292, 252, 72, 0x00F8FAFC);
    app_graphics_present();
    fputs("monilog.exe rendered user-mode logon splash\r\n");
    return 0;
}
