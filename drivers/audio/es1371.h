#ifndef _ES1371_H_
#define _ES1371_H_

#include "audio.h"
#include "dma.h"
#include "pci.h"

#define ES1371_RATE_44100            44100U
#define ES1371_RATE_48000            48000U

bool es1371_supported(const pci_device_info_t *info);
uint32_t es1371_sample_rate(void);
bool es1371_set_sample_rate(audio_device_info_t *device, uint32_t rate);
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

/* ------------------------------------------------------------------ */
/* ADC (record / capture) channel. The ES1371 has an independent ADC  */
/* (P1/R1) DMA engine separate from the DAC2 (P2) playback engine.    */
/* It uses its own frame registers (0x24/0x30/0x34) and its own serial */
/* control (P1) fields. Recording is programmed as a looping buffer.   */
/* ------------------------------------------------------------------ */
bool es1371_adc_prepare(const audio_device_info_t *device, dma_buffer_t *dma_buffer,
                        uint32_t frame_count, uint32_t period_frames);
void es1371_adc_stop(const audio_device_info_t *device);
bool es1371_adc_interrupt_pending(const audio_device_info_t *device);
void es1371_adc_clear_interrupt(const audio_device_info_t *device);
void es1371_adc_rearm(const audio_device_info_t *device);
uint32_t es1371_adc_position_frames(const audio_device_info_t *device, uint32_t ring_frames);
void es1371_adc_set_rate(const audio_device_info_t *device, uint32_t rate);

/* ------------------------------------------------------------------ */
/*  Standalone driver interface (probe/init/play/record/read/write/    */
/*  shutdown/info/status). Loads the ES1371 on its own without the      */
/*  global audio layer. Prints "es1371: not found" when absent.        */
/* ------------------------------------------------------------------ */
bool es1371_probe(void);
bool es1371_play(const void *buffer, uint32_t size, uint32_t sample_rate);
bool es1371_record_start(void);
uint32_t es1371_record(void *buffer, uint32_t max_size);
uint32_t es1371_read(void *buffer, uint32_t max_size);
uint32_t es1371_write(const void *buffer, uint32_t size, uint32_t sample_rate);
void es1371_shutdown(void);
const audio_device_info_t *es1371_info(void);
const char *es1371_status(void);

#endif
