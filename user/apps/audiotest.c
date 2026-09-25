/*
 * audiotest.c — Monios audio playback verification tool.
 *
 * Usage:
 *   audiotest play <file.wav>   parse RIFF/WAVE header, submit PCM data
 *   audiotest tone <hz> <ms>    generate a sine wave at <hz> for <ms>
 *   audiotest info              show audio device / playback state
 *
 * play() supports 8-bit and 16-bit PCM, mono and stereo. It reads the whole
 * file into a scratch buffer (tests use small .wav assets), validates the
 * RIFF/fmt /data chunk layout, then hands the PCM payload to the existing
 * SYS_AUDIO_PLAY_PCM (21) entry point via app_audio_play_pcm().
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "string.h"
#include "stdint.h"

#define WAV_SCRATCH_MAX (256u * 1024u)

typedef struct {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    const uint8_t *data;
    uint32_t data_len;
    bool valid;
} wav_info_t;

static uint16_t rd_u16(const uint8_t *p)
{
    return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static bool tag_eq(const uint8_t *p, const char *tag)
{
    return p[0] == (uint8_t) tag[0] && p[1] == (uint8_t) tag[1] &&
           p[2] == (uint8_t) tag[2] && p[3] == (uint8_t) tag[3];
}

static void wav_parse(const uint8_t *buf, uint32_t len, wav_info_t *info)
{
    uint32_t pos = 12; /* skip "RIFF" + size + "WAVE" */

    memset(info, 0, sizeof(*info));
    if (len < 44 || !tag_eq(buf, "RIFF") || !tag_eq(buf + 8, "WAVE")) {
        return;
    }

    while (pos + 8 <= len) {
        const uint8_t *chunk_id = buf + pos;
        uint32_t chunk_size = rd_u32(buf + pos + 4);
        uint32_t body = pos + 8;

        if (body + chunk_size > len) {
            break;
        }
        if (tag_eq(chunk_id, "fmt ") && chunk_size >= 16) {
            info->audio_format = rd_u16(buf + body);
            info->channels = rd_u16(buf + body + 2);
            info->sample_rate = rd_u32(buf + body + 4);
            info->byte_rate = rd_u32(buf + body + 8);
            info->block_align = rd_u16(buf + body + 12);
            info->bits_per_sample = rd_u16(buf + body + 14);
        } else if (tag_eq(chunk_id, "data")) {
            info->data = buf + body;
            info->data_len = chunk_size;
        }
        pos = body + chunk_size + (chunk_size & 1u); /* chunks are word-aligned */
    }

    info->valid = info->audio_format == 1 &&
                  info->channels >= 1 && info->channels <= 2 &&
                  info->sample_rate > 0 &&
                  (info->bits_per_sample == 8 || info->bits_per_sample == 16) &&
                  info->data != 0 && info->data_len > 0;
}

static int cmd_play(const char *path)
{
    static uint8_t scratch[WAV_SCRATCH_MAX];
    wav_info_t info;
    int32_t n;

    n = app_file_read(path, scratch, sizeof(scratch));
    if (n <= 0) {
        fputs("play: unable to read file: ");
        fputs(path);
        fputs("\r\n");
        return 1;
    }

    wav_parse(scratch, (uint32_t) n, &info);
    if (!info.valid) {
        fputs("play: not a supported PCM WAV (need RIFF/WAVE, fmt PCM, "
              "8/16-bit, 1/2 channels)\r\n");
        return 1;
    }

    fputs("playing ");
    fputs(path);
    fputs("\r\n");
    fputs("  sample_rate = ");
    print_uint(info.sample_rate);
    fputs(" Hz\r\n  channels    = ");
    print_uint(info.channels);
    fputs("\r\n  bits/sample = ");
    print_uint(info.bits_per_sample);
    fputs("\r\n  data bytes  = ");
    print_uint(info.data_len);
    fputs("\r\n");

    if (app_audio_play_pcm(info.data, info.data_len, info.sample_rate,
                           info.channels, info.bits_per_sample) != 0) {
        fputs("play: kernel rejected the PCM submit\r\n");
        return 1;
    }
    fputs("play: done\r\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* tone: 22050 Hz, 16-bit mono sine, built from a 256-entry table      */
/* ------------------------------------------------------------------ */
static int16_t g_sine[256];

static void build_sine_table(void)
{
    /* sin(x) via Taylor on [0, pi/2], mirrored over the four quadrants. */
    for (int i = 0; i < 64; i++) {
        double x = (double) i * (1.5707963267948966 / 64.0);
        double x2 = x * x;
        double s = x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0)));
        g_sine[i] = (int16_t) (s * 32767.0);
    }
    for (int i = 0; i < 64; i++) {
        g_sine[64 + i] = g_sine[63 - i];
    }
    for (int i = 0; i < 128; i++) {
        g_sine[128 + i] = (int16_t) -g_sine[i];
    }
}

