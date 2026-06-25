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
    (void) argc;
    (void) argv;

    write_line("square.elf");
    app_enter_graphics_mode();
    app_graphics_fill_rect(392, 248, 240, 240, 0x000F172A);
    app_graphics_fill_rect(412, 268, 200, 200, 0x0038BDF8);
    app_graphics_fill_rect(456, 312, 112, 112, 0x00F8FAFC);
    app_graphics_present();
    write_line("rendered square");
    return 0;
}
