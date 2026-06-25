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
    uint16_t ac97_ext_audio_id;
    uint16_t ac97_status;
    bool variable_rate_audio;
    bool present;
} audio_device_info_t;

void audio_init(void);
const audio_device_info_t *audio_primary_device(void);
void audio_log_state(void);
uint8_t audio_volume(void);
void audio_set_volume(uint8_t percent);
bool audio_play_file(const char *path);
bool audio_play_wav_file(const char *path);
bool audio_play_startup_chime(void);
void audio_play_pc_speaker_beep(void);
void audio_toggle_pause(void);
bool audio_is_playing(void);
bool audio_is_paused(void);
const char *audio_current_track(void);
void audio_update(void);
void audio_shutdown(void);

#endif
