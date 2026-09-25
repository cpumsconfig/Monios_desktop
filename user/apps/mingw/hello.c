#include "appsys.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

extern uint64_t mingw_sample_magic(void);

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void print_listing_preview(const char *listing)
{
    uint32_t shown = 0;
    uint32_t length = 0;

    if (listing == 0) {
        return;
    }
    while (listing[length] != '\0' && shown < 3) {
        uint32_t start = length;

        while (listing[length] != '\0' && listing[length] != '\n') {
            length++;
        }
        if (length > start) {
            char line[64];
            uint32_t copy_length = length - start;

            if (copy_length >= sizeof(line)) {
                copy_length = sizeof(line) - 1;
            }
            memcpy(line, listing + start, copy_length);
            line[copy_length] = '\0';
            fputs("  ");
            write_line(line);
            shown++;
        }
        if (listing[length] == '\n') {
            length++;
        }
    }
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    char cwd[PATH_MAX_LEN];
    char listing[256];

    (void) argc;
    (void) argv;
    write_line("MoniOS MinGW sample");
    write_line("gcc + as + ld -> PE32+ user application");
    printf("assembly marker: 0x%llx\r\n",
           (unsigned long long) mingw_sample_magic());

    if (info != 0) {
        fputs("program: ");
        write_line(info->program_path);
        fputs("user: ");
        write_line(info->user_name);
    }
    if (app_getcwd(cwd, sizeof(cwd)) >= 0) {
        fputs("cwd: ");
        write_line(cwd);
    }
    if (app_file_list_dir("C:\\Monios\\Apps", listing, sizeof(listing)) == 0) {
        write_line("first entries from the MoniOS filesystem:");
        print_listing_preview(listing);
    }
    app_log("mingw sample: compiled with MoniOS-only user libraries");
    write_line("sample complete");
    return 0;
}
