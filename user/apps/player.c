/*
 * player.c - Monios media player: WAV music + AVI video playback.
 *
 * Usage:
 *   player                 open the GUI (music playlist)
 *   player <file.wav>      load an audio track
 *   player <file.avi>      load an uncompressed AVI video
 *
 * Video: RIFF/AVI container parser, BI_RGB uncompressed frames.
 * Frames are drawn via app_graphics_fill_rect at a reduced block size.
 * Audio: existing WAV decoder with 10-band EQ (unchanged).
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "windows_dll.h"
#include "osui_dll.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"
#include "syscall.h"
#include "audio.h"

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */
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

static void fmt_uint_local(uint32_t v, char *out, uint32_t cap)
{
    char tmp[12];
    uint32_t n = 0, i = 0;
    if (v == 0) { if (cap > 1) { out[0] = '0'; out[1] = '\0'; } return; }
    while (v > 0 && n < sizeof(tmp)) { tmp[n++] = (char) ('0' + v % 10u); v /= 10u; }
    while (n > 0 && i + 1 < cap) { out[i++] = tmp[--n]; }
    out[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* constants                                                           */
/* ------------------------------------------------------------------ */
#define PLAYER_MAX_TRACKS     32u
#define PLAYER_PATH_MAX       128u
#define PLAYER_PCM_MAX        (2u * 1024u * 1024u)
#define EQ_BANDS              10u
#define AVI_FILE_BUF_SZ       (8u * 1024u * 1024u)  /* 8 MB for AVI */
#define AVI_MAX_FRAMES        512u
#define VIDEO_BLOCK           3u   /* screen pixels per video pixel block */
#define VIDEO_MAX_W           320u
#define VIDEO_MAX_H           240u

static const uint32_t eq_freqs[EQ_BANDS] = {
    31u, 62u, 125u, 250u, 500u, 1000u, 2000u, 4000u, 8000u, 16000u
};

enum {
    MODE_SEQUENTIAL = 0,
    MODE_SINGLE_REPEAT,
    MODE_LIST_REPEAT,
    MODE_RANDOM,
    MODE_COUNT
};

enum {
    APP_MODE_MUSIC = 0,
    APP_MODE_VIDEO
};

/* ------------------------------------------------------------------ */
/* WAV parsing                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    const uint8_t *data;
    uint32_t data_len;
    bool valid;
} wav_info_t;

static void wav_parse(const uint8_t *buf, uint32_t len, wav_info_t *info)
{
    uint32_t pos = 12;
    memset(info, 0, sizeof(*info));
    if (len < 44 || !tag_eq(buf, "RIFF") || !tag_eq(buf + 8, "WAVE")) {
        return;
    }
    while (pos + 8 <= len) {
        uint32_t chunk_size = rd_u32(buf + pos + 4);
        uint32_t body = pos + 8;
        if (body + chunk_size > len) { break; }
        if (tag_eq(buf + pos, "fmt ") && chunk_size >= 16) {
            info->channels = rd_u16(buf + body + 2);
            info->sample_rate = rd_u32(buf + body + 4);
            info->bits_per_sample = rd_u16(buf + body + 14);
        } else if (tag_eq(buf + pos, "data")) {
            info->data = buf + body;
            info->data_len = chunk_size;
        }
        pos = body + chunk_size + (chunk_size & 1u);
    }
    info->valid = info->channels >= 1 && info->channels <= 2 &&
                  info->sample_rate > 0 &&
                  (info->bits_per_sample == 8 || info->bits_per_sample == 16) &&
                  info->data != 0 && info->data_len > 0;
}

/* ------------------------------------------------------------------ */
/* 10-band peaking EQ (unchanged from music player)                    */
/* ------------------------------------------------------------------ */
typedef struct {
    int32_t b0, b1, b2, a1, a2;
    int32_t x1, x2, y1, y2;
} biquad_t;

static biquad_t eq[EQ_BANDS];
static int32_t eq_gain_db[EQ_BANDS];
static uint32_t eq_active_fs;

static double my_sin(double x)
{
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0)));
}
static double my_cos(double x)
{
    double x2 = x * x;
    return 1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0));
}
static double pow10_db(int32_t db)
{
    double r = 1.0;
    double base = 1.059253725;
    if (db >= 0) {
        for (int i = 0; i < db; i++) { r *= base; }
    } else {
        for (int i = 0; i > db; i--) { r /= base; }
    }
    return r;
}

