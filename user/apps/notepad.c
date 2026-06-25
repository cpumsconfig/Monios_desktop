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
    write_line("notepad.elf");
    write_line("graphical notepad");
    return 0;
}
