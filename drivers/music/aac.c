#include "aac.h"
#include "common.h"
#include "file.h"

static const uint32_t g_aac_rates[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

static aac_info_t g_aac_info;

static uint32_t aac_id3v2_skip(const uint8_t *data, uint32_t size)
{
    uint32_t tag_size;

    if (data == NULL || size < 10 || memcmp(data, "ID3", 3) != 0) {
        return 0;
    }
    if ((data[6] & 0x80u) != 0 ||
        (data[7] & 0x80u) != 0 ||
        (data[8] & 0x80u) != 0 ||
        (data[9] & 0x80u) != 0) {
        return 0;
    }
    tag_size = ((uint32_t) data[6] << 21) |
               ((uint32_t) data[7] << 14) |
               ((uint32_t) data[8] << 7) |
               (uint32_t) data[9];
    return tag_size + 10u;
}

void aac_init(void)
{
    memset(&g_aac_info, 0, sizeof(g_aac_info));
    strcpy(g_aac_info.status, "aac: ready");
}

bool aac_parse_adts_header(const uint8_t *data, uint32_t size, aac_info_t *out)
{
    uint8_t profile;
    uint8_t sampling_index;
    uint8_t channels;
    uint16_t frame_length;
    bool protection_absent;

    if (data == NULL || out == NULL || size < 7) {
        return false;
    }
    if (data[0] != 0xFF || (data[1] & 0xF0u) != 0xF0u) {
        return false;
    }
    protection_absent = (data[1] & 0x01u) != 0;
    profile = (uint8_t) ((data[2] >> 6) & 0x03u);
    sampling_index = (uint8_t) ((data[2] >> 2) & 0x0Fu);
    channels = (uint8_t) (((data[2] & 0x01u) << 2) | ((data[3] >> 6) & 0x03u));
    frame_length = (uint16_t) (((uint16_t) (data[3] & 0x03u) << 11) |
                               ((uint16_t) data[4] << 3) |
                               ((uint16_t) data[5] >> 5));
    if (sampling_index >= 13 || g_aac_rates[sampling_index] == 0 || channels == 0 || frame_length < 7) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->adts = true;
    out->profile = (uint8_t) (profile + 1);
    out->sampling_index = sampling_index;
    out->sample_rate = g_aac_rates[sampling_index];
    out->channels = channels;
    out->frame_length = frame_length;
    out->header_size = protection_absent ? 7u : 9u;
    strcpy(out->codec, "aac-adts");
    strcpy(out->status, "aac: adts header parsed");
    return true;
}

static bool aac_probe_mp4(const uint8_t *data, uint32_t size, aac_info_t *out)
{
    if (data == NULL || out == NULL || size < 12) {
        return false;
    }
    if (memcmp(data + 4, "ftyp", 4) != 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->mp4_container = true;
    strcpy(out->codec, "aac-mp4");
    strcpy(out->status, "aac: mp4 container detected");
    return true;
}

static void aac_scan_adts_file(const char *path, uint32_t start_offset, uint32_t file_size, aac_info_t *info)
{
    uint8_t header[9];
    uint32_t offset = start_offset;
    uint32_t frames = 0;
    int32_t read;

    while (offset + 7u <= file_size) {
        aac_info_t frame;

        read = file_read_at(path, offset, header, sizeof(header));
        if (read < 7 || !aac_parse_adts_header(header, (uint32_t) read, &frame)) {
            break;
        }
        if (frame.frame_length < frame.header_size || offset + frame.frame_length > file_size) {
            break;
        }
        frames++;
        offset += frame.frame_length;
    }

    if (frames > 0) {
        info->estimated_frames = frames;
        if (info->sample_rate != 0) {
            info->duration_ms = (uint32_t) (((uint64_t) frames * 1024ULL * 1000ULL) / info->sample_rate);
        }
        if (offset == file_size) {
            strcpy(info->status, start_offset == 0 ? "aac: adts stream parsed" : "aac: id3/adts stream parsed");
        } else {
            strcpy(info->status, "aac: adts stream partially parsed");
        }
    }
}

bool aac_probe_file(const char *path, aac_info_t *out)
{
    uint8_t header[16];
    int32_t size;
    int32_t read;
    uint32_t header_size;
    uint32_t data_offset = 0;
    aac_info_t info;

    if (path == NULL || out == NULL) {
        return false;
    }
    memset(&info, 0, sizeof(info));
    size = file_size(path);
    read = size > 0 ? file_read_at(path, 0, header, sizeof(header)) : -1;
    if (size <= 0 || read <= 0) {
        strcpy(g_aac_info.status, "aac: file unavailable");
        return false;
    }
    header_size = (uint32_t) read;
    data_offset = aac_id3v2_skip(header, (uint32_t) read);
    if (data_offset > 0 && data_offset < (uint32_t) size) {
        read = file_read_at(path, data_offset, header, sizeof(header));
        header_size = read > 0 ? (uint32_t) read : 0;
    }
    if ((read <= 0 || !aac_parse_adts_header(header, header_size, &info)) &&
        !aac_probe_mp4(header, header_size, &info)) {
        strcpy(g_aac_info.status, "aac: unsupported header");
        return false;
    }
    info.file_size = (uint32_t) size;
    if (info.adts) {
        aac_scan_adts_file(path, data_offset, (uint32_t) size, &info);
    }
    g_aac_info = info;
    *out = info;
    return true;
}

const aac_info_t *aac_last_info(void)
{
    return &g_aac_info;
}

const char *aac_status(void)
{
    return g_aac_info.status;
}