static void biquad_compute(biquad_t *bq, uint32_t freq, uint32_t fs, int32_t gain_db)
{
    double A = pow10_db(gain_db);
    double w0 = 6.28318530718 * (double) freq / (double) fs;
    double cosw = my_cos(w0);
    double sinw = my_sin(w0);
    double alpha = sinw / 2.0;
    double a0;
    double nb0 = (1.0 + alpha * A);
    double nb1 = (-2.0 * cosw);
    double nb2 = (1.0 - alpha * A);
    double na1 = (-2.0 * cosw);
    double na2 = (1.0 - alpha / A);
    a0 = (1.0 + alpha / A);

    bq->b0 = (int32_t) ((nb0 / a0) * 16384.0);
    bq->b1 = (int32_t) ((nb1 / a0) * 16384.0);
    bq->b2 = (int32_t) ((nb2 / a0) * 16384.0);
    bq->a1 = (int32_t) ((na1 / a0) * 16384.0);
    bq->a2 = (int32_t) ((na2 / a0) * 16384.0);
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0;
}

static void eq_recompute(uint32_t fs)
{
    if (fs == eq_active_fs) return;
    eq_active_fs = fs;
    for (uint32_t i = 0; i < EQ_BANDS; i++) {
        biquad_compute(&eq[i], eq_freqs[i], fs, eq_gain_db[i]);
    }
}

static int16_t eq_process_one(biquad_t *bq, int16_t x)
{
    int32_t y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2
                - bq->a1 * bq->y1 - bq->a2 * bq->y2;
    y >>= 14;
    bq->x2 = bq->x1; bq->x1 = x;
    bq->y2 = bq->y1; bq->y1 = y;
    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;
    return (int16_t) y;
}

/* ------------------------------------------------------------------ */
/* AVI parsing                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t microsec_per_frame;  /* dwMicroSecPerFrame */
    uint32_t total_frames;         /* dwTotalFrames */
    uint32_t width;               /* dwWidth */
    uint32_t height;               /* dwHeight */
    uint32_t num_streams;
} avih_t;

typedef struct {
    uint32_t has_video;
    uint32_t has_audio;
    uint32_t video_w;
    uint32_t video_h;
    uint32_t video_bpp;
    uint32_t video_compression;  /* mmioFOURCC */
    uint32_t video_frame_size;  /* suggested buffer size */
} strf_video_t;

typedef struct {
    uint32_t offset;   /* file offset of frame data */
    uint32_t size;     /* frame data size */
} avi_frame_entry_t;

static uint8_t g_avi_buf[AVI_FILE_BUF_SZ];
static uint32_t g_avi_len;
static avih_t g_avih;
static strf_video_t g_strv;
static avi_frame_entry_t g_frames[AVI_MAX_FRAMES];
static uint32_t g_frame_count;
static uint32_t g_avi_valid;

/* Recursively scan RIFF chunks. We look for:
 *   avih (main AVI header)
 *   strl -> strh / strf (stream headers)
 *   movi chunks containing 00dc (video frames)
 *   idx1 (index)
 */
static void avi_parse_movi_frames(const uint8_t *base, uint32_t start, uint32_t end)
{
    uint32_t pos = start;
    while (pos + 8 <= end && g_frame_count < AVI_MAX_FRAMES) {
        uint32_t ckid = rd_u32(base + pos);
        uint32_t cksize = rd_u32(base + pos + 4);
        uint32_t body = pos + 8;
        if (body + cksize > end) break;
        /* '00dc' = video frame chunk (FOURCC little-endian: 0x63643030) */
        if (ckid == 0x63643030u) {
            g_frames[g_frame_count].offset = body;
            g_frames[g_frame_count].size = cksize;
            g_frame_count++;
        }
        pos = body + cksize + (cksize & 1u);
    }
}

