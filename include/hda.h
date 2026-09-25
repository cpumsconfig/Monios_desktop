#ifndef _HDA_H_
#define _HDA_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool mmio_ready;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint8_t irq;
    uint32_t mmio_base;
    uint16_t global_cap;
    char status[64];
} hda_info_t;

bool hda_driver_init(void);
bool hda_probe(void);
void hda_shutdown(void);
const hda_info_t *hda_info(void);
const char *hda_status(void);

/* PCM streaming: play() submits a buffer to the output DMA, record() arms
 * the capture DMA. hda_read() drains captured samples, hda_write() queues
 * playback (thin wrapper over hda_play). */
bool hda_play(const void *buffer, uint32_t size, uint32_t sample_rate,
              uint16_t bits, uint16_t channels);
bool hda_record(void *buffer, uint32_t max_size);
uint32_t hda_read(void *buffer, uint32_t max_size);
uint32_t hda_write(const void *buffer, uint32_t size, uint32_t sample_rate,
                   uint16_t bits, uint16_t channels);

/* Master output amplifier gain 0..100 percent. */
void hda_set_volume(uint8_t percent);
uint8_t hda_get_volume(void);

#endif
