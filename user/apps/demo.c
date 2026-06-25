#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void write_uint(uint32_t value)
{
    char buffer[16];
    uint32_t index = 0;

    if (value == 0) {
        write(STDOUT_FILENO, "0", 1);
        return;
    }

    while (value > 0 && index < sizeof(buffer)) {
        buffer[index++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (index > 0) {
        write(STDOUT_FILENO, &buffer[--index], 1);
    }
}

static void write_mouse_snapshot(const app_mouse_snapshot_t *snapshot)
{
    fputs("mouse x=");
    write_uint((uint32_t) snapshot->x_pixels);
    fputs(" y=");
    write_uint((uint32_t) snapshot->y_pixels);
    fputs(" buttons=");
    write_uint(snapshot->buttons);
    fputs(" wheel=");
    if (snapshot->wheel_delta < 0) {
        write(STDOUT_FILENO, "-", 1);
        write_uint((uint32_t) (-snapshot->wheel_delta));
    } else {
        write_uint((uint32_t) snapshot->wheel_delta);
    }
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    char cwd[PATH_MAX_LEN];
    app_mouse_snapshot_t snapshot;

    write_line("demo.elf started");
    if (app_getcwd(cwd, sizeof(cwd)) >= 0) {
        fputs("cwd: ");
        write_line(cwd);
    }

    fputs("argc: ");
    write_uint((uint32_t) argc);
    fputs("\r\n");
    for (int i = 0; i < argc; i++) {
        fputs("argv[");
        write_uint((uint32_t) i);
        fputs("]: ");
        write_line(argv[i]);
    }

    if (app_get_mouse(&snapshot) >= 0) {
        write_mouse_snapshot(&snapshot);
    }
    app_enter_graphics_mode();
    app_graphics_fill_rect(340, 210, 344, 180, 0x0017283A);
    app_graphics_fill_rect(356, 226, 312, 148, 0x0038BDF8);
    app_graphics_fill_rect(384, 254, 256, 92, 0x00F8FAFC);
    app_graphics_present();
    write_line("demo drew a panel and exited");
    return 0;
}
