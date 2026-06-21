#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    /* Enter graphics mode and open the login/window provided by the window manager
       Applications should call kernel-provided interfaces rather than implement
       kernel functionality themselves. */
    app_enter_graphics_mode();
    /* Open a dedicated login window (implementation provided by WM/kernel) */
    app_open_cube3d_window();
    return 0;
}
