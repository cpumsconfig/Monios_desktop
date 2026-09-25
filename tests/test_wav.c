/*
 * test_wav.c — host-side (Windows / MinGW) unit tests for the WAV file
 * header parser used by user/apps/audiotest.c.
 *
 * The parser is reproduced here verbatim from audiotest.c so the header
 * layout can be exercised on the host without the audio driver stack. It
 * constructs in-memory RIFF/WAVE blobs covering the supported formats
 * (8/16-bit PCM, mono/stereo, several sample rates) and the rejection
 * paths (bad magic, non-PCM codec, unsupported bit depth, missing data).
 *
 * Build: gcc -std=gnu17 -fno-builtin -I . -o test_wav.exe test_wav.c
 */

int printf(const char *fmt, ...);

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
    uint32_t pos = 12;
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
        pos = body + chunk_size + (chunk_size & 1u);
    }
    info->valid = info->audio_format == 1 &&
                  info->channels >= 1 && info->channels <= 2 &&
                  info->sample_rate > 0 &&
                  (info->bits_per_sample == 8 || info->bits_per_sample == 16) &&
                  info->data != 0 && info->data_len > 0;
}

/* ============================================================
 *  Harness
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) { g_pass++; }                                            \
        else {                                                             \
            g_fail++;                                                      \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);   \
        }                                                                  \
    } while (0)
static void section(const char *name) { printf("\n== %s ==\n", name); }

/* Build a minimal WAV blob. fmt chunk params come from the caller; an
 * optional "extra" chunk (e.g. fact) is emitted between fmt and data. */
typedef struct {
    uint16_t format;
    uint16_t channels;
    uint32_t rate;
    uint16_t bits;
    bool extra_chunk;
} wav_cfg_t;

static uint32_t build_wav(uint8_t *buf, uint32_t cap, const wav_cfg_t *cfg)
{
    uint32_t pos = 0;
    uint32_t data_len = 64;

    (void) cap;
    memcpy(buf + pos, "RIFF", 4); pos += 4;
    rd_u16(buf); (void) 0;
    pos += 4;                       /* RIFF size (patched below) */
    memcpy(buf + pos, "WAVE", 4); pos += 4;

    /* fmt chunk */
    memcpy(buf + pos, "fmt ", 4); pos += 4;
    rd_u32(buf + pos);
    buf[pos] = 16; buf[pos+1]=0; buf[pos+2]=0; buf[pos+3]=0; pos += 4;
    rd_u16(buf + pos);
    buf[pos] = (uint8_t)(cfg->format & 0xFF); buf[pos+1]=(uint8_t)(cfg->format>>8); pos += 2;
    buf[pos] = (uint8_t)(cfg->channels & 0xFF); buf[pos+1]=(uint8_t)(cfg->channels>>8); pos += 2;
    buf[pos] = (uint8_t)(cfg->rate & 0xFF); buf[pos+1]=(uint8_t)(cfg->rate>>8);
    buf[pos+2]=(uint8_t)(cfg->rate>>16); buf[pos+3]=(uint8_t)(cfg->rate>>24); pos += 4;
    uint32_t frame_bytes = cfg->channels * cfg->bits / 8u;
    uint32_t br = cfg->rate * frame_bytes;
    buf[pos] = (uint8_t)(br & 0xFF); buf[pos+1]=(uint8_t)(br>>8);
    buf[pos+2]=(uint8_t)(br>>16); buf[pos+3]=(uint8_t)(br>>24); pos += 4;
    buf[pos] = (uint8_t)(frame_bytes & 0xFF); buf[pos+1]=(uint8_t)(frame_bytes>>8); pos += 2;
    buf[pos] = (uint8_t)(cfg->bits & 0xFF); buf[pos+1]=(uint8_t)(cfg->bits>>8); pos += 2;

    if (cfg->extra_chunk) {
        memcpy(buf + pos, "fact", 4); pos += 4;
        buf[pos]=4; buf[pos+1]=0; buf[pos+2]=0; buf[pos+3]=0; pos += 4;
        buf[pos]=0; buf[pos+1]=0; buf[pos+2]=0; buf[pos+3]=0; pos += 4;
    }

    /* data chunk */
    memcpy(buf + pos, "data", 4); pos += 4;
    buf[pos] = (uint8_t)(data_len & 0xFF); buf[pos+1]=(uint8_t)(data_len>>8);
    buf[pos+2]=(uint8_t)(data_len>>16); buf[pos+3]=(uint8_t)(data_len>>24); pos += 4;
    for (uint32_t i = 0; i < data_len; i++) {
        buf[pos++] = (uint8_t) (i & 0xFF);
    }

    /* patch RIFF size */
    uint32_t riff_size = pos - 8;
    buf[4] = (uint8_t)(riff_size & 0xFF); buf[5]=(uint8_t)(riff_size>>8);
    buf[6]=(uint8_t)(riff_size>>16); buf[7]=(uint8_t)(riff_size>>24);
    return pos;
}

