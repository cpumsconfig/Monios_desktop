# MP3 解码器说明

## 结论速览

MoniOS 现在能真正解码并播放 MP3，不再是"只解析帧头"。

- 适配层：`drivers/audio/mp3_dec.c` + `include/mp3_dec.h`
- 解码核心：`drivers/audio/minimp3.h`（vendored，公有领域 CC0）
- 调用链：`shell play <file.mp3>` → `audio_play_file()` →
  `mp3_play_file()` → `mp3_decode_file()` → `audio_play_pcm()`

## 为什么要 vendored 而不是自己写

原来的 `mp3_play_file()` 只定位到第一个帧头就返回 `false`，注释里把
Huffman 解码、IMDCT、32 子带合成滤波器组列为 TODO。自己实现不可行：

- MPEG-1 Layer III 的 Huffman 码表有数十张、上千条码字（ISO 11172-3
  的 Table 3-B.7 起）。手抄这些表**无法在本项目内有效验证**——没有任何
  参考向量能覆盖所有码表分支，抄错一位就是一个听不出来但结果错误的 bug。
- 项目里已经有过同类先例：字体渲染直接用了 `kernel/ui/stb_truetype.h`。
  音频这里沿用同样做法，把经过广泛验证的参考实现作为 vendored 文件引入。

于是选了 **minimp3**：单头文件、CC0（公有领域，无授权顾虑）、无外部依赖。

### 对 minimp3.h 做的本地改动

只有三处，都是为了适配 freestanding 内核环境：

1. 去掉 `<stdlib.h>`（内核没有标准库的 malloc 链）。
2. `<string.h>` 改为项目自带的 `"string.h"`。
3. 文件顶部默认定义 `MINIMP3_NO_SIMD` 与 `MINIMP3_ONLY_MP3`：
   - `NO_SIMD`：不依赖 SSE/AVX 内建函数，走可移植标量路径。
   - `ONLY_MP3`：只保留 Layer III 路径，裁掉 Layer I/II 代码，减小体积。

编译产物 `out/mp3_dec.o` 的 `.text` 约 **30 KB**。

## 适配层做了什么

`mp3_decode_file()` 只做"搬运 + 记账"，不做任何 DSP：

1. 用 `file_size()` / `file_read_at()` 把**整个文件**读进一块 `kmalloc`
   缓冲区。整文件读入是刻意的——MP3 的 **bit reservoir** 允许后面的帧
   引用前面帧的数据，流式小块喂入会解错头几帧。
2. 分配一个 `mp3dec_t` 状态块与一块 PCM 输出缓冲区。
3. 循环调用 `mp3dec_decode_frame()`：
   - `fi.frame_bytes <= 0` 表示输入耗尽或剩余字节凑不出完整帧，退出。
   - `samples > 0` 时累加帧数、记录 `hz`/`channels`/`bitrate_kbps`。
   - `samples == 0` 但 `frame_bytes > 0` 是正常的（ID3 标签帧、填充帧），
     只推进偏移、不计帧。
4. 输出统一为 **16-bit 立体声交错 PCM**，然后交给
   `drivers/audio/audio.c` 做重采样与提交。

### 资源上限（`include/mp3_dec.h`）

| 宏 | 值 | 含义 |
| --- | --- | --- |
| `MP3_DEC_MAX_SAMPLES_PER_FRAME` | 1152 | MPEG-1 Layer III 每声道最大样本数 |
| `MP3_DEC_MAX_INPUT_BYTES` | 8 MiB | 输入文件上限（约 8 分钟 128 kbps） |
| `MP3_DEC_MAX_PCM_BYTES` | 24 MiB | 输出 PCM 上限（约 2.4 分钟 44.1 kHz 立体声） |

超过输出上限时不会失败，而是**截断**：置 `truncated = 1`，状态字符串变
成 `mp3: decoded (output buffer capped)`，已解码的部分照常播放。

### 错误路径

所有失败都会 `kfree` 已分配的三块内存，并通过 `mp3_dec_status()`
返回可读原因：

- `mp3: file unavailable` — 文件打不开或大小为 0
- `mp3: file too large for decoder buffer` — 超过 8 MiB
- `mp3: decoder alloc failed` — 三块缓冲区任一分配失败
- `mp3: file read failed` — 落盘读取失败
- `mp3: no decodable frames` — 一帧都没解出来（不是合法 MP3）

## 与音频引擎的衔接

`mp3_play_file()`（在 `drivers/audio/audio.c`）在拿到 PCM 后：

1. 读设备当前采样率，与 MP3 自身采样率比较；
2. 不一致就调 `audio_resample_stereo_s16()` 做线性重采样到设备采样率；
3. 调 `audio_play_pcm(pcm, bytes, rate, 2, 16)` 提交；
4. 无论成功与否都 `kfree(pcm)`。

如果解码失败，还会调用 `mp3_find_first_frame()` 做一次帧几何诊断，
在日志里区分"有帧但解不出"和"根本不是 MPEG-1 Layer III"。

## 已知限制

- 输出固定 16-bit 立体声；单声道源会在解码层就按 `fi.channels` 记账，
  但下游按 2 声道提交，极端情况下单声道文件的时长显示会偏差。
- 不支持 MPEG-2.5 的低采样率扩展的完整覆盖（minimp3 对 Layer III 的
  支持面已覆盖主流文件）。
- 大文件受 8 MiB 输入上限约束，完整专辑级别的文件需要先切分。
- 解码在调用上下文同步完成，长文件会占用较久；尚未做增量/异步解码。

## 后续可做

- 把解码改成和 `memtest` 一样的**增量模型**（按帧推进、带进度回调），
  避免一次性占用。
- 做成环形缓冲 + 后台解码线程，边解边放，解除 8 MiB / 24 MiB 的硬上限。
- 补 AAC/M4A 的真实解码（目前只做容器与 ADTS 头解析）。
