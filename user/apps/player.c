#include "stdio.h"
#include "unistd.h"
#include "appsys.h"

#define DEFAULT_TRACK "/music.wav"

static void write_line(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

int main(int argc, char **argv)
{
    const char *path = DEFAULT_TRACK;
    app_system_status_t status;

    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        path = argv[1];
    }
    write_line("player.elf");
    fputs("track: ");
    write_line(path);
    if (app_audio_play_file(path) == 0) {
        write_line("playback started");
    } else {
        write_line("playback failed");
        return 1;
    }
    if (app_get_system_status(&status) >= 0) {
        fputs("driver: ");
        write_line(status.audio_driver);
        fputs("now: ");
        write_line(status.audio_track);
    }
    return 0;
}
