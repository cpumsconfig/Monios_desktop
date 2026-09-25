#ifndef _AUDIO_H_
#define _AUDIO_H_

#include "stdbool.h"
#include "stdint.h"

typedef enum {
    AUDIO_DEVICE_NONE = 0,
    AUDIO_DEVICE_SB16,
    AUDIO_DEVICE_AC97,
    AUDIO_DEVICE_HDA,
    AUDIO_DEVICE_ES1371
} audio_device_kind_t;

typedef struct {
    audio_device_kind_t kind;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t mixer_base;
    uint32_t bus_master_base;
    uint8_t irq_line;
    bool onboard;
    uint16_t ac97_ext_audio_id;
    uint16_t ac97_status;
    bool variable_rate_audio;
    bool present;
} audio_device_info_t;

#define AUDIO_PCM_MAX_BYTES (48U * 1024U * 1024U)

/* ---------------------------------------------------------------------- */
/* Feature 11: audio mixer (syscall 55)                                    */
/* ---------------------------------------------------------------------- */
#define AUDIO_MIXER_MAX_APPS 16U

typedef enum {
    AUDIO_MIXER_GET_MASTER = 0,
    AUDIO_MIXER_SET_MASTER,
    AUDIO_MIXER_GET_MUTE,
    AUDIO_MIXER_SET_MUTE,
    AUDIO_MIXER_SET_APP_VOLUME,
    AUDIO_MIXER_GET_APP_VOLUME,
    AUDIO_MIXER_SET_OUTPUT_DEVICE,
    AUDIO_MIXER_GET_OUTPUT_DEVICE,
    AUDIO_MIXER_ENUM_DEVICES
} audio_mixer_cmd_t;

typedef struct {
    uint32_t cmd;        /* in: audio_mixer_cmd_t */
    int32_t  pid;        /* in: target process (per-app volume) */
    uint32_t value;      /* in/out: percent / 0|1 mute / device kind */
    char     name[32];   /* out: device name */
    int32_t  result;     /* out: 0 ok, <0 error */
} audio_mixer_request_t;

typedef struct {
    uint32_t count;
    audio_device_kind_t kinds[8];
    char names[8][24];
} audio_device_list_t;

typedef struct {
    const void *data;
    uint32_t byte_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} audio_pcm_submit_request_t;

void audio_init(void);
const audio_device_info_t *audio_primary_device(void);
void audio_log_state(void);
uint8_t audio_volume(void);
void audio_set_volume(uint8_t percent);
bool audio_play_pcm(const void *data, uint32_t byte_count, uint32_t sample_rate,
                    uint16_t channels, uint16_t bits_per_sample);
bool audio_play_startup_chime(void);
void audio_play_pc_speaker_beep(void);
void audio_toggle_pause(void);
bool audio_is_playing(void);
bool audio_is_paused(void);
const char *audio_current_track(void);
void audio_update(void);
void audio_shutdown(void);

/* --- Task 19: music-player control (syscall 74) --- */
#define AUDIO_PLAYER_PAUSE_TOGGLE 0u
#define AUDIO_PLAYER_STOP         1u
#define AUDIO_PLAYER_GET_POSITION 2u
#define AUDIO_PLAYER_PLAY         3u
#define AUDIO_PLAYER_SEEK         4u

typedef struct {
    uint32_t cmd;          /* in: AUDIO_PLAYER_* */
    uint32_t position;     /* in/out: playback byte offset */
    uint32_t total;        /* out: total PCM bytes */
    uint32_t playing;      /* out: 1 if active, 0 if stopped */
    uint32_t paused;       /* out: 1 if paused */
} audio_player_ctl_request_t;

uint32_t audio_player_position_bytes(void);
uint32_t audio_player_total_bytes(void);
void audio_player_stop(void);
int32_t audio_player_ctl(const audio_player_ctl_request_t *request);

/* --- Task: media file playback interfaces --- */
/* Play a WAV file (RIFF/WAVE/fmt/data). Supports 8/16-bit PCM, mono/stereo;
 * mono and 8-bit are converted on the fly to 16-bit stereo for the DAC. */
bool wav_play_file(const char *path);
/* Play an MP3 file. A minimal MPEG1 Layer3 frame parser + PCM output is
 * provided; full IMDCT/synthesis is documented as a stub hook. Returns true
 * when the file was accepted and playback started. */
bool mp3_play_file(const char *path);

/* --- Feature 11: mixer control --- */
int32_t audio_mixer_ctl(const audio_mixer_request_t *request);
uint8_t audio_app_volume(int32_t pid);
void audio_set_app_volume(int32_t pid, uint8_t percent);
bool audio_muted(void);
void audio_set_muted(bool muted);
audio_device_kind_t audio_output_device(void);
void audio_set_output_device(audio_device_kind_t kind);
void audio_mixer_devices(audio_device_list_t *list);

/* --- Task: microphone recording (ADC capture) ---
 * Recorded format: 16-bit signed PCM, stereo, 44100 Hz, interleaved
 * L/R int16 samples. The kernel keeps a software ring buffer that the
 * ADC DMA engine fills; user code drains it with audio_record_read().
 * Mirrors the playback path but in the capture direction. */
#define AUDIO_REC_CMD_START   0u   /* begin capture; arg: sample rate (ignored, fixed 44.1k) */
#define AUDIO_REC_CMD_STOP    1u   /* end capture */
#define AUDIO_REC_CMD_READ    2u   /* drain up to .capacity bytes into .buffer */
#define AUDIO_REC_CMD_STATUS  3u   /* out: frames recorded / level */

typedef struct {
    uint32_t cmd;          /* in: AUDIO_REC_CMD_* */
    uint32_t sample_rate;  /* in: requested (fixed 44100); out: actual */
    uint32_t frames_total; /* out: total frames captured since start */
    uint32_t bytes_ready;  /* out: bytes available to read right now */
    uint8_t  level;        /* out: instantaneous VU level 0..100 */
    uint8_t  channels;     /* out: always 2 */
    uint16_t bits;         /* out: always 16 */
    uint32_t capacity;      /* in: read buffer capacity (READ) */
    uint32_t bytes_copied;  /* out: bytes copied (READ) */
    void    *buffer;       /* in: user buffer (READ) */
    int32_t  result;       /* out: 0 ok, <0 error */
} audio_record_request_t;

int32_t audio_record_ctl(audio_record_request_t *request);
void audio_record_pump(void);
bool audio_record_active(void);

/* --- Standard driver interface (probe/play/record/read/write/info/status) ---
 * audio_probe() scans PCI for an AC97/HDA/ES1371 device; prints "audio: not
 * found" and returns false when none is present. audio_play() submits a PCM
 * buffer (auto-converts 8-bit / mono to 16-bit stereo). audio_record() starts
 * capture and drains up to max_size bytes of recorded PCM. */
bool audio_probe(void);
bool audio_play(const void *buffer, uint32_t size, uint32_t sample_rate,
                uint16_t bits, uint16_t channels);
uint32_t audio_record(void *buffer, uint32_t max_size);
uint32_t audio_read(void *buffer, uint32_t max_size);
uint32_t audio_write(const void *buffer, uint32_t size, uint32_t sample_rate,
                     uint16_t bits, uint16_t channels);
void audio_set_volume_lr(uint8_t left, uint8_t right);
uint8_t audio_get_volume(void);
const audio_device_info_t *audio_info(void);
const char *audio_status(void);

#endif
