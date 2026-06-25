#ifndef AAC_H
#define AAC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    bool adts;
    bool mp4_container;
    uint8_t profile;
    uint8_t sampling_index;
    uint32_t sample_rate;
    uint8_t channels;
    uint16_t frame_length;
    uint32_t header_size;
    uint32_t file_size;
    uint32_t estimated_frames;
    uint32_t duration_ms;
    char codec[32];
    char status[64];
} aac_info_t;

void aac_init(void);
bool aac_parse_adts_header(const uint8_t *data, uint32_t size, aac_info_t *out);
bool aac_probe_file(const char *path, aac_info_t *out);
const aac_info_t *aac_last_info(void);
const char *aac_status(void);

#endif
