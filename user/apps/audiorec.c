/*
 * audiorec.c -- Monios microphone recorder (Task 1).
 *
 * GUI:
 *   - 录音按钮 (开始/停止): toggles the ES1371 ADC capture.
 *   - 播放按钮: replay the recorded PCM through the DAC.
 *   - 保存按钮: write the recording as a WAV file.
 *   - VU 电平表 + 录音时长 display (live).
 *
 * Recorded format: 16-bit signed, stereo, 44100 Hz, interleaved.
 * Output WAV: C:\Monios\Users\root\Desktop\recording.wav
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "monios_dll.h"
#include "audio.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "syscall.h"

#define EV_CHAR   1
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

#define REC_MAX_BYTES   (4U * 1024U * 1024U)   /* ~23 s at 176 kB/s */
#define REC_CHUNK       4096U

typedef struct {
    uint32_t x, y, w, h;
    const char *label;
} rec_button_t;

static uint8_t g_rec_buf[REC_MAX_BYTES];
static uint32_t g_rec_len;
static bool g_recording;
static uint8_t g_level;
static uint32_t g_frames;

static rec_button_t g_btns[4];
static uint32_t g_btn_count;

static void draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    app_graphics_fill_rect((uint16_t) x, (uint16_t) y, (uint16_t) w, (uint16_t) h, color);
}

static bool hit(const rec_button_t *b, int32_t mx, int32_t my)
{
    return mx >= (int32_t) b->x && mx < (int32_t) (b->x + b->w) &&
           my >= (int32_t) b->y && my < (int32_t) (b->y + b->h);
}

static void build_ui(void)
{
    g_btn_count = 0;
    g_btns[0].x = 40;  g_btns[0].y = 560; g_btns[0].w = 160; g_btns[0].h = 56;
    g_btns[0].label = "Record";
    g_btns[1].x = 230; g_btns[1].y = 560; g_btns[1].w = 160; g_btns[1].h = 56;
    g_btns[1].label = "Play";
    g_btns[2].x = 420; g_btns[2].y = 560; g_btns[2].w = 160; g_btns[2].h = 56;
    g_btns[2].label = "Save WAV";
    g_btns[3].x = 610; g_btns[3].y = 560; g_btns[3].w = 160; g_btns[3].h = 56;
    g_btns[3].label = "Quit";
    g_btn_count = 4;
}

static void draw_ui(void)
{
    uint32_t i;
    char txt[64];
    uint32_t secs = g_frames / 44100U;
    uint32_t mm = secs / 60U;
    uint32_t ss = secs % 60U;

    draw_rect(0, 0, 1024, 768, 0x00202028);
    draw_rect(40, 40, 944, 80, 0x00303040);
    app_graphics_draw_text(60, 60, "Monios 录音器 (Microphone Recorder)", 0x00FFFFFF);

    /* VU meter */
    draw_rect(60, 160, 700, 28, 0x00101018);
    draw_rect(62, 162, (uint32_t) (696U * (g_level > 100 ? 100 : g_level) / 100U), 24,
              g_level > 80 ? 0x00FF4444 : (g_level > 40 ? 0x00FFCC44 : 0x0044DD44));
    app_graphics_draw_text(780, 164, "VU", 0x00AAAAAA);

    /* duration */
    {
        char *p = txt;
        strcpy(p, "Time: "); p += 6;
        if (mm < 10) *p++ = '0';
        { uint32_t v = mm; char t[8]; uint32_t n = 0;
          if (v == 0) t[n++] = '0';
          while (v) { t[n++] = (char)('0' + v % 10U); v /= 10U; }
          while (n) *p++ = t[--n]; }
        *p++ = ':';
        if (ss < 10) *p++ = '0';
        { uint32_t v = ss; char t[8]; uint32_t n = 0;
          if (v == 0) t[n++] = '0';
          while (v) { t[n++] = (char)('0' + v % 10U); v /= 10U; }
          while (n) *p++ = t[--n]; }
        *p = 0;
    }
    app_graphics_draw_text(60, 210, txt, 0x00FFFFFF);

    /* status */
    app_graphics_draw_text(60, 250,
                           g_recording ? "RECORDING..." :
                           (g_rec_len > 0 ? "Ready to play / save" : "Idle"),
                           g_recording ? 0x00FF5555 : 0x00CCCCCC);

    for (i = 0; i < g_btn_count; i++) {
        uint32_t bg = 0x004060A0;
        if (g_recording && i == 0) bg = 0x00AA3030;
        draw_rect(g_btns[i].x, g_btns[i].y, g_btns[i].w, g_btns[i].h, bg);
        app_graphics_draw_text((uint16_t) (g_btns[i].x + 16U),
                               (uint16_t) (g_btns[i].y + 20U),
                               g_btns[i].label, 0x00FFFFFF);
    }
    app_graphics_present();
}

