#include "appsys.h"
#include "stdio.h"

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;

    fputs("cube3d.elf opening Cube3D window\r\n");
    app_open_cube3d_window();
    return 0;
}