static void expect_wav(const wav_cfg_t *cfg, bool expect_valid,
                       uint16_t exp_channels, uint32_t exp_rate, uint16_t exp_bits)
{
    static uint8_t buf[512];
    wav_info_t info;
    uint32_t len = build_wav(buf, sizeof(buf), cfg);
    wav_parse(buf, len, &info);
    CHECK(info.valid == expect_valid);
    if (expect_valid) {
        CHECK(info.channels == exp_channels);
        CHECK(info.sample_rate == exp_rate);
        CHECK(info.bits_per_sample == exp_bits);
        CHECK(info.data_len == 64);
        CHECK(info.data != 0);
        CHECK(info.data[0] == 0 && info.data[1] == 1);
    }
}

int main(void)
{
    uint8_t buf[512];
    wav_info_t info;
    printf("Monios WAV parser host test\n");

    section("supported formats accepted");
    {
        wav_cfg_t c16m = { 1, 1, 44100, 16, false };
        expect_wav(&c16m, true, 1, 44100, 16);

        wav_cfg_t c16s = { 1, 2, 44100, 16, false };
        expect_wav(&c16s, true, 2, 44100, 16);

        wav_cfg_t c8m = { 1, 1, 8000, 8, false };
        expect_wav(&c8m, true, 1, 8000, 8);

        wav_cfg_t c16s22 = { 1, 2, 22050, 16, false };
        expect_wav(&c16s22, true, 2, 22050, 16);
    }

    section("extra chunk between fmt and data handled");
    {
        wav_cfg_t cfact = { 1, 2, 48000, 16, true };
        expect_wav(&cfact, true, 2, 48000, 16);
    }

    section("rejection paths");
    {
        /* bad RIFF magic */
        wav_cfg_t cfg = { 1, 1, 44100, 16, false };
        uint32_t len = build_wav(buf, sizeof(buf), &cfg);
        memcpy(buf, "XXXX", 4);
        wav_parse(buf, len, &info);
        CHECK(info.valid == false);

        /* bad WAVE magic */
        len = build_wav(buf, sizeof(buf), &cfg);
        memcpy(buf + 8, "XBAD", 4);
        wav_parse(buf, len, &info);
        CHECK(info.valid == false);

        /* non-PCM codec (IEEE float = 3) */
        wav_cfg_t cfloat = { 3, 2, 44100, 32, false };
        expect_wav(&cfloat, false, 0, 0, 0);

        /* 24-bit PCM unsupported */
        wav_cfg_t c24 = { 1, 1, 44100, 24, false };
        expect_wav(&c24, false, 0, 0, 0);

        /* 3 channels unsupported */
        wav_cfg_t c3ch = { 1, 3, 44100, 16, false };
        expect_wav(&c3ch, false, 0, 0, 0);

        /* truncated buffer (< 44 bytes) rejected */
        len = build_wav(buf, sizeof(buf), &cfg);
        wav_parse(buf, 20, &info);
        CHECK(info.valid == false);
    }

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");
    return g_fail == 0 ? 0 : 1;
}