/* Build a 44-byte canonical WAV header. */
static uint32_t build_wav_header(uint8_t *h, uint32_t pcm_bytes)
{
    uint32_t rate = 44100U;
    uint32_t byterate = rate * 2U * 2U;   /* stereo 16-bit */
    uint32_t data_bytes = pcm_bytes;
    uint32_t riff = 36U + data_bytes;

    h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
    h[4] = (uint8_t)(riff); h[5] = (uint8_t)(riff >> 8);
    h[6] = (uint8_t)(riff >> 16); h[7] = (uint8_t)(riff >> 24);
    h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';
    h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1; h[21] = 0;                 /* PCM */
    h[22] = 2; h[23] = 0;                 /* stereo */
    h[24] = (uint8_t) rate; h[25] = (uint8_t)(rate >> 8);
    h[26] = (uint8_t)(rate >> 16); h[27] = (uint8_t)(rate >> 24);
    h[28] = (uint8_t) byterate; h[29] = (uint8_t)(byterate >> 8);
    h[30] = (uint8_t)(byterate >> 16); h[31] = (uint8_t)(byterate >> 24);
    h[32] = 4; h[33] = 0;                 /* block align */
    h[34] = 16; h[35] = 0;                /* bits/sample */
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    h[40] = (uint8_t) data_bytes; h[41] = (uint8_t)(data_bytes >> 8);
    h[42] = (uint8_t)(data_bytes >> 16); h[43] = (uint8_t)(data_bytes >> 24);
    return 44U;
}

static void do_save(void)
{
    static uint8_t out[44U + REC_MAX_BYTES];
    uint32_t hlen = build_wav_header(out, g_rec_len);
    char path[PATH_MAX_LEN];

    if (g_rec_len == 0U) return;
    memcpy(out + hlen, g_rec_buf, g_rec_len);
    strcpy(path, "C:\\Monios\\Users\\root\\Desktop");
    if (!app_file_exists(path)) app_file_mkdir(path);
    strcat(path, "\\recording.wav");
    app_file_write(path, out, hlen + g_rec_len);
}

static void do_record_toggle(void)
{
    audio_record_request_t req;

    memset(&req, 0, sizeof(req));
    if (!g_recording) {
        g_rec_len = 0;
        g_frames = 0;
        g_level = 0;
        req.cmd = AUDIO_REC_CMD_START;
        syscall1(SYS_AUDIO_REC_CTL, (uint64_t) &req);
        g_recording = true;
    } else {
        req.cmd = AUDIO_REC_CMD_STOP;
        syscall1(SYS_AUDIO_REC_CTL, (uint64_t) &req);
        g_recording = false;
    }
}

static void do_play(void)
{
    if (g_rec_len == 0U) return;
    app_audio_play_pcm(g_rec_buf, g_rec_len, 44100U, 2U, 16U);
}

/* Drain new captured PCM into g_rec_buf. */
static void drain_recording(void)
{
    audio_record_request_t req;
    static uint8_t chunk[REC_CHUNK];

    for (;;) {
        memset(&req, 0, sizeof(req));
        req.cmd = AUDIO_REC_CMD_READ;
        req.buffer = chunk;
        req.capacity = REC_CHUNK;
        syscall1(SYS_AUDIO_REC_CTL, (uint64_t) &req);
        if (req.bytes_copied == 0U) break;
        if (g_rec_len + req.bytes_copied > REC_MAX_BYTES) break;
        memcpy(g_rec_buf + g_rec_len, chunk, req.bytes_copied);
        g_rec_len += req.bytes_copied;
    }
    {
        memset(&req, 0, sizeof(req));
        req.cmd = AUDIO_REC_CMD_STATUS;
        syscall1(SYS_AUDIO_REC_CTL, (uint64_t) &req);
        g_level = req.level;
        g_frames = req.frames_total;
    }
}

int main(int argc, char **argv)
{
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;
    bool quit = false;
    (void) argc; (void) argv;

    app_enter_graphics_mode();
    build_ui();

    while (!quit) {
        app_key_event_t ev;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t) &ev) == 1) {
            if (ev.type == EV_ESC) quit = true;
            if (ev.type == EV_CHAR && (ev.ch == 'q' || ev.ch == 'Q')) quit = true;
        }

        if (g_recording) {
            drain_recording();
        }

        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t) mouse.buttons;
            if ((btn & 1) && !(prev_btn & 1)) {
                uint32_t i;
                for (i = 0; i < g_btn_count; i++) {
                    if (hit(&g_btns[i], mouse.x_pixels, mouse.y_pixels)) {
                        if (i == 0) do_record_toggle();
                        else if (i == 1) do_play();
                        else if (i == 2) do_save();
                        else if (i == 3) quit = true;
                    }
                }
            }
            prev_btn = btn;
        }

        draw_ui();
        app_sleep_ticks(2);
    }

    if (g_recording) {
        audio_record_request_t req;
        memset(&req, 0, sizeof(req));
        req.cmd = AUDIO_REC_CMD_STOP;
        syscall1(SYS_AUDIO_REC_CTL, (uint64_t) &req);
    }
    app_exit(0);
    return 0;
}
