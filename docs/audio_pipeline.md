# 音频播放流水线说明

## 总览

音频子系统分三层：**解码 → 统一为 PCM → 提交给硬件**。所有格式最终都
汇聚到同一个出口 `audio_play_pcm()`。

```
                        ┌──────────────────────────────────────┐
  play <file>  ────────►│ audio_play_file()   按扩展名分派      │
                        └───────────────┬──────────────────────┘
                                        │
        ┌───────────────────┬───────────┴───────────┬─────────────────┐
        ▼                   ▼                       ▼                 ▼
   .wav                .mp3                    .m4a / .mp4       其他
 audio_play_wav_      mp3_play_file()        aac_probe_file()   拒绝
   file()                  │                 （仅解析，不解码）
        │                  ▼
        │            mp3_decode_file()
        │            (minimp3, →S16 立体声)
        │                  │
        │                  ▼
        │        audio_resample_stereo_s16()
        │                  │
        └──────────┬───────┘
                   ▼
        audio_play_pcm(data, bytes, rate, channels, bits)
                   │
                   ▼
        HDA / ES1371 / AC'97 后端 + DMA 环形缓冲
```

## 格式支持矩阵

| 格式 | 解码 | 说明 |
| --- | --- | --- |
| WAV (PCM) | ✅ 完整 | 解析 RIFF/WAVE 头，转成 16-bit 立体声 |
| MP3 | ✅ 完整 | vendored minimp3，见 `docs/mp3_decoder.md` |
| M4A / MP4 / M4B | ⚠️ 仅解析 | 只做容器与 ADTS 头探测，不解码；若同目录存在同名 `.wav` 会作为 sidecar 直接播放 |
| AAC | ⚠️ 仅解析 | 同上，`aac_probe_file()` 报告流参数后明确回 "decode unavailable" |

## 出口：`audio_play_pcm()`

这是唯一的提交点，签名：

```c
bool audio_play_pcm(const void *data, uint32_t byte_count,
                    uint32_t sample_rate, uint16_t channels,
                    uint16_t bits_per_sample);
```

入口会先检查后端是否就绪（设备存在、硬件已初始化、DMA 缓冲已分配），
任何一个不满足就直接返回 `false`，不做半残播放。

## WAV 路径

`audio_play_wav_file()`：

1. 读文件头，`audio_read_wav_header()` 解析 RIFF/WAVE、`fmt ` 块与
   `data` 块偏移；
2. `wav_convert_to_pcm16_stereo()` 把任意位深/声道数统一转成
   16-bit 立体声交错；已经是目标格式时不复制，直接用原缓冲；
3. 提交 `audio_play_pcm()`。

## MP3 路径

`mp3_play_file()`：

1. `mp3_decode_file()` 解出 16-bit 立体声 PCM（详见
   `docs/mp3_decoder.md`）；
2. 比较 MP3 采样率与设备当前采样率，不同则
   `audio_resample_stereo_s16()` 线性重采样；
3. 提交 `audio_play_pcm()`；
4. 无论成败都 `kfree` 中间缓冲。

解码失败时会额外调 `mp3_find_first_frame()` 做帧几何诊断，在日志里
区分"有合法帧但解不出"与"根本不是 MPEG-1 Layer III"。

## 采样率转换

`audio_resample_stereo_s16(src, src_frames, src_rate, dst_rate, &out, &out_frames)`
是**线性插值**的立体声 S16 重采样：

- 输入输出都是交错 L/R；
- 目标帧数按比例换算，逐样本在源上取相邻两点插值；
- 输出缓冲区由 `kmalloc` 分配，调用方负责 `kfree`。

线性插值在 44.1k→48k 这类小幅转换上足够；大比例转换会有高频损失，
属于已知折衷。

## 硬件后端

| 后端 | 文件 | 状态 |
| --- | --- | --- |
| HDA (Intel High Definition Audio) | `drivers/audio/hda.c` | 框架 + Stream Descriptor 编程 |
| ES1371 | `drivers/audio/audio.c` | 驱动实现 |
| AC'97 | `drivers/audio/audio.c` | 初始化、格式设置、流控 |

HDA 的 Stream Descriptor 寄存器编程在本次整顿中做了修正：原先把 BDL
地址写进了 `CBL`、从未编程 `CBL`、`LVI` 也写在错误偏移，现已按规范
正确编程 `CBL`/`BDPL`/`BDPU`/`LVI`/`FMT` 并做回读自检。
`SDnFMT` 的编码也修正为
`(rate_field << 8) | (bits_field << 4) | (channels - 1)`，
48 kHz / 16-bit / 立体声 = `0x0011`。

## shell 用法

```
play <file.wav|file.mp3>
```

这是 `kernel/ui/shell.c` 里新增的命令，内部就是按扩展名走到
`audio_play_file()`。

## 已知限制

- AAC 没有真实解码器，只报流参数。
- 没有流式/环形播放：整段 PCM 一次性提交，长文件占内存（MP3 上限见
  `docs/mp3_decoder.md`）。
- 重采样是线性插值，非高质量重采样。
- 混音、多流并发、音量控制与效果器尚未实现。
