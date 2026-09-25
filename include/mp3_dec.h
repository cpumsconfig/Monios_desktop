#ifndef MP3_DEC_H
#define MP3_DEC_H

/*
 * include/mp3_dec.h - MPEG-1/2 Audio Layer III 解码器适配层。
 *
 * 实现：drivers/audio/mp3_dec.c，底层是 vendored 的公有领域解码器
 * drivers/audio/minimp3.h（CC0）。Huffman 码表、IMDCT 与 32 子带合成
 * 滤波器组都由它提供，本层只负责：
 *   - 把整个文件读进内存（bit reservoir 需要回看前面的帧）
 *   - 逐帧解码成 16-bit 立体声交错 PCM
 *   - 收集统计信息
 * 采样率转换与提交给音频引擎由 drivers/audio/audio.c 负责。
 */

#include "stdbool.h"
#include "stdint.h"

/* 单帧最多 1152 个样本/声道（MPEG-1 Layer III）。 */
#define MP3_DEC_MAX_SAMPLES_PER_FRAME 1152u
/* 输入文件大小上限：8 MiB，约 8 分钟的 128 kbps 音频。 */
#define MP3_DEC_MAX_INPUT_BYTES (8u * 1024u * 1024u)
/* 解码后 PCM 上限：24 MiB，约 2.4 分钟的 44.1 kHz 立体声。 */
#define MP3_DEC_MAX_PCM_BYTES (24u * 1024u * 1024u)

typedef struct {
    bool     ok;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t bitrate_kbps;
    uint32_t frames_decoded;   /* 解码成功的帧数（每帧 1152 样本） */
    uint32_t input_bytes;      /* 读入的 MP3 字节数 */
    uint32_t pcm_bytes;
    uint32_t duration_ms;
    uint32_t truncated;      /* 1 = 因缓冲上限提前结束 */
    char     status[64];
} mp3_dec_info_t;

/* 把一个 MP3 文件解码成 16-bit 立体声交错 PCM。
 *
 * 成功返回 true：out_pcm 指向 kmalloc 分配的缓冲区，调用者负责 kfree；
 * out_bytes 是字节数，out_frames 是每声道样本数，out_rate 与 out_channels
 * 是流参数（目前总是 2 声道）。
 * 失败返回 false，原因见 mp3_dec_status()。 */
bool mp3_decode_file(const char *path, int16_t **out_pcm, uint32_t *out_bytes,
                     uint32_t *out_frames, uint32_t *out_rate,
                     uint32_t *out_channels);

/* 最近一次解码的统计信息。 */
const mp3_dec_info_t *mp3_dec_last_info(void);
/* 最近一次解码的状态字符串。 */
const char *mp3_dec_status(void);
/* 复位统计状态。 */
void mp3_dec_reset(void);

#endif /* MP3_DEC_H */
