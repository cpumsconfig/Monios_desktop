#include "windows_dll.h"

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    return windows_open_cube3d_window() == 0 ? 0 : 1;
}
