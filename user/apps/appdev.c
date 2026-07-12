#include "appsys.h"
#include "stdio.h"
#include "string.h"

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

int main(int argc, char **argv)
{
    const app_launch_info_t *info = app_launch_info();
    app_system_status_t status;
    char cwd[PATH_MAX_LEN];
    char version[80];
    char default_app[PATH_MAX_LEN];
    int version_size;

    write_line("appdev.elf");
    write_line("MoniOS app API sample");
    app_log("appdev: launched through app API");

    if (argc > 1 && strcmp(argv[1], "badptr") == 0) {
        write_line("triggering bad user buffer test");
        (void) app_file_read("/version.txt", (void *) 0x1000, 16);
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

    if (app_default_app_get(".rzs", default_app, sizeof(default_app))) {
        fputs("default .rzs app: ");
        write_line(default_app);
    }

    version_size = app_file_read("/version.txt", version, sizeof(version) - 1);
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