static void avi_parse(void)
{
    uint32_t pos;
    memset(&g_avih, 0, sizeof(g_avih));
    memset(&g_strv, 0, sizeof(g_strv));
    g_frame_count = 0;
    g_avi_valid = 0;

    if (g_avi_len < 12) return;
    if (!tag_eq(g_avi_buf, "RIFF") || !tag_eq(g_avi_buf + 8, "AVI ")) return;

    pos = 12;
    while (pos + 8 <= g_avi_len) {
        uint32_t ckid = rd_u32(g_avi_buf + pos);
        uint32_t cksize = rd_u32(g_avi_buf + pos + 4);
        uint32_t body = pos + 8;
        uint32_t chunk_end = body + cksize;
        if (chunk_end > g_avi_len) break;

        /* LIST chunk? */
        if (ckid == 0x5453494Cu) {  /* "LIST" */
            if (body + 4 <= chunk_end) {
                uint32_t list_type = rd_u32(g_avi_buf + body);
                /* "hdrl" = header list */
                if (list_type == 0x6C726468u) {
                    uint32_t sub = body + 4;
                    while (sub + 8 <= chunk_end) {
                        uint32_t sub_id = rd_u32(g_avi_buf + sub);
                        uint32_t sub_sz = rd_u32(g_avi_buf + sub + 4);
                        uint32_t sub_body = sub + 8;
                        uint32_t sub_end = sub_body + sub_sz;
                        if (sub_end > chunk_end) break;

                        if (sub_id == 0x68697661u) {  /* "avih" */
                            if (sub_sz >= 40) {
                                g_avih.microsec_per_frame = rd_u32(g_avi_buf + sub_body);
                                g_avih.total_frames = rd_u32(g_avi_buf + sub_body + 16);
                                g_avih.width = rd_u32(g_avi_buf + sub_body + 32);
                                g_avih.height = rd_u32(g_avi_buf + sub_body + 36);
                                g_avih.num_streams = rd_u32(g_avi_buf + sub_body + 28);
                            }
                        } else if (sub_id == 0x4C525453u) {  /* "strl" */
                            uint32_t ss = sub_body;
                            uint32_t strh_found = 0;
                            uint32_t strh_type = 0;
                            while (ss + 8 <= sub_end) {
                                uint32_t ss_id = rd_u32(g_avi_buf + ss);
                                uint32_t ss_sz = rd_u32(g_avi_buf + ss + 4);
                                uint32_t ss_body = ss + 8;
                                uint32_t ss_end = ss_body + ss_sz;
                                if (ss_end > sub_end) break;

                                if (ss_id == 0x68727473u) {  /* "strh" */
                                    if (ss_sz >= 56) {
                                        strh_type = rd_u32(g_avi_buf + ss_body);
                                        strh_found = 1;
                                    }
                                } else if (ss_id == 0x66727473u) {  /* "strf" */
                                    /* If video stream (type 0x73646976 = "vids") */
                                    if (strh_type == 0x73646976u && ss_sz >= 40) {
                                        g_strv.has_video = 1;
                                        g_strv.video_w = rd_u32(g_avi_buf + ss_body + 4);
                                        g_strv.video_h = rd_u32(g_avi_buf + ss_body + 8);
                                        g_strv.video_bpp = rd_u16(g_avi_buf + ss_body + 14);
                                        g_strv.video_compression = rd_u32(g_avi_buf + ss_body + 16);
                                        g_strv.video_frame_size = rd_u32(g_avi_buf + ss_body + 20);
                                    } else if (strh_type == 0x73647561u) {  /* "auds" */
                                        g_strv.has_audio = 1;
                                    }
                                }
                                ss = ss_end + (ss_sz & 1u);
                            }
                        }
                        sub = sub_end + (sub_sz & 1u);
                    }
                } else if (list_type == 0x69766F6Du) {  /* "movi" */
                    avi_parse_movi_frames(g_avi_buf, body + 4, chunk_end);
                }
            }
        } else if (ckid == 0x31786469u) {  /* "idx1" */
            /* Parse index entries: each entry is 16 bytes (flags, offset, size) */
            uint32_t i;
            uint32_t n = cksize / 16;
            for (i = 0; i < n && g_frame_count < AVI_MAX_FRAMES; i++) {
                uint32_t entry = body + i * 16;
                uint32_t flags = rd_u32(g_avi_buf + entry);
                uint32_t offset = rd_u32(g_avi_buf + entry + 4);
                uint32_t size = rd_u32(g_avi_buf + entry + 8);
                /* ch FourCC is at entry+0 but we already know it's 00dc from movi scan
                 * if we already have frames, skip; otherwise collect from idx1 */
                if (g_frame_count == 0 && (flags & 0x10u)) {  /* keyframe */
                    g_frames[g_frame_count].offset = 12 + offset; /* relative to movi base */
                    g_frames[g_frame_count].size = size;
                    g_frame_count++;
                }
            }
        }

        pos = chunk_end + (cksize & 1u);
    }

    /* Validate: need video stream and at least one frame */
    if (!g_strv.has_video || g_frame_count == 0) return;
    if (g_strv.video_bpp != 24 && g_strv.video_bpp != 16) return;
    if (g_strv.video_compression != 0) return;  /* BI_RGB only */

    g_avi_valid = 1;
}

