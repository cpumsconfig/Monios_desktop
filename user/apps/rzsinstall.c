#include "appsys.h"
#include "stdio.h"
#include "string.h"

#define SYSINST_COPY_MAX (256U * 1024U)
#define MONIOS_DRIVERS_DIR "C:\\Monios\\driver"

static uint8_t g_driver_copy_buffer[SYSINST_COPY_MAX];

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

static const char *path_basename(const char *path)
{
    const char *base = path;

    if (path == 0) {
        return "";
    }
    while (*path != '\0') {
        if (*path == '\\') {
            base = path + 1;
        }
        path++;
    }
    return base;
}

static bool path_under_drivers(const char *path)
{
    const char prefix[] = MONIOS_DRIVERS_DIR "\\";

    if (path == 0) {
        return false;
    }
    for (uint32_t i = 0; prefix[i] != '\0'; i++) {
        char left = path[i];
        char right = prefix[i];

        if (left >= 'A' && left <= 'Z') {
            left = (char) (left - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

static bool build_driver_path(const char *source_path, char *driver_path, uint32_t driver_path_size)
{
    const char *base = path_basename(source_path);
    uint32_t base_len = (uint32_t) strlen(base);

    if (base_len == 0 || strlen(MONIOS_DRIVERS_DIR) + 1 + base_len + 1 > driver_path_size) {
        return false;
    }
    strcpy(driver_path, MONIOS_DRIVERS_DIR "\\");
    strcpy(driver_path + strlen(driver_path), base);
    return true;
}

static bool install_driver_copy(const char *source_path, char *driver_path, uint32_t driver_path_size)
{
    int size;

    if (path_under_drivers(source_path)) {
        if (strlen(source_path) + 1 > driver_path_size) {
            return false;
        }
        strcpy(driver_path, source_path);
        return true;
    }
    if (!build_driver_path(source_path, driver_path, driver_path_size)) {
        return false;
    }
    if (!app_file_exists("C:\\Monios")) {
        (void) app_file_mkdir("C:\\Monios");
    }
    if (!app_file_exists(MONIOS_DRIVERS_DIR)) {
        (void) app_file_mkdir(MONIOS_DRIVERS_DIR);
    }
    if (!app_file_exists(MONIOS_DRIVERS_DIR) || !app_file_is_dir(MONIOS_DRIVERS_DIR)) {
        write_line("cannot create C:\\Monios\\driver");
        return false;
    }

    size = app_file_size(source_path);
    if (size <= 0 || (uint32_t) size > SYSINST_COPY_MAX) {
        write_line("driver copy too large");
        return false;
    }
    if (app_file_read(source_path, g_driver_copy_buffer, (uint32_t) size) != size) {
        write_line("driver read failed");
        return false;
    }
    if (app_file_write(driver_path, g_driver_copy_buffer, (uint32_t) size) != size) {
        write_line("driver write failed");
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *package_path;
    char driver_path[128];

    write_line("sysinst.exe");
    if (argc < 2 || argv[1] == 0 || argv[1][0] == '\0') {
        write_line("usage: sysinst <driver.sys>");
        return 1;
    }

    package_path = argv[1];
    fputs("install package: ");
    write_line(package_path);

    if (!path_has_suffix(package_path, ".sys")) {
        write_line("not a SYS driver");
        return 1;
    }
    if (!app_file_exists(package_path) || app_file_is_dir(package_path)) {
        write_line("package not found");
        return 1;
    }

    write_line("requesting installer permission");
    if (!app_request_r2("install SYS driver")) {
        write_line("install canceled");
        return 1;
    }

    write_line("installing driver package");
    if (!install_driver_copy(package_path, driver_path, sizeof(driver_path))) {
        write_line("install failed");
        return 1;
    }
    if (!app_registry_set("drivers.boot.count", "1") ||
        !app_registry_set("drivers.boot.0", driver_path)) {
        write_line("registry write failed");
        return 1;
    }
    fputs("registered boot driver: ");
    write_line(driver_path);
    if (app_driver_load(driver_path)) {
        write_line("driver loaded");
    } else {
        write_line("driver registered; reboot to retry load");
    }
    return 0;
}
