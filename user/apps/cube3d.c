#include "appsys.h"
#include "stdio.h"

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    fputs("cube3d.elf drawing in user mode\r\n");
    app_enter_graphics_mode();
    app_graphics_fill_rect(0, 0, 1024, 768, 0x000F172A);
    app_graphics_fill_rect(390, 238, 180, 180, 0x0038BDF8);
    app_graphics_fill_rect(430, 198, 180, 180, 0x007DD3FC);
    app_graphics_fill_rect(570, 238, 40, 180, 0x000E7490);
    app_graphics_fill_rect(430, 198, 40, 40, 0x00BAE6FD);
    app_graphics_fill_rect(410, 258, 140, 140, 0x000F172A);
    app_graphics_fill_rect(426, 274, 108, 108, 0x00F8FAFC);
    app_graphics_present();
    return 0;
}