/* Draw one video frame at position (ox, oy) on screen */
static void avi_draw_frame(const uint8_t *frame, uint32_t fw, uint32_t fh,
                           uint32_t bpp, uint16_t ox, uint16_t oy)
{
    uint32_t row_bytes;
    uint32_t x, y;
    /* BMP rows are bottom-up, padded to 4 bytes */
    if (bpp == 24) {
        row_bytes = (fw * 3u + 3u) & ~3u;
    } else {
        row_bytes = fw * 2u;
    }

    for (y = 0; y < fh; y++) {
        /* bottom-up: row 0 is at the bottom */
        const uint8_t *row = frame + (fh - 1u - y) * row_bytes;
        for (x = 0; x < fw; x++) {
            uint32_t color;
            uint8_t r, g, b;
            if (bpp == 24) {
                b = row[x * 3u + 0];
                g = row[x * 3u + 1];
                r = row[x * 3u + 2];
            } else {
                uint16_t px = (uint16_t) (row[x * 2u] | (row[x * 2u + 1] << 8));
                r = (uint8_t) (((px >> 11) & 0x1F) << 3);
                g = (uint8_t) (((px >> 5) & 0x3F) << 2);
                b = (uint8_t) ((px & 0x1F) << 3);
            }
            color = 0x00000000u | ((uint32_t) r << 16) |
                    ((uint32_t) g << 8) | (uint32_t) b;
            app_graphics_fill_rect((uint16_t) (ox + x * VIDEO_BLOCK),
                                   (uint16_t) (oy + y * VIDEO_BLOCK),
                                   VIDEO_BLOCK, VIDEO_BLOCK, color);
        }
    }
}

/* ------------------------------------------------------------------ */
/* playlist state (music)                                              */
/* ------------------------------------------------------------------ */
static char g_tracks[PLAYER_MAX_TRACKS][PLAYER_PATH_MAX];
static uint32_t g_track_count;
static int32_t g_current = -1;
static uint32_t g_mode = MODE_SEQUENTIAL;
static bool g_playing;
static bool g_paused;
static uint32_t g_volume = 80;

static int16_t g_pcm[PLAYER_PCM_MAX / 2u];
static uint32_t g_pcm_frames;
static uint32_t g_pcm_rate;

static char g_page_title[64] = "No track";
static bool g_show_eq;

/* video state */
static uint32_t g_app_mode = APP_MODE_MUSIC;
static uint32_t g_video_frame;       /* current frame index */
static bool g_video_playing;
static bool g_video_paused;
static uint64_t g_last_frame_tick;
static char g_video_info[128];

static int32_t load_track(const char *path)
{
    static uint8_t raw[PLAYER_PCM_MAX];
    wav_info_t info;
    int32_t n;
    uint32_t i;

    const char *dot = strrchr(path, '.');
    if (dot == NULL || strcasecmp(dot, ".wav") != 0) {
        return -1;
    }

    n = app_file_read(path, raw, sizeof(raw));
    if (n <= 0) return -1;
    wav_parse(raw, (uint32_t) n, &info);
    if (!info.valid) return -1;

    eq_recompute(info.sample_rate);
    g_pcm_frames = info.data_len / (info.channels * (info.bits_per_sample / 8u));
    g_pcm_rate = info.sample_rate;
    {
        int16_t *out = g_pcm;
        uint32_t frame;
        for (frame = 0; frame < g_pcm_frames; frame++) {
            int32_t l = 0, r = 0;
            if (info.channels == 1) {
                if (info.bits_per_sample == 16) {
                    l = (int16_t) (info.data[frame * 2] | (info.data[frame * 2 + 1] << 8));
                } else {
                    l = ((int32_t) info.data[frame] - 128) * 256;
                }
                r = l;
            } else {
                if (info.bits_per_sample == 16) {
                    l = (int16_t) (info.data[frame * 4] | (info.data[frame * 4 + 1] << 8));
                    r = (int16_t) (info.data[frame * 4 + 2] | (info.data[frame * 4 + 3] << 8));
                } else {
                    l = ((int32_t) info.data[frame * 2] - 128) * 256;
                    r = ((int32_t) info.data[frame * 2 + 1] - 128) * 256;
                }
            }
            for (i = 0; i < EQ_BANDS; i++) l = eq_process_one(&eq[i], (int16_t) l);
            for (i = 0; i < EQ_BANDS; i++) r = eq_process_one(&eq[i], (int16_t) r);
            *out++ = (int16_t) l;
            *out++ = (int16_t) r;
        }
        g_pcm_frames = frame;
    }
    return 0;
}