static uint32_t parse_u32(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t) (*s - '0');
        s++;
    }
    return v;
}

static int cmd_tone(uint32_t freq, uint32_t duration_ms)
{
    const uint32_t rate = 22050u;
    uint32_t samples;
    uint32_t step;
    uint32_t phase = 0;
    int32_t rc;

    if (freq == 0 || freq > rate / 2u) {
        fputs("tone: frequency must be in (0, 11025]\r\n");
        return 1;
    }
    samples = rate * duration_ms / 1000u;
    if (samples == 0 || samples > 65536u) {
        fputs("tone: duration out of range (1..~2970 ms)\r\n");
        return 1;
    }

    build_sine_table();
    static int16_t pcm[65536];
    step = (freq * 256u * 256u) / rate; /* 16.8 fixed-point phase step */

    for (uint32_t i = 0; i < samples; i++) {
        pcm[i] = g_sine[(phase >> 8) & 0xFFu];
        phase += step;
    }

    fputs("tone: ");
    print_uint(freq);
    fputs(" Hz for ");
    print_uint(duration_ms);
    fputs(" ms (");
    print_uint(samples);
    fputs(" samples @ 22050 Hz 16-bit mono)\r\n");

    rc = app_audio_play_pcm(pcm, samples * 2u, rate, 1u, 16u);
    if (rc != 0) {
        fputs("tone: kernel rejected the PCM submit\r\n");
        return 1;
    }
    fputs("tone: done\r\n");
    return 0;
}

static int cmd_info(void)
{
    app_system_status_t st;

    if (app_get_system_status(&st) != 0) {
        fputs("info: unable to query system status\r\n");
        return 1;
    }
    fputs("Monios audio device\r\n");
    fputs("  present  : ");
    fputs(st.audio_present ? "yes" : "no");
    fputs("\r\n  playing  : ");
    fputs(st.audio_playing ? "yes" : "no");
    fputs("\r\n  paused   : ");
    fputs(st.audio_paused ? "yes" : "no");
    fputs("\r\n  volume   : ");
    print_uint(st.audio_volume);
    fputs(" %\r\n  driver   : ");
    fputs(st.audio_driver);
    fputs("\r\n  track    : ");
    fputs(st.audio_track);
    fputs("\r\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("usage:\r\n");
        fputs("  audiotest play <file.wav>\r\n");
        fputs("  audiotest tone <hz> <ms>\r\n");
        fputs("  audiotest info\r\n");
        return 1;
    }

    if (strcmp(argv[1], "play") == 0) {
        if (argc < 3) {
            fputs("play: missing file.wav\r\n");
            return 1;
        }
        return cmd_play(argv[2]);
    }
    if (strcmp(argv[1], "tone") == 0) {
        if (argc < 4) {
            fputs("tone: missing <hz> <ms>\r\n");
            return 1;
        }
        return cmd_tone(parse_u32(argv[2]), parse_u32(argv[3]));
    }
    if (strcmp(argv[1], "info") == 0) {
        return cmd_info();
    }

    fputs("unknown command\r\n");
    return 1;
}
