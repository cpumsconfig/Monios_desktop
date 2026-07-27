#include "console_dll.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    (void) argc;
    (void) argv;
    printf("Hello World\n");
    console_set_title("Hello World");
    return 0;
}