static void start_current(void)
{
    if (g_current < 0 || g_current >= (int32_t) g_track_count) return;
    if (load_track(g_tracks[g_current]) != 0) {
        strcpy(g_page_title, "unsupported format");
        return;
    }
    strcpy(g_page_title, g_tracks[g_current]);
    {
        uint32_t bytes = g_pcm_frames * 2u * sizeof(int16_t);
        if (app_audio_play_pcm(g_pcm, bytes, g_pcm_rate, 2, 16) == 0) {
            g_playing = true;
            g_paused = false;
        }
    }
}

static void player_stop(void)
{
    audio_player_ctl_request_t r;
    memset(&r, 0, sizeof(r));
    r.cmd = 1u;
    monios_syscall1(SYS_AUDIO_PLAYER_CTL, (uint64_t) &r);
    g_playing = false;
    g_paused = false;
}

static void player_pause_toggle(void)
{
    audio_player_ctl_request_t r;
    memset(&r, 0, sizeof(r));
    r.cmd = 0u;
    monios_syscall1(SYS_AUDIO_PLAYER_CTL, (uint64_t) &r);
    g_paused = !g_paused;
}

/* Load an AVI file for video playback */
static int32_t load_avi(const char *path)
{
    int32_t n = app_file_read(path, g_avi_buf, sizeof(g_avi_buf));
    if (n <= 0) return -1;
    g_avi_len = (uint32_t) n;
    avi_parse();
    if (!g_avi_valid) return -1;

    g_video_frame = 0;
    g_video_playing = true;
    g_video_paused = false;
    g_last_frame_tick = app_ticks();

    /* build info string */
    g_video_info[0] = '\0';
    strcat(g_video_info, "AVI: ");
    {
        char tmp[16];
        fmt_uint_local(g_strv.video_w, tmp, sizeof(tmp));
        strcat(g_video_info, tmp);
        strcat(g_video_info, "x");
        fmt_uint_local(g_strv.video_h, tmp, sizeof(tmp));
        strcat(g_video_info, tmp);
        strcat(g_video_info, " ");
    }
    fmt_uint_local(g_strv.video_bpp, (char *)g_page_title, sizeof(g_page_title));
    strcat(g_page_title, "bpp  frames: ");
    {
        char tmp[16];
        fmt_uint_local(g_frame_count, tmp, sizeof(tmp));
        strcat(g_page_title, tmp);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* playlist loading                                                    */
/* ------------------------------------------------------------------ */
static void playlist_add(const char *path)
{
    if (g_track_count >= PLAYER_MAX_TRACKS) return;
    strncpy(g_tracks[g_track_count], path, PLAYER_PATH_MAX - 1u);
    g_tracks[g_track_count][PLAYER_PATH_MAX - 1u] = '\0';
    g_track_count++;
}

static bool is_audio_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return false;
    return strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".mp3") == 0 ||
           strcasecmp(dot, ".ogg") == 0;
}

static bool is_video_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return false;
    return strcasecmp(dot, ".avi") == 0;
}

static void load_dir(const char *dir)
{
    static char list[2048];
    uint32_t i = 0;
    list[0] = '\0';
    if (app_file_list_dir(dir, list, sizeof(list)) <= 0) return;
    while (list[i] != '\0') {
        char entry[96];
        uint32_t j = 0;
        bool is_dir = false;
        while (list[i] != '\n' && list[i] != '\0' && j + 1 < sizeof(entry)) {
            entry[j] = list[i];
            if (list[i] == '/') is_dir = true;
            j++; i++;
        }
        entry[j] = '\0';
        if (list[i] == '\n') i++;
        if (!is_dir && is_audio_ext(entry)) {
            char full[PLAYER_PATH_MAX];
            strcpy(full, dir);
            strcat(full, "\\");
            {
                uint32_t k = 0;
                while (entry[k] != '\0') k++;
                if (k > 0 && entry[k - 1] == '/') entry[k - 1] = '\0';
            }
            strcat(full, entry);
            playlist_add(full);
        }
    }
}

