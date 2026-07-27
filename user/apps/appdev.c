#include "appsys.h"
#include "stdio.h"
#include "string.h"

#define APPDEV_VERSION_PATH "C:\\Monios\\System\\version.txt"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void write_label_u32(const char *label, uint32_t value)
{
    fputs(label);
    print_uint(value);
    fputs("\r\n");
}

static void write_flag(uint32_t flags, uint32_t bit, const char *name)
{
    if ((flags & bit) != 0) {
        fputs("  ");
        write_line(name);
    }
}

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    app_system_status_t status;
    char cwd[PATH_MAX_LEN];
    char version[80];
    char default_app[PATH_MAX_LEN];
    int version_size;

    write_line("appdev.exe");
    write_line("MoniOS app API sample");
    app_log("appdev: launched through app API");

    if (argc > 1 && strcmp(argv[1], "badptr") == 0) {
        write_line("triggering bad user buffer test");
        (void) app_file_read(APPDEV_VERSION_PATH, (void *) 0x1000, 16);
        write_line("bad pointer was not isolated");
        return 1;
    }

    if (info != 0) {
        write_label_u32("abi: ", info->abi_version);
        write_label_u32("privilege: R", info->privilege_level);
        fputs("user: ");
        write_line(info->user_name);
        fputs("program: ");
        write_line(info->program_path);
        printf("image flags: 0x%x\r\n", info->image_flags);
        write_flag(info->image_flags, APP_IMAGE_FLAG_GUI, "gui");
        write_flag(info->image_flags, APP_IMAGE_FLAG_CONSOLE, "console");
        write_flag(info->image_flags, APP_IMAGE_FLAG_DRIVER, "driver");
        write_flag(info->image_flags, APP_IMAGE_FLAG_NEEDS_R2, "needs R2/UAC");
        write_flag(info->image_flags, APP_IMAGE_FLAG_RESOURCE_TABLE, "resource table");
        write_flag(info->image_flags, APP_IMAGE_FLAG_ICON_RESOURCE, "icon resource");
        write_flag(info->image_flags, APP_IMAGE_FLAG_MANIFEST_RESOURCE, "manifest resource");
    }

    if (app_getcwd(cwd, sizeof(cwd)) >= 0) {
        fputs("cwd: ");
        write_line(cwd);
    }

    if (app_get_system_status(&status) >= 0) {
        write_label_u32("kernel tasks: ", status.task_count);
        write_label_u32("processes: ", status.process_count);
        if (status.current_pid >= 0) {
            write_label_u32("current pid: ", (uint32_t) status.current_pid);
        }
    }

    if (app_default_app_get(".sys", default_app, sizeof(default_app))) {
        fputs("default .sys app: ");
        write_line(default_app);
    }

    version_size = app_file_read(APPDEV_VERSION_PATH, version, sizeof(version) - 1);
    if (version_size > 0) {
        version[version_size] = '\0';
        fputs("version: ");
        write_line(version);
    }

    write_label_u32("ticks: ", (uint32_t) app_ticks());
    app_sleep_ticks(2);
    write_line("api sample complete");
    return 0;
}
