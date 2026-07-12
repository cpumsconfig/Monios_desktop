#include "appsys.h"
#include "stdio.h"
#include "string.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static bool path_has_suffix(const char *path, const char *suffix)
{
    uint32_t path_len;
    uint32_t suffix_len;

    if (path == 0 || suffix == 0) {
        return false;
    }
    path_len = (uint32_t) strlen(path);
    suffix_len = (uint32_t) strlen(suffix);
    if (suffix_len > path_len) {
        return false;
    }
    return strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

int main(int argc, char **argv)
{
    const char *package_path;

    write_line("rzsinst.elf");
    if (argc < 2 || argv[1] == 0 || argv[1][0] == '\0') {
        write_line("usage: rzsinst <package.rzs>");
        return 1;
    }

    package_path = argv[1];
    fputs("install package: ");
    write_line(package_path);

    if (!path_has_suffix(package_path, ".rzs")) {
        write_line("not an RZS package");
        return 1;
    }
    if (!app_file_exists(package_path) || app_file_is_dir(package_path)) {
        write_line("package not found");
        return 1;
    }

    write_line("requesting installer permission");
    if (!app_request_r2("install RZS package")) {
        write_line("install canceled");
        return 1;
    }

    write_line("starting package installer");
    if (!app_defer_exec(package_path)) {
        write_line("cannot queue package");
        return 1;
    }
    write_line("installer queued");
    return 0;
}