/* ------------------------------------------------------------------ */
/* GUI                                                                 */
/* ------------------------------------------------------------------ */
static bool hit(app_mouse_snapshot_t *m, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    return m->x_pixels >= x && m->x_pixels < x + w &&
           m->y_pixels >= y && m->y_pixels < y + h;
}

static const char *mode_name(uint32_t m)
{
    switch (m) {
        case MODE_SINGLE_REPEAT: return "Repeat One";
        case MODE_LIST_REPEAT:   return "Repeat All";
        case MODE_RANDOM:        return "Shuffle";
        default:                 return "Sequential";
    }
}

int main(int argc, char **argv)
{
    app_mouse_snapshot_t m;
    uint8_t prev_buttons = 0;

    for (uint32_t i = 0; i < EQ_BANDS; i++) eq_gain_db[i] = 0;
    g_track_count = 0;
    g_current = -1;

    if (argc >= 2) {
        if (is_video_ext(argv[1])) {
            g_app_mode = APP_MODE_VIDEO;
            if (load_avi(argv[1]) != 0) {
                strcpy(g_page_title, "Cannot open AVI");
            }
        } else if (app_file_is_dir(argv[1])) {
            load_dir(argv[1]);
        } else if (is_audio_ext(argv[1])) {
            playlist_add(argv[1]);
        }
    }

    if (g_app_mode == APP_MODE_MUSIC && g_track_count == 0) {
        static char pl[4096];
        if (app_file_read("C:\\Users\\root\\Music\\playlist.m3u", pl, sizeof(pl)) > 0) {
            uint32_t i = 0;
            while (pl[i] != '\0') {
                char line[PLAYER_PATH_MAX];
                uint32_t j = 0;
                while (pl[i] != '\n' && pl[i] != '\r' && pl[i] != '\0' && j + 1 < sizeof(line)) {
                    line[j++] = pl[i++];
                }
                line[j] = '\0';
                if (pl[i] == '\r' || pl[i] == '\n') i++;
                if (line[0] != '#' && line[0] != '\0') playlist_add(line);
            }
        }
    }
    if (g_app_mode == APP_MODE_MUSIC && g_track_count == 0) {
        playlist_add("C:\\Monios\\Users\\root\\Desktop\\bgm.wav");
    }

    app_enter_graphics_mode();

    for (;;) {
        osui_canvas(0x0020242c);
        osui_titlebar((osui_rect_t) { 40, 24, 720, 36 },
                      g_app_mode == APP_MODE_VIDEO ? "Monios Video Player" : "Monios Player",
                      true);

        if (g_app_mode == APP_MODE_VIDEO && g_avi_valid) {
            /* ---- VIDEO MODE ---- */
            /* video display area */
            uint16_t vx = 60, vy = 72;
            uint16_t vw = (uint16_t) (g_strv.video_w * VIDEO_BLOCK);
            uint16_t vh = (uint16_t) (g_strv.video_h * VIDEO_BLOCK);
            if (vw > 680) vw = 680;
            if (vh > 360) vh = 360;

            /* black backdrop */
            app_graphics_fill_rect(vx - 4, vy - 4, (uint16_t)(vw + 8), (uint16_t)(vh + 8),
                                   0x00000000);

            /* draw current frame */
            if (g_video_frame < g_frame_count) {
                avi_frame_entry_t *fe = &g_frames[g_video_frame];
                if (fe->offset + fe->size <= g_avi_len) {
                    avi_draw_frame(g_avi_buf + fe->offset,
                                   g_strv.video_w, g_strv.video_h,
                                   g_strv.video_bpp, vx, vy);
                }
            }

            /* info bar */
            {
                char info[128];
                info[0] = '\0';
                strcpy(info, "Frame ");
                {
                    char tmp[16];
                    fmt_uint_local(g_video_frame + 1, tmp, sizeof(tmp));
                    strcat(info, tmp);
                    strcat(info, "/");
                    fmt_uint_local(g_frame_count, tmp, sizeof(tmp));
                }
                osui_label(60, (uint16_t)(vy + vh + 10), info, true);
            }
            osui_label(60, (uint16_t)(vy + vh + 30), g_page_title, false);

            /* playback controls */
            osui_button_state((osui_rect_t) { 60, (uint16_t)(vy + vh + 60), 60, 36 },
                              g_video_paused ? "Play" : "Pause",
                              OSUI_BUTTON_PRIMARY, OSUI_STATE_PRIMARY);
            osui_button_state((osui_rect_t) { 130, (uint16_t)(vy + vh + 60), 60, 36 },
                              "Stop", OSUI_BUTTON_DANGER, 0);

            /* progress bar */
            {
                uint32_t pct = g_frame_count > 0 ?
                    (g_video_frame * 100u / g_frame_count) : 0;
                osui_progress((osui_rect_t) { 210, (uint16_t)(vy + vh + 68), 400, 20 },
                              pct);
            }

            /* volume slider */
            osui_label(620, (uint16_t)(vy + vh + 72), "Vol", true);
            osui_slider((osui_rect_t) { 660, (uint16_t)(vy + vh + 66), 100, 24 },
                        g_volume, 0);

            /* video timing */
            if (g_video_playing && !g_video_paused) {
                uint64_t now = app_ticks();
                uint64_t frame_ticks;
                /* dwMicroSecPerFrame is microseconds; ticks are ~100Hz (10ms) */
                if (g_avih.microsec_per_frame > 0) {
                    frame_ticks = g_avih.microsec_per_frame / 10000u;
                } else {
                    frame_ticks = 10u; /* default 10fps */
                }
                if (frame_ticks < 1) frame_ticks = 1;
                if (now - g_last_frame_tick >= frame_ticks) {
                    g_last_frame_tick = now;
                    g_video_frame++;
                    if (g_video_frame >= g_frame_count) {
                        g_video_frame = 0;  /* loop */
                    }
                }
            }
        } else if (g_app_mode == APP_MODE_VIDEO) {
            /* video error state */
            osui_card((osui_rect_t) { 60, 80, 680, 200 });
            osui_label(80, 100, "Cannot open video file", false);
            osui_label(80, 130, g_page_title, true);
            osui_label(80, 170, "Supported: uncompressed AVI (RIFF), BI_RGB 16/24bpp", true);
            osui_label(80, 200, "Usage: player <file.avi>", true);
        } else {
            /* ---- MUSIC MODE (original) ---- */
            osui_card((osui_rect_t) { 40, 72, 720, 120 });
            {
                uint32_t i;
                for (i = 0; i < 24; i++) {
                    uint32_t h = 10u + ((i * 37u + (uint32_t) app_ticks() / 20u) % 60u);
                    windows_fill_rect(60 + i * 28, 180 - (int) h, 20, (int) h, 0xff40a0ff);
                }
            }

            osui_label(60, 204, g_page_title, false);
            {
                char modebuf[32];
                strcpy(modebuf, "Mode: ");
                strcat(modebuf, mode_name(g_mode));
                osui_label(60, 224, modebuf, true);
            }

            osui_button_state((osui_rect_t) { 60, 250, 60, 36 }, "|<", 0, 0);
            osui_button_state((osui_rect_t) { 130, 250, 60, 36 }, g_paused ? "Play" : "Pause",
                              OSUI_BUTTON_PRIMARY, OSUI_STATE_PRIMARY);
            osui_button_state((osui_rect_t) { 200, 250, 60, 36 }, ">|", 0, 0);
            osui_button_state((osui_rect_t) { 270, 250, 60, 36 }, "Stop", OSUI_BUTTON_DANGER, 0);
            osui_button_state((osui_rect_t) { 340, 250, 90, 36 }, "Mode", 0, 0);

            osui_label(450, 258, "Vol", true);
            osui_slider((osui_rect_t) { 490, 252, 180, 24 }, g_volume, 0);

            osui_tabbar((osui_rect_t) { 40, 296, 720, 28 },
                        (const char *[]) { "Playlist", "Equalizer" }, 2,
                        g_show_eq ? 1u : 0u);

            if (!g_show_eq) {
                uint32_t i;
                for (i = 0; i < g_track_count && i < 12u; i++) {
                    uint32_t state = (i == (uint32_t) g_current) ? OSUI_STATE_SELECTED : 0u;
                    char base[PLAYER_PATH_MAX];
                    const char *slash = strrchr(g_tracks[i], '\\');
                    strcpy(base, slash != NULL ? slash + 1 : g_tracks[i]);
                    osui_list_item((osui_rect_t) { 60, 336 + i * 40, 680, 36 },
                                   base, g_tracks[i], state);
                }
            } else {
                uint32_t i;
                for (i = 0; i < EQ_BANDS; i++) {
                    char label[16];
                    fmt_uint_local(eq_freqs[i], label, sizeof(label));
                    uint32_t pos = (uint32_t) (eq_gain_db[i] + 12);
                    osui_label(60, 340 + i * 26, label, true);
                    osui_slider((osui_rect_t) { 110, 336 + i * 26, 300, 20 }, pos, 0);
                }
                osui_button_state((osui_rect_t) { 440, 340, 120, 30 }, "Flat", 0, 0);
                osui_button_state((osui_rect_t) { 440, 376, 120, 30 }, "Bass Boost", 0, 0);
                osui_button_state((osui_rect_t) { 440, 412, 120, 30 }, "Rock", 0, 0);
                osui_button_state((osui_rect_t) { 440, 448, 120, 30 }, "Vocal", 0, 0);
            }
        }

        osui_present();

        /* ---- input ---- */
        app_get_mouse(&m);
        if ((m.buttons & 1u) && prev_buttons == 0) {
            if (g_app_mode == APP_MODE_VIDEO && g_avi_valid) {
                uint16_t vy = 72;
                uint16_t vh = (uint16_t) (g_strv.video_h * VIDEO_BLOCK);
                if (vh > 360) vh = 360;
                /* play/pause */
                if (hit(&m, 60, (uint16_t)(vy + vh + 60), 60, 36)) {
                    g_video_paused = !g_video_paused;
                } else if (hit(&m, 130, (uint16_t)(vy + vh + 60), 60, 36)) {
                    g_video_frame = 0;
                    g_video_paused = true;
                } else if (hit(&m, 210, (uint16_t)(vy + vh + 68), 400, 20)) {
                    /* click progress bar to seek */
                    uint32_t pct = (uint32_t)(m.x_pixels - 210) * 100u / 400u;
                    if (pct > 100) pct = 100;
                    g_video_frame = pct * g_frame_count / 100u;
                }
            } else if (g_app_mode == APP_MODE_MUSIC) {
                if (hit(&m, 60, 250, 60, 36)) {
                    g_current = (g_current <= 0) ? (int32_t) g_track_count - 1 : g_current - 1;
                    start_current();
                } else if (hit(&m, 130, 250, 60, 36)) {
                    player_pause_toggle();
                } else if (hit(&m, 200, 250, 60, 36)) {
                    g_current = (g_current + 1) % (int32_t) g_track_count;
                    start_current();
                } else if (hit(&m, 270, 250, 60, 36)) {
                    player_stop();
                    g_playing = false;
                } else if (hit(&m, 340, 250, 90, 36)) {
                    g_mode = (g_mode + 1u) % MODE_COUNT;
                } else if (hit(&m, 40, 296, 360, 28)) {
                    g_show_eq = false;
                } else if (hit(&m, 400, 296, 360, 28)) {
                    g_show_eq = true;
                } else if (!g_show_eq) {
                    uint32_t i;
                    for (i = 0; i < g_track_count && i < 12u; i++) {
                        if (hit(&m, 60, (uint16_t) (336 + i * 40), 680, 36)) {
                            g_current = (int32_t) i;
                            start_current();
                            break;
                        }
                    }
                } else {
                    uint32_t i;
                    if (hit(&m, 440, 340, 120, 30)) {
                        for (i = 0; i < EQ_BANDS; i++) eq_gain_db[i] = 0;
                    } else if (hit(&m, 440, 376, 120, 30)) {
                        for (i = 0; i < EQ_BANDS; i++)
                            eq_gain_db[i] = (i < 4) ? 8 : ((i > 7) ? 2 : 0);
                    } else if (hit(&m, 440, 412, 120, 30)) {
                        for (i = 0; i < EQ_BANDS; i++)
                            eq_gain_db[i] = (i < 3) ? 6 : ((i > 6) ? 6 : -2);
                    } else if (hit(&m, 440, 448, 120, 30)) {
                        for (i = 0; i < EQ_BANDS; i++)
                            eq_gain_db[i] = (i >= 4 && i <= 6) ? 6 : -3;
                    }
                }
            }
        }
        prev_buttons = m.buttons & 1u;

        /* auto-advance music when track finishes */
        if (g_app_mode == APP_MODE_MUSIC) {
            audio_player_ctl_request_t st;
            memset(&st, 0, sizeof(st));
            st.cmd = 2u;
            monios_syscall1(SYS_AUDIO_PLAYER_CTL, (uint64_t) &st);
            if (g_playing && !st.playing) {
                if (g_mode == MODE_SINGLE_REPEAT) {
                    start_current();
                } else {
                    g_current = (g_current + 1) % (int32_t) g_track_count;
                    start_current();
                }
            }
        }

        app_sleep_ticks(1);
    }
    return 0;
}
