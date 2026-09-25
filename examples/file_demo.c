/*
 * file_demo.c - MoniOS file operation example (feature 32).
 *
 * Demonstrates: create/write/read a file, make a directory, list a
 * directory, check existence/size, and clean up.
 *
 * Build:
 *   x86_64-w64-mingw32-gcc examples/file_demo.c \
 *       -I user/lib -I include -o C:\\Monios\\Apps\\file_demo.exe
 */
#include "appsys.h"
#include "console_dll.h"
#include <stdio.h>
#include <string.h>

#define DEMO_DIR  "C:\\Monios\\Apps\\filedemo"
#define DEMO_FILE "C:\\Monios\\Apps\\filedemo\\hello.txt"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    console_set_title("MoniOS File Demo");

    /* 1. Create directory */
    if (app_file_exists(DEMO_DIR)) {
        printf("dir already exists: %s\n", DEMO_DIR);
    } else if (app_file_mkdir(DEMO_DIR)) {
        printf("created dir: %s\n", DEMO_DIR);
    } else {
        printf("mkdir failed: %s\n", DEMO_DIR);
    }

    /* 2. Write a file */
    const char *text = "Hello from MoniOS file_demo!\r\nline 2\r\n";
    int w = app_file_write(DEMO_FILE, text, (uint32_t)strlen(text));
    printf("wrote %d bytes to %s\n", w, DEMO_FILE);

    /* 3. Stat it */
    printf("exists=%d size=%d is_dir=%d\n",
           app_file_exists(DEMO_FILE),
           app_file_size(DEMO_FILE),
           app_file_is_dir(DEMO_FILE));

    /* 4. Read it back */
    char buf[256];
    int r = app_file_read(DEMO_FILE, buf, sizeof(buf) - 1);
    if (r > 0) {
        buf[r] = '\0';
        printf("read back %d bytes:\n%s\n", r, buf);
    }

    /* 5. Directory listing */
    char listing[1024];
    int n = app_file_list_dir(DEMO_DIR, listing, sizeof(listing) - 1);
    if (n > 0) {
        listing[n] = '\0';
        printf("listing of %s:\n%s\n", DEMO_DIR, listing);
    }

    /* 6. Clean up */
    app_file_delete(DEMO_FILE);
    app_file_rmdir(DEMO_DIR);
    printf("cleaned up demo files\n");
    return 0;
}
