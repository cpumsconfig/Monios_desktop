/*
 * drivers/audio/mp3_dec.c - MPEG-1/2 Audio Layer III 解码适配层。
 *
 * 解码核心来自 drivers/audio/minimp3.h（公有领域，CC0）。这一层不做任何
 * DSP：只负责把文件读进内存、驱动 minimp3 逐帧解码、把 16-bit 立体声
 * 交错 PCM 交给调用者。
 *
 * 之前的 mp3_play_file() 只定位到第一个帧头就返回 false，并把
 * "Huffman + IMDCT + 32 子带合成" 列为 TODO。这里补齐了那条链路：
 * 完整解码由 minimp3 完成（码表与滤波器组直接取自参考实现，
 * 避免手写数千条 Huffman 码表带来的不可验证错误）。
 */

#include "mp3_dec.h"
#include "common.h"
#include "file.h"
#include "kernel.h"
#include "memory.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static mp3_dec_info_t g_mp3_dec;

static void mp3_dec_set_status(const char *text)
{
    uint32_t i = 0u;

    while (text[i] != '\0' && i + 1u < sizeof(g_mp3_dec.status)) {
        g_mp3_dec.status[i] = text[i];
        i++;
    }
    g_mp3_dec.status[i] = '\0';
}

void mp3_dec_reset(void)
{
    memset(&g_mp3_dec, 0, sizeof(g_mp3_dec));
    mp3_dec_set_status("mp3: idle");
}

const mp3_dec_info_t *mp3_dec_last_info(void)
{
    return &g_mp3_dec;
}

const char *mp3_dec_status(void)
{
    return g_mp3_dec.status;
}

bool mp3_decode_file(const char *path, int16_t **out_pcm, uint32_t *out_bytes,
                     uint32_t *out_frames, uint32_t *out_rate,
                     uint32_t *out_channels)
{
    mp3dec_t *dec = NULL;
    uint8_t *input = NULL;
    int16_t *pcm = NULL;
    mp3dec_frame_info_t fi;
    int32_t file_bytes;
    int32_t got;
    uint32_t in_pos = 0u;
    uint32_t in_end;
    uint32_t pcm_used = 0u;
    uint32_t frames = 0u;
    uint32_t rate = 0u;
    uint32_t channels = 0u;
    uint32_t bitrate = 0u;
    bool truncated = false;

    if (path == NULL || out_pcm == NULL || out_bytes == NULL ||
        out_frames == NULL || out_rate == NULL || out_channels == NULL) {
        return false;
    }
    mp3_dec_reset();
    *out_pcm = NULL;
    *out_bytes = 0u;
    *out_frames = 0u;
    *out_rate = 0u;
    *out_channels = 0u;

    file_bytes = file_size(path);
    if (file_bytes <= 0) {
        mp3_dec_set_status("mp3: file unavailable");
        return false;
    }
    if ((uint32_t) file_bytes > MP3_DEC_MAX_INPUT_BYTES) {
        mp3_dec_set_status("mp3: file too large for decoder buffer");
        return false;
    }

    input = (uint8_t *) kmalloc((uint64_t) file_bytes);
    dec = (mp3dec_t *) kmalloc((uint64_t) sizeof(mp3dec_t));
    pcm = (int16_t *) kmalloc(MP3_DEC_MAX_PCM_BYTES);
    if (input == NULL || dec == NULL || pcm == NULL) {
        kfree(input);
        kfree(dec);
        kfree(pcm);
        mp3_dec_set_status("mp3: decoder alloc failed");
        return false;
    }

    got = file_read_at(path, 0u, input, (uint32_t) file_bytes);
    if (got <= 0) {
        kfree(input);
        kfree(dec);
        kfree(pcm);
        mp3_dec_set_status("mp3: file read failed");
        return false;
    }
    in_end = (uint32_t) got;
    g_mp3_dec.input_bytes = in_end;

    mp3dec_init(dec);

    /* 逐帧解码。minimp3 会在 frame_bytes==0 时表示"没有更多完整帧"，
     * 也会在遇到 ID3/填充帧时返回 samples==0 但推进 frame_bytes。 */
    while (in_pos < in_end) {
        int samples;

        /* 输出缓冲必须留得下一整帧（1152 样本 × 2 声道 × 2 字节）。 */
        if (pcm_used + MP3_DEC_MAX_SAMPLES_PER_FRAME * 4u > MP3_DEC_MAX_PCM_BYTES) {
            truncated = true;
            break;
        }

        memset(&fi, 0, sizeof(fi));
        samples = mp3dec_decode_frame(dec, input + in_pos, (int) (in_end - in_pos),
                                      pcm + (pcm_used / 2u), &fi);
        if (fi.frame_bytes <= 0) {
            break;                       /* 输入结束或剩余字节不构成长整帧 */
        }
        in_pos += (uint32_t) fi.frame_bytes;

        if (samples > 0) {
            frames += (uint32_t) samples;
            rate = (uint32_t) fi.hz;
            channels = (uint32_t) fi.channels;
            bitrate = (uint32_t) fi.bitrate_kbps;
            pcm_used += (uint32_t) samples * channels * 2u;
            g_mp3_dec.frames_decoded++;
        }
    }

    kfree(input);
    kfree(dec);

    if (frames == 0u || pcm_used == 0u) {
        kfree(pcm);
        mp3_dec_set_status("mp3: no decodable frames");
        return false;
    }

    g_mp3_dec.ok = true;
    g_mp3_dec.sample_rate = rate;
    g_mp3_dec.channels = channels;
    g_mp3_dec.bitrate_kbps = bitrate;
    g_mp3_dec.pcm_bytes = pcm_used;
    g_mp3_dec.truncated = truncated ? 1u : 0u;
    if (rate != 0u) {
        g_mp3_dec.duration_ms = (uint32_t) (((uint64_t) frames * 1000ULL) / rate);
    }
    mp3_dec_set_status(truncated ? "mp3: decoded (output buffer capped)"
                                 : "mp3: decoded");

    *out_pcm = pcm;
    *out_bytes = pcm_used;
    *out_frames = frames;
    *out_rate = rate;
    *out_channels = channels;
    return true;
}
