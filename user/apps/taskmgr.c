#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void write_u32(uint32_t value)
{
    char temp[10];
    char out[11];
    uint32_t i = 0;
    uint32_t j = 0;

    if (value == 0) {
        fputs("0");
        return;
    }
    while (value > 0) {
        temp[i++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (i > 0) {
        out[j++] = temp[--i];
    }
    out[j] = '\0';
    fputs(out);
}

static void write_u32_line(uint32_t value)
{
    write_u32(value);
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    app_system_status_t status;
    (void) argc;
    (void) argv;

    write_line("taskmgr.elf");
    write_line("MONIOS task manager");
    if (app_get_system_status(&status) >= 0) {
        fputs("tasks: ");
        write_u32_line(status.task_count);
        fputs("net: ");
        write_line(status.net_status);
        fputs("mac: ");
        write_line(status.net_mac);
        fputs("ip: ");
        write_line(status.net_ip);
        fputs("gw: ");
        write_line(status.net_gateway);
        fputs("dns: ");
        write_line(status.net_dns);
        fputs("ping: ");
        write_u32(status.net_ping_replies);
        fputs("/");
        write_u32_line(status.net_ping_requests);
        fputs("audio: ");
        write_line(status.audio_present ? status.audio_driver : "none");
        fputs("track: ");
        write_line(status.audio_playing ? status.audio_track : "stopped");
        fputs("gpu submits: ");
        write_u32_line(status.gpu_submits);
        fputs("gpu presents: ");
        write_u32_line(status.gpu_presents);
        fputs("windows: ");
        write_u32_line(status.wm_windows);
        fputs("terminal lines: ");
        write_u32_line(status.terminal_lines);
        fputs("smp online/logical: ");
        write_u32(status.smp_online_processors);
        fputs("/");
        write_u32_line(status.smp_logical_processors);
    } else {
        write_line("system status unavailable");
    }
    write_line("kernel tasks: keypoll guipoll deskui");
    write_line("apps: run programs appear in terminal output");
    return 0;
}
