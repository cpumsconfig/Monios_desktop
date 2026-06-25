#ifndef _ES1371_H_
#define _ES1371_H_

#include "audio.h"
#include "dma.h"
#include "pci.h"

bool es1371_supported(const pci_device_info_t *info);
uint32_t es1371_sample_rate(void);
bool es1371_init(audio_device_info_t *device, dma_buffer_t *dma_buffer, int16_t **pcm_buffer);
bool es1371_prepare_pcm_out(const audio_device_info_t *device, const dma_buffer_t *dma_buffer, uint32_t frame_count, uint32_t period_frames, bool loop);
bool es1371_pcm_interrupt_pending(const audio_device_info_t *device);
uint32_t es1371_playback_position_frames(const audio_device_info_t *device, uint32_t ring_frames);
uint32_t es1371_debug_status(const audio_device_info_t *device);
void es1371_clear_pcm_interrupt(const audio_device_info_t *device);
void es1371_rearm_pcm_out(const audio_device_info_t *device);
void es1371_set_paused(const audio_device_info_t *device, bool paused);
void es1371_set_volume(const audio_device_info_t *device, uint8_t percent);
void es1371_stop(const audio_device_info_t *device);

#endif
