#include "aac.h"
#include "audio.h"
#include "common.h"
#include "dma.h"
#include "es1371.h"
#include "hda.h"
#include "file.h"
#include "hda.h"
#include "interrupt.h"
#include "kernel.h"
#include "memory.h"
#include "mp3_dec.h"
#include "pci.h"

#define PCI_CLASS_MULTIMEDIA 0x04
#define PCI_SUBCLASS_AUDIO   0x01
#define PCI_SUBCLASS_HDA     0x03
#define PCI_VENDOR_INTEL     0x8086
#define PCI_DEVICE_ICH_AC97  0x2415

#define PCI_COMMAND_OFFSET         0x04
#define PCI_COMMAND_IO             0x0001
#define PCI_COMMAND_BUS_MASTER     0x0004

#define AC97_MIXER_RESET           0x00
#define AC97_MIXER_MASTER_VOL      0x02
#define AC97_MIXER_PCM_OUT_VOL     0x18
#define AC97_MIXER_POWERDOWN_CTRL  0x26
#define AC97_MIXER_EXT_AUDIO_ID    0x28
#define AC97_MIXER_EXT_AUDIO_CTRL  0x2A
#define AC97_MIXER_PCM_FRONT_RATE  0x2C

#define AC97_EXT_AUDIO_VRA         0x0001

#define AC97_PO_BDBAR              0x10
#define AC97_PO_CIV                0x14
#define AC97_PO_LVI                0x15
#define AC97_PO_SR                 0x16
#define AC97_PO_PICB               0x18
#define AC97_PO_CR                 0x1B

#define AC97_GLOB_CNT              0x2C
#define AC97_GLOB_STA              0x30
#define AC97_DMA_BYTES             (1024U * 1024U)
#define AC97_BDL_BYTES             4096U
#define AC97_TEST_SAMPLE_RATE      44100U
#define AC97_MIXER_IO_BYTES        0x40U
#define AC97_BUS_MASTER_IO_BYTES   0x40U

#define AC97_X_CR_RPBM             0x01
#define AC97_X_CR_RR               0x02
#define AC97_X_SR_DCH              0x01
#define AC97_X_SR_CELV             0x02
#define AC97_X_SR_LVBCI            0x04
#define AC97_X_SR_BCIS             0x08
#define AC97_X_SR_FIFOE            0x10
#define AC97_X_SR_CLEAR            0x001C

#define AC97_BDL_IOC               0x8000
#define AC97_BDL_BUP               0x4000
#define AC97_BDL_MAX_SAMPLES       0xFFFEU
#define AC97_BDL_COUNT             32U
#define AC97_STEREO_CHANNELS       2U
#define AC97_STREAM_CHUNK_BYTES    (AC97_DMA_BYTES / AC97_BDL_COUNT)
#define AC97_STREAM_LOG_BYTES      (AC97_STREAM_CHUNK_BYTES * 8U)
#define ES1371_STREAM_FRAMES       8192U
#define ES1371_STREAM_PERIODS      8U
#define ES1371_SOURCE_CACHE_BYTES  (512U * 1024U)
#define ES1371_CACHE_READ_BYTES    (192U * 1024U)
#define AUDIO_TRACK_PATH_MAX       96U

#define PC_SPEAKER_CTRL            0x61
#define PIT_CHANNEL2               0x42
#define PIT_COMMAND                0x43

typedef struct {
    uint32_t offset;
    uint16_t length;
    uint16_t control;
} __attribute__((packed)) ac97_buffer_descriptor_t;

typedef struct {
    uint16_t format_tag;
    uint16_t channels;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_pcm_fmt_t;

typedef struct {
    uint8_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t data_size;
    uint8_t *data;
} audio_track_t;

static audio_device_info_t g_audio_device;
static audio_track_t g_track;
static dma_buffer_t g_dma_buffer;
static ac97_buffer_descriptor_t *g_bdl;
static int16_t *g_audio_dma;
static bool g_audio_started;
static bool g_audio_paused;
static bool g_audio_hw_initialized;
static uint32_t g_audio_dma_frames;
static uint32_t g_audio_total_samples;
static uint32_t g_audio_data_offset;
static uint32_t g_audio_data_size;
static uint32_t g_audio_stream_pos;
static uint32_t g_audio_stream_rate;
static bool g_audio_streaming;
static bool g_audio_stream_eof;
static bool g_audio_stream_desc_busy[AC97_BDL_COUNT];
static uint8_t g_audio_volume;
static uint32_t g_audio_stream_lvi;
static uint32_t g_audio_stream_last_civ;
static uint32_t g_audio_stream_last_log_pos;
static uint32_t g_audio_stream_recovery_count;
static volatile uint32_t g_audio_es1371_irq_pending;
static volatile uint32_t g_audio_es1371_irq_total;
static uint64_t g_audio_es1371_chunk_start_tick;
static uint64_t g_audio_es1371_chunk_ticks;
static uint64_t g_audio_es1371_stop_tick;
static uint32_t g_audio_es1371_period_frames;
static uint32_t g_audio_es1371_next_period;
static uint8_t *g_audio_es1371_src_buffer;
static uint32_t g_audio_es1371_src_buffer_size;
static uint32_t g_audio_es1371_cache_start;
static uint32_t g_audio_es1371_cache_size;
static uint32_t g_audio_es1371_underruns;
static bool g_audio_es1371_draining;
static uint32_t g_audio_es1371_ring_frames;
static uint32_t g_audio_es1371_period_events;
static uint32_t g_audio_es1371_last_hw_period;
static uint32_t g_audio_es1371_last_refill_period;
static uint8_t *g_audio_pcm_source;
static uint32_t g_audio_pcm_source_size;
static char g_audio_stream_path[AUDIO_TRACK_PATH_MAX];
static char g_audio_track_name[64];

/* --- ADC (microphone recording) state --- */
#define AUDIO_REC_FIFO_BYTES (256U * 1024U)
#define AUDIO_REC_DMA_BYTES   (256U * 1024U)
static dma_buffer_t g_rec_dma_buffer;
static int16_t *g_rec_dma;
static bool g_rec_active;
static bool g_rec_hw_ready;
static uint32_t g_rec_ring_frames;
static uint32_t g_rec_period_frames;
static uint32_t g_rec_last_hw_frame;
static uint32_t g_rec_total_frames;
static uint8_t  g_rec_level;
static uint8_t *g_rec_fifo;
static uint32_t g_rec_fifo_head;
static uint32_t g_rec_fifo_tail;

static bool audio_init_ac97_hw(void);
static void audio_apply_volume(void);
static bool audio_irq_handler(uint8_t irq, void *ctx);
static uint32_t audio_dma_pcm_capacity_frames(void);
static bool audio_start_ac97_stream(void);
static bool audio_prime_ac97_stream_ring(void);
static bool audio_restart_ac97_stream_from_current(void);
static void audio_clear_stream_descriptor(uint32_t index);
static void audio_release_played_stream_descriptors(uint32_t civ);
static bool audio_fill_stream_descriptor(uint32_t index);
static bool audio_prefetch_es1371_stream_cache(uint32_t read_budget);
static bool audio_fill_es1371_stream_period(uint32_t period_index, bool *has_audio);
static bool audio_start_es1371_stream(void);
static bool __attribute__((unused)) audio_play_file(const char *path);
static bool audio_play_wav_file(const char *path);
static bool audio_read_source(uint32_t offset, void *buffer, uint32_t size);
static void audio_stop_current_playback(void);
static bool wav_convert_to_pcm16_stereo(const char *path, uint32_t data_offset,
                                        const wav_pcm_fmt_t *fmt, uint8_t **out_buf,
                                        uint32_t *out_size);

static void audio_write8(uint16_t port, uint8_t value)
{
    outb(port, value);
}

static void audio_write16(uint16_t port, uint16_t value)
{
    outw(port, value);
}

static void audio_write32(uint16_t port, uint32_t value)
{
    outl(port, value);
}

static uint8_t audio_read8(uint16_t port)
{
    return inb(port);
}

static uint16_t audio_read16(uint16_t port)
{
    return inw(port);
}

static uint32_t __attribute__((unused)) audio_read32(uint16_t port)
{
    return inl(port);
}

static void audio_append_hex4(char *dst, uint16_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    for (uint8_t i = 0; i < 4; i++) {
        dst[i] = hex[(value >> ((3 - i) * 4)) & 0xF];
    }
    dst[4] = '\0';
}

static void audio_log_path(const char *prefix, const char *path)
{
    char line[96];
    uint32_t pos = 0;

    while (prefix != NULL && prefix[pos] != '\0' && pos + 1 < sizeof(line)) {
        line[pos] = prefix[pos];
        pos++;
    }
    if (path == NULL) {
        path = "(null)";
    }
    for (uint32_t i = 0; path[i] != '\0' && pos + 1 < sizeof(line); i++) {
        line[pos++] = path[i];
    }
    line[pos] = '\0';
    log_write(line);
}

static void audio_append_dec(char *dst, uint32_t value)
{
    char tmp[11];
    uint32_t pos = 0;

    if (value == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        return;
    }
    while (value > 0 && pos < sizeof(tmp)) {
        tmp[pos++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    for (uint32_t i = 0; i < pos; i++) {
        dst[i] = tmp[pos - i - 1u];
    }
    dst[pos] = '\0';
}

static void audio_log_append(char *line, uint32_t *pos, const char *text)
{
    while (text != NULL && text[0] != '\0' && *pos + 1 < 96U) {
        line[*pos] = *text;
        (*pos)++;
        text++;
    }
    line[*pos] = '\0';
}

static void audio_log_stream_state(const char *prefix)
{
    char line[96];
    char value[12];
    uint32_t pos = 0;

    if (prefix == NULL) {
        prefix = "audio: stream ";
    }
    audio_log_append(line, &pos, prefix);
    audio_log_append(line, &pos, "pos=");
    audio_append_dec(value, g_audio_stream_pos);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, "/");
    audio_append_dec(value, g_audio_data_size);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " lvi=");
    audio_append_dec(value, g_audio_stream_lvi);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " civ=");
    audio_append_dec(value, audio_read8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CIV)) & (AC97_BDL_COUNT - 1u));
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " sr=");
    audio_append_dec(value, g_audio_device.ac97_status);
    audio_log_append(line, &pos, value);
    log_write(line);
}

static void audio_log_es1371_runtime(const char *tag, uint32_t period_index, uint32_t hw_frame, uint32_t periods)
{
    char line[96];
    char value[12];
    uint32_t pos = 0;

    audio_log_append(line, &pos, tag != NULL ? tag : "audio: es1371 ");
    audio_log_append(line, &pos, "sw=");
    audio_append_dec(value, period_index);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " hwf=");
    audio_append_dec(value, hw_frame);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " pos=");
    audio_append_dec(value, g_audio_stream_pos);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, "/");
    audio_append_dec(value, g_audio_data_size);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " pend=");
    audio_append_dec(value, g_audio_es1371_irq_pending);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " ev=");
    audio_append_dec(value, g_audio_es1371_period_events);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " src=");
    audio_append_dec(value, periods);
    audio_log_append(line, &pos, value);
    log_write(line);
}

static void audio_log_es1371_period_state(uint32_t period_index, uint32_t bytes_read, uint32_t dst_frames, const int16_t *dst)
{
    char line[96];
    char value[12];
    uint32_t pos = 0;
    uint32_t samples;
    uint32_t avg;
    uint64_t sum = 0;

    samples = dst_frames * 2u;
    if (samples > 1024u) {
        samples = 1024u;
    }
    for (uint32_t i = 0; i < samples; i++) {
        int32_t sample = dst[i];

        sum += (uint32_t) (sample < 0 ? -sample : sample);
    }
    avg = samples == 0 ? 0 : (uint32_t) (sum / samples);

    audio_log_append(line, &pos, "audio: es1371 period=");
    audio_append_dec(value, period_index);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " bytes=");
    audio_append_dec(value, bytes_read);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " frames=");
    audio_append_dec(value, dst_frames);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " avg=");
    audio_append_dec(value, avg);
    audio_log_append(line, &pos, value);
    audio_log_append(line, &pos, " cache=");
    audio_append_dec(value, g_audio_es1371_cache_size);
    audio_log_append(line, &pos, value);
    log_write(line);
}

static bool __attribute__((unused)) audio_resample_stereo_s16(const int16_t *src, uint32_t src_frames, uint32_t src_rate, uint32_t dst_rate, int16_t **out_pcm, uint32_t *out_frames)
{
    int16_t *dst;
    uint64_t frame_count64;

    if (src == NULL || out_pcm == NULL || out_frames == NULL || src_frames == 0 || src_rate == 0 || dst_rate == 0) {
        return false;
    }

    frame_count64 = ((uint64_t) src_frames * (uint64_t) dst_rate + (uint64_t) src_rate - 1u) / (uint64_t) src_rate;
    if (frame_count64 == 0 || frame_count64 > 0x7FFFFFFFu) {
        return false;
    }

    *out_frames = (uint32_t) frame_count64;
    dst = (int16_t *) kmalloc(*out_frames * 4u);
    if (dst == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < *out_frames; i++) {
        uint64_t src_pos = (((uint64_t) i * (uint64_t) src_rate) << 16) / (uint64_t) dst_rate;
        uint32_t src_index = (uint32_t) (src_pos >> 16);
        uint32_t frac = (uint32_t) (src_pos & 0xFFFFU);

        if (src_index + 1U >= src_frames) {
            src_index = src_frames - 1U;
            dst[i * 2U] = src[src_index * 2U];
            dst[i * 2U + 1U] = src[src_index * 2U + 1U];
        } else {
            int32_t left_a = src[src_index * 2U];
            int32_t left_b = src[(src_index + 1U) * 2U];
            int32_t right_a = src[src_index * 2U + 1U];
            int32_t right_b = src[(src_index + 1U) * 2U + 1U];

            dst[i * 2U] = (int16_t) (left_a + (int32_t) (((int64_t) (left_b - left_a) * (int64_t) frac) >> 16));
            dst[i * 2U + 1U] = (int16_t) (right_a + (int32_t) (((int64_t) (right_b - right_a) * (int64_t) frac) >> 16));
        }
    }

    *out_pcm = dst;
    return true;
}

static uint32_t audio_pci_bar_base(uint32_t bar)
{
    if ((bar & 0x1u) == 0) {
        return 0;
    }
    return bar & 0xFFFFFFFCu;
}

static bool audio_io_bar_valid(uint32_t base, uint32_t size)
{
    if (base < 0x100u) {
        return false;
    }
    if (base + size > 0x10000u) {
        return false;
    }
    return true;
}

static bool audio_is_supported_ac97(const pci_device_info_t *info)
{
    return info->vendor_id == PCI_VENDOR_INTEL && info->device_id == PCI_DEVICE_ICH_AC97;
}

static void audio_busy_delay(uint32_t loops)
{
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

static void __attribute__((unused)) audio_pc_speaker_tone(uint32_t hz, uint32_t loops)
{
    uint32_t divisor;
    uint8_t ctrl;

    if (hz == 0) {
        return;
    }

    divisor = 1193182u / hz;
    outb(PIT_COMMAND, 0xB6);
    outb(PIT_CHANNEL2, (uint8_t) (divisor & 0xFF));
    outb(PIT_CHANNEL2, (uint8_t) ((divisor >> 8) & 0xFF));

    ctrl = inb(PC_SPEAKER_CTRL);
    outb(PC_SPEAKER_CTRL, (uint8_t) (ctrl | 0x03));
    audio_busy_delay(loops);
    outb(PC_SPEAKER_CTRL, (uint8_t) (inb(PC_SPEAKER_CTRL) & 0xFC));
}

static uint16_t audio_ac97_volume_register(uint8_t percent)
{
    uint16_t attenuation;

    if (percent == 0) {
        return 0x8000u;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    attenuation = (uint16_t) (((100u - percent) * 31u) / 100u);
    return (uint16_t) ((attenuation << 8) | attenuation);
}

static void audio_apply_volume(void)
{
    uint16_t value;

    if (!g_audio_device.present || !g_audio_hw_initialized) {
        return;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        es1371_set_volume(&g_audio_device, g_audio_volume);
        return;
    }
    if (g_audio_device.kind != AUDIO_DEVICE_AC97) {
        return;
    }
    value = audio_ac97_volume_register(g_audio_volume);
    audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_MASTER_VOL), value);
    audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_PCM_OUT_VOL), value);
}

void audio_play_pc_speaker_beep(void)
{
    log_write("audio: pc speaker startup beep disabled");
}

static void audio_ac97_enable_pci(const audio_device_info_t *device)
{
    uint16_t command = pci_config_read16(device->bus, device->slot, device->func, PCI_COMMAND_OFFSET);

    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(device->bus, device->slot, device->func, PCI_COMMAND_OFFSET, command);
}

static bool audio_ac97_wait_codec_ready(void)
{
    for (uint32_t i = 0; i < 50000; i++) {
        uint16_t reset_caps = audio_read16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_RESET));

        if (reset_caps != 0xFFFFu && reset_caps != 0x0000u) {
            return true;
        }
        io_wait();
    }
    return false;
}

static void audio_ac97_stop_pcm_out(void)
{
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), 0);
    for (uint32_t i = 0; i < 10000; i++) {
        if ((audio_read8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR)) & AC97_X_CR_RPBM) == 0) {
            break;
        }
        io_wait();
    }
}

static void audio_ac97_reset_pcm_out(void)
{
    audio_ac97_stop_pcm_out();
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), AC97_X_CR_RR);
    for (uint32_t i = 0; i < 10000; i++) {
        if ((audio_read8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR)) & AC97_X_CR_RR) == 0) {
            break;
        }
        io_wait();
    }
    audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
}

static bool audio_ac97_set_output_format(uint32_t sample_rate)
{
    audio_apply_volume();
    audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_POWERDOWN_CTRL), 0x0000);

    if (g_audio_device.variable_rate_audio) {
        audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_EXT_AUDIO_CTRL), AC97_EXT_AUDIO_VRA);
        audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_PCM_FRONT_RATE), (uint16_t) sample_rate);
    }
    return sample_rate == AC97_TEST_SAMPLE_RATE || g_audio_device.variable_rate_audio;
}

static bool __attribute__((unused)) audio_ac97_prepare_pcm_out(uint32_t frame_count, uint32_t sample_rate, const char *track_name)
{
    uint32_t samples = frame_count * AC97_STEREO_CHANNELS;
    uint32_t max_frames = audio_dma_pcm_capacity_frames();

    if (!g_audio_hw_initialized || g_audio_dma == NULL || g_bdl == NULL || frame_count == 0) {
        log_write("audio: ac97 prepare invalid state");
        return false;
    }
    if (frame_count > max_frames) {
        log_write("audio: ac97 prepare frame limit");
        return false;
    }
    if (samples > AC97_BDL_MAX_SAMPLES) {
        log_write("audio: ac97 prepare sample limit");
        return false;
    }
    if (!audio_ac97_set_output_format(sample_rate)) {
        log_write("audio: unsupported ac97 sample rate");
        return false;
    }

    audio_ac97_reset_pcm_out();
    memset(g_bdl, 0, AC97_BDL_COUNT * sizeof(ac97_buffer_descriptor_t));
    g_bdl[0].offset = (uint32_t) (g_dma_buffer.physical_address + AC97_BDL_BYTES);
    g_bdl[0].length = (uint16_t) samples;
    g_bdl[0].control = AC97_BDL_IOC;
    audio_write32((uint16_t) (g_audio_device.bus_master_base + AC97_PO_BDBAR), (uint32_t) g_dma_buffer.physical_address);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_LVI), 0);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), AC97_X_CR_RPBM);

    strcpy(g_audio_track_name, track_name != NULL ? track_name : "AC97 PCM");
    g_audio_dma_frames = frame_count;
    g_audio_total_samples = samples;
    g_audio_started = true;
    g_audio_paused = false;
    g_audio_device.ac97_status = audio_read16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR));
    return true;
}

static bool audio_detect_callback(const pci_device_info_t *info, void *ctx)
{
    audio_device_info_t *device = (audio_device_info_t *) ctx;

    if (info->class_code != PCI_CLASS_MULTIMEDIA ||
        (info->subclass != PCI_SUBCLASS_AUDIO && info->subclass != PCI_SUBCLASS_HDA)) {
        return true;
    }

    memset(device, 0, sizeof(*device));
    device->vendor_id = info->vendor_id;
    device->device_id = info->device_id;
    device->bus = info->bus;
    device->slot = info->slot;
    device->func = info->func;
    device->mixer_base = audio_pci_bar_base(info->bar0);
    device->bus_master_base = audio_pci_bar_base(info->bar1);
    device->irq_line = info->interrupt_line;
    device->onboard = true;
    device->ac97_ext_audio_id = 0;
    device->ac97_status = 0;
    device->variable_rate_audio = false;
    device->present = true;

    if (info->subclass == PCI_SUBCLASS_HDA) {
        device->kind = AUDIO_DEVICE_HDA;
        device->mixer_base = info->bar0 & 0xFFFFFFF0u;
        device->bus_master_base = 0;
    } else if (audio_is_supported_ac97(info)) {
        device->kind = AUDIO_DEVICE_AC97;
    } else if (es1371_supported(info)) {
        device->kind = AUDIO_DEVICE_ES1371;
    } else if (info->prog_if == 0x80) {
        device->kind = AUDIO_DEVICE_HDA;
    } else {
        device->kind = AUDIO_DEVICE_SB16;
    }

    return false;
}

static bool audio_irq_handler(uint8_t irq, void *ctx)
{
    (void) ctx;

    if (!g_audio_device.present || irq != g_audio_device.irq_line) {
        return false;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_ES1371 && es1371_pcm_interrupt_pending(&g_audio_device)) {
        es1371_clear_pcm_interrupt(&g_audio_device);
        g_audio_es1371_irq_pending++;
        g_audio_es1371_irq_total++;
        return true;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
        uint16_t status = audio_read16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR));

        if ((status & AC97_X_SR_CLEAR) != 0) {
            audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), status & AC97_X_SR_CLEAR);
            return true;
        }
    }
    return false;
}

static uint32_t audio_dma_pcm_capacity_frames(void)
{
    if (g_audio_dma == NULL) {
        return 0;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        return (uint32_t) g_dma_buffer.size / 4u;
    }
    if (g_dma_buffer.size <= AC97_BDL_BYTES) {
        return 0;
    }
    return ((uint32_t) g_dma_buffer.size - AC97_BDL_BYTES) / 4u;
}

static void audio_release_pcm_source(void)
{
    if (g_audio_pcm_source != NULL) {
        kfree(g_audio_pcm_source);
        g_audio_pcm_source = NULL;
    }
    g_audio_pcm_source_size = 0;
}

static bool audio_read_source(uint32_t offset, void *buffer, uint32_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }
    if (g_audio_pcm_source != NULL) {
        if (offset > g_audio_pcm_source_size ||
            size > g_audio_pcm_source_size - offset) {
            return false;
        }
        memcpy(buffer, g_audio_pcm_source + offset, size);
        return true;
    }
    if (g_audio_stream_path[0] == '\0') {
        return false;
    }
    return file_read_at(g_audio_stream_path, g_audio_data_offset + offset,
                        buffer, size) == (int32_t) size;
}

static void audio_stop_current_playback(void)
{
    if (g_audio_device.present && g_audio_hw_initialized) {
        if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
            es1371_stop(&g_audio_device);
        } else if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
            audio_ac97_stop_pcm_out();
        }
    }
    audio_release_pcm_source();
    if (g_audio_es1371_src_buffer != NULL) {
        kfree(g_audio_es1371_src_buffer);
        g_audio_es1371_src_buffer = NULL;
    }
    g_audio_es1371_src_buffer_size = 0;
    g_audio_started = false;
    g_audio_paused = false;
    g_audio_streaming = false;
    g_audio_stream_eof = false;
    g_audio_stream_path[0] = '\0';
    g_audio_stream_pos = 0;
    g_audio_data_offset = 0;
    g_audio_data_size = 0;
    g_audio_stream_rate = 0;
    g_audio_es1371_cache_start = 0;
    g_audio_es1371_cache_size = 0;
    g_audio_es1371_stop_tick = 0;
    g_audio_es1371_chunk_start_tick = 0;
    g_audio_es1371_chunk_ticks = 0;
    g_audio_es1371_period_frames = 0;
    g_audio_es1371_ring_frames = 0;
    g_audio_dma_frames = 0;
    g_audio_total_samples = 0;
    g_audio_track_name[0] = '\0';
    g_track.channels = 0;
    g_track.sample_rate = 0;
    g_track.bits_per_sample = 0;
    g_track.data_size = 0;
}

static bool audio_prime_ac97_stream_ring(void)
{
    uint32_t filled = 0;

    if (!g_audio_hw_initialized || g_audio_dma == NULL || g_bdl == NULL) {
        return false;
    }

    memset(g_bdl, 0, AC97_BDL_COUNT * sizeof(ac97_buffer_descriptor_t));
    memset(g_audio_dma, 0, AC97_DMA_BYTES);
    memset(g_audio_stream_desc_busy, 0, sizeof(g_audio_stream_desc_busy));
    g_audio_stream_eof = false;
    g_audio_stream_lvi = 0;
    g_audio_stream_last_civ = 0;
    g_audio_stream_last_log_pos = g_audio_stream_pos;

    for (uint32_t i = 0; i < AC97_BDL_COUNT; i++) {
        if (!audio_fill_stream_descriptor(i)) {
            break;
        }
        filled++;
        g_audio_stream_lvi = i;
        if (g_audio_stream_eof) {
            break;
        }
    }

    return filled > 0;
}

static void audio_clear_stream_descriptor(uint32_t index)
{
    uint8_t *dst;

    if (index >= AC97_BDL_COUNT || g_audio_dma == NULL || g_bdl == NULL) {
        return;
    }

    dst = (uint8_t *) g_audio_dma + index * AC97_STREAM_CHUNK_BYTES;
    memset(dst, 0, AC97_STREAM_CHUNK_BYTES);
    g_bdl[index].offset = (uint32_t) (g_dma_buffer.physical_address + AC97_BDL_BYTES + index * AC97_STREAM_CHUNK_BYTES);
    g_bdl[index].length = 2;
    g_bdl[index].control = 0;
    g_audio_stream_desc_busy[index] = false;
}

static void audio_release_played_stream_descriptors(uint32_t civ)
{
    uint32_t index;
    uint32_t guard = 0;

    if (civ >= AC97_BDL_COUNT) {
        return;
    }

    index = g_audio_stream_last_civ;
    while (index != civ && guard < AC97_BDL_COUNT) {
        audio_clear_stream_descriptor(index);
        index = (index + 1u) & (AC97_BDL_COUNT - 1u);
        guard++;
    }
}

static bool audio_restart_ac97_stream_from_current(void)
{
    audio_ac97_stop_pcm_out();
    audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
    if (!audio_prime_ac97_stream_ring()) {
        return false;
    }
    g_audio_stream_recovery_count++;
    audio_write32((uint16_t) (g_audio_device.bus_master_base + AC97_PO_BDBAR), (uint32_t) g_dma_buffer.physical_address);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_LVI), (uint8_t) g_audio_stream_lvi);
    audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), AC97_X_CR_RPBM);
    return true;
}

void audio_init(void)
{
    memset(&g_audio_device, 0, sizeof(g_audio_device));
    memset(&g_track, 0, sizeof(g_track));
    memset(&g_dma_buffer, 0, sizeof(g_dma_buffer));
    g_bdl = NULL;
    g_audio_dma = NULL;
    g_audio_started = false;
    g_audio_paused = false;
    g_audio_hw_initialized = false;
    g_audio_volume = 90;
    g_audio_dma_frames = 0;
    g_audio_total_samples = 0;
    g_audio_track_name[0] = '\0';
    g_audio_es1371_src_buffer = NULL;
    g_audio_es1371_src_buffer_size = 0;
    g_audio_pcm_source = NULL;
    g_audio_pcm_source_size = 0;
    g_audio_es1371_irq_pending = 0;
    g_audio_es1371_irq_total = 0;
    g_audio_es1371_chunk_start_tick = 0;
    g_audio_es1371_chunk_ticks = 0;
    pci_enumerate(audio_detect_callback, &g_audio_device);
    if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_AC97) {
        if (audio_init_ac97_hw()) {
            audio_apply_volume();
            if (g_audio_device.irq_line < 16) {
                (void) interrupt_register_irq_handler(g_audio_device.irq_line, audio_irq_handler, NULL);
            }
        }
    } else if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_HDA) {
        g_audio_hw_initialized = hda_driver_init();
    } else if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        if (es1371_init(&g_audio_device, &g_dma_buffer, &g_audio_dma)) {
            g_audio_hw_initialized = true;
            audio_apply_volume();
            if (g_audio_device.irq_line < 16) {
                (void) interrupt_register_irq_handler(g_audio_device.irq_line, audio_irq_handler, NULL);
            }
        }
    }
}

const audio_device_info_t *audio_primary_device(void)
{
    return &g_audio_device;
}

void audio_log_state(void)
{
    char line[64] = "audio: ";

    if (!g_audio_device.present) {
        log_write("audio: no pci audio device found");
        return;
    }

    switch (g_audio_device.kind) {
    case AUDIO_DEVICE_AC97:
        strcpy(line + 7, "ac97 detected");
        break;
    case AUDIO_DEVICE_HDA:
        strcpy(line + 7, g_audio_hw_initialized ? "hda onboard ready" : "hda detected");
        break;
    case AUDIO_DEVICE_SB16:
        strcpy(line + 7, "legacy/sb16-style detected");
        break;
    case AUDIO_DEVICE_ES1371:
        strcpy(line + 7, "onboard es1371 detected");
        break;
    default:
        strcpy(line + 7, "unknown audio device");
        break;
    }
    log_write(line);
}

static bool audio_init_ac97_hw(void)
{
    uint16_t ext_audio_id;
    char line[40] = "audio: ac97 codec ";
    char hex[5];

    if (!g_audio_device.present || g_audio_device.kind != AUDIO_DEVICE_AC97) {
        return false;
    }
    if (!audio_io_bar_valid(g_audio_device.mixer_base, AC97_MIXER_IO_BYTES) ||
        !audio_io_bar_valid(g_audio_device.bus_master_base, AC97_BUS_MASTER_IO_BYTES)) {
        log_write("audio: ac97 bars invalid");
        return false;
    }

    audio_ac97_enable_pci(&g_audio_device);
    if (!audio_ac97_wait_codec_ready()) {
        log_write("audio: ac97 codec not ready, continuing");
    }

    if (!dma_alloc(AC97_DMA_BYTES + AC97_BDL_BYTES, 4096, 0xFFFFFFFFu, &g_dma_buffer)) {
        log_write("audio: dma alloc failed");
        return false;
    }

    g_bdl = (ac97_buffer_descriptor_t *) g_dma_buffer.virtual_address;
    g_audio_dma = (int16_t *) ((uint8_t *) g_dma_buffer.virtual_address + AC97_BDL_BYTES);
    memset(g_bdl, 0, AC97_BDL_COUNT * sizeof(ac97_buffer_descriptor_t));
    memset(g_audio_dma, 0, AC97_DMA_BYTES);

    audio_write32((uint16_t) (g_audio_device.bus_master_base + AC97_GLOB_CNT), 0x00000002);
    audio_ac97_reset_pcm_out();

    ext_audio_id = audio_read16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_EXT_AUDIO_ID));
    g_audio_device.ac97_ext_audio_id = ext_audio_id;
    g_audio_device.variable_rate_audio = (ext_audio_id & AC97_EXT_AUDIO_VRA) != 0;
    audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_POWERDOWN_CTRL), 0x0000);
    audio_ac97_set_output_format(AC97_TEST_SAMPLE_RATE);

    g_audio_hw_initialized = true;
    audio_append_hex4(hex, ext_audio_id);
    strcpy(line + strlen(line), hex);
    log_write(line);
    log_write("audio: ac97 dma ready");
    return true;
}

static bool audio_read_wav_header(const uint8_t *data, uint32_t size, wav_pcm_fmt_t *fmt, uint32_t *data_offset)
{
    uint32_t pos = 0;

    if (size < 44 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) {
        return false;
    }
    pos = 12;
    while (pos + 8 <= size) {
        uint32_t chunk_size = *(const uint32_t *) (data + pos + 4);

        if (memcmp(data + pos, "fmt ", 4) == 0) {
            if (chunk_size < 16 || pos + 8 + chunk_size > size) {
                return false;
            }
            memcpy(fmt, data + pos + 8, sizeof(*fmt));
        } else if (memcmp(data + pos, "data", 4) == 0) {
            *data_offset = pos + 8;
            return true;
        }
        pos += 8 + chunk_size + (chunk_size & 1u);
    }
    return false;
}

/* Convert an arbitrary PCM WAV data chunk to 16-bit signed stereo interleaved.
 * Supports: 8-bit unsigned mono/stereo, 16-bit signed mono/stereo.
 * The converted buffer is kmalloc'd; caller frees with kfree. */
static bool wav_convert_to_pcm16_stereo(const char *path, uint32_t data_offset,
                                        const wav_pcm_fmt_t *fmt, uint8_t **out_buf,
                                        uint32_t *out_size)
{
    uint32_t raw_bytes;
    uint32_t frames;
    uint32_t out_bytes;
    uint8_t *raw;
    int16_t *out;
    uint32_t i;

    if (path == NULL || fmt == NULL || out_buf == NULL || out_size == NULL) {
        return false;
    }
    raw_bytes = (uint32_t) file_size(path) - data_offset;
    if (raw_bytes == 0 || fmt->channels == 0 || fmt->block_align == 0) {
        return false;
    }
    frames = raw_bytes / fmt->block_align;
    out_bytes = frames * 2u * (uint32_t) sizeof(int16_t);
    if (out_bytes == 0 || out_bytes > AUDIO_PCM_MAX_BYTES) {
        return false;
    }
    raw = (uint8_t *) kmalloc(raw_bytes);
    out = (int16_t *) kmalloc(out_bytes);
    if (raw == NULL || out == NULL) {
        kfree(raw);
        kfree(out);
        return false;
    }
    if (file_read_at(path, data_offset, raw, raw_bytes) != (int32_t) raw_bytes) {
        kfree(raw);
        kfree(out);
        return false;
    }

    if (fmt->bits_per_sample == 16 && fmt->channels == 2) {
        memcpy(out, raw, raw_bytes);
    } else if (fmt->bits_per_sample == 16 && fmt->channels == 1) {
        for (i = 0; i < frames; i++) {
            int16_t s = ((const int16_t *) raw)[i];
            out[i * 2u] = s;
            out[i * 2u + 1u] = s;
        }
    } else if (fmt->bits_per_sample == 8 && fmt->channels == 2) {
        for (i = 0; i < frames; i++) {
            out[i * 2u] = (int16_t) (raw[i * 2u] << 8) - 32768;
            out[i * 2u + 1u] = (int16_t) (raw[i * 2u + 1u] << 8) - 32768;
        }
    } else if (fmt->bits_per_sample == 8 && fmt->channels == 1) {
        for (i = 0; i < frames; i++) {
            int16_t s = (int16_t) (raw[i] << 8) - 32768;
            out[i * 2u] = s;
            out[i * 2u + 1u] = s;
        }
    } else {
        kfree(raw);
        kfree(out);
        return false;
    }

    kfree(raw);
    *out_buf = (uint8_t *) out;
    *out_size = out_bytes;
    return true;
}

bool audio_play_pcm(const void *data, uint32_t byte_count, uint32_t sample_rate,
                    uint16_t channels, uint16_t bits_per_sample)
{
    if (!g_audio_device.present || !g_audio_hw_initialized || g_audio_dma == NULL ||
        data == NULL || byte_count == 0 || byte_count > AUDIO_PCM_MAX_BYTES ||
        channels != 2 || bits_per_sample != 16 || sample_rate == 0 ||
        (byte_count & 3u) != 0) {
        log_write("audio: pcm buffer rejected");
        return false;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_AC97 &&
        !audio_ac97_set_output_format(sample_rate)) {
        log_write("audio: pcm ac97 rate unsupported");
        return false;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_ES1371 &&
        sample_rate != es1371_sample_rate()) {
        log_write("audio: pcm es1371 rate unsupported");
        return false;
    }
    if (g_audio_device.kind != AUDIO_DEVICE_AC97 &&
        g_audio_device.kind != AUDIO_DEVICE_ES1371) {
        log_write("audio: pcm device unsupported");
        return false;
    }

    audio_stop_current_playback();
    g_audio_pcm_source = (uint8_t *) kmalloc(byte_count);
    if (g_audio_pcm_source == NULL) {
        log_write("audio: pcm source alloc failed");
        return false;
    }
    memcpy(g_audio_pcm_source, data, byte_count);
    g_audio_pcm_source_size = byte_count;
    g_audio_dma_frames = audio_dma_pcm_capacity_frames();
    g_audio_total_samples = byte_count / sizeof(int16_t);
    g_audio_data_offset = 0;
    g_audio_data_size = byte_count;
    g_audio_stream_pos = 0;
    g_audio_stream_rate = sample_rate;
    g_audio_streaming = true;
    g_audio_stream_eof = false;
    g_audio_stream_last_log_pos = 0;
    g_audio_stream_recovery_count = 0;
    g_track.channels = (uint8_t) channels;
    g_track.sample_rate = sample_rate;
    g_track.bits_per_sample = bits_per_sample;
    g_track.data_size = byte_count;
    strcpy(g_audio_track_name, "PCM buffer");

    if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
        if (!audio_start_ac97_stream()) {
            audio_stop_current_playback();
            log_write("audio: pcm ac97 stream start failed");
            return false;
        }
    } else if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        if (!audio_start_es1371_stream()) {
            audio_stop_current_playback();
            log_write("audio: pcm es1371 stream start failed");
            return false;
        }
    } else {
        audio_stop_current_playback();
        return false;
    }
    log_write("audio: pcm buffer stream started");
    return true;
}

static bool __attribute__((unused)) audio_play_file(const char *path)
{
    aac_info_t info;
    char wav_path[96];
    uint32_t len;

    if (path == NULL) {
        return false;
    }
    len = (uint32_t) strlen(path);
    if (len >= 4 && strcasecmp(path + len - 4, ".wav") == 0) {
        return audio_play_wav_file(path);
    }
    if (len >= 4 && strcasecmp(path + len - 4, ".mp3") == 0) {
        return mp3_play_file(path);
    }
    if (len >= 4 && strcasecmp(path + len - 4, ".m4a") == 0) {
        if (len + 1 >= sizeof(wav_path)) {
            if (aac_probe_file(path, &info)) {
                log_write(aac_status());
                log_write("audio: aac/mp4 decode unavailable");
            } else {
                log_write(aac_status());
            }
            return false;
        }
        strcpy(wav_path, path);
        strcpy(wav_path + len - 4, ".wav");
        if (file_exists(wav_path)) {
            audio_log_path("audio: m4a sidecar ", wav_path);
            return audio_play_wav_file(wav_path);
        }
        if (aac_probe_file(path, &info)) {
            log_write(aac_status());
            log_write("audio: aac/mp4 decode unavailable");
        } else {
            log_write(aac_status());
        }
        return false;
    }
    if (len >= 4 && strcasecmp(path + len - 4, ".mp4") == 0) {
        if (aac_probe_file(path, &info)) {
            log_write(aac_status());
            log_write("audio: aac/mp4 decode unavailable");
        } else {
            log_write(aac_status());
        }
        return false;
    }
    if ((len >= 4 && strcasecmp(path + len - 4, ".aac") == 0) ||
        (len >= 5 && strcasecmp(path + len - 5, ".adts") == 0)) {
        if (aac_probe_file(path, &info)) {
            log_write(aac_status());
            log_write("audio: aac decode unavailable");
        } else {
            log_write(aac_status());
        }
        return false;
    }
    log_write("audio: unsupported media file");
    return false;
}

static uint64_t audio_frames_to_ticks(uint32_t sample_rate, uint32_t frames)
{
    uint64_t ticks;
    uint32_t hz;

    hz = timer_hz();
    if (hz == 0 || sample_rate == 0 || frames == 0) {
        return 1;
    }
    ticks = ((uint64_t) frames * (uint64_t) hz + (uint64_t) sample_rate - 1u) / (uint64_t) sample_rate;
    if (ticks == 0) {
        ticks = 1;
    }
    return ticks;
}

static bool audio_prefetch_es1371_stream_cache(uint32_t read_budget)
{
    uint32_t drop;
    uint32_t total_read = 0;

    if (g_audio_es1371_src_buffer == NULL || g_audio_es1371_src_buffer_size == 0 ||
        (g_audio_pcm_source == NULL && g_audio_stream_path[0] == '\0')) {
        return false;
    }
    if (read_budget == 0) {
        read_budget = g_audio_es1371_src_buffer_size;
    }
    if (g_audio_stream_pos > g_audio_es1371_cache_start) {
        drop = g_audio_stream_pos - g_audio_es1371_cache_start;
        if (drop >= g_audio_es1371_cache_size) {
            g_audio_es1371_cache_start = g_audio_stream_pos;
            g_audio_es1371_cache_size = 0;
        } else {
            uint32_t move_bytes = g_audio_es1371_cache_size - drop;

            for (uint32_t i = 0; i < move_bytes; i++) {
                g_audio_es1371_src_buffer[i] = g_audio_es1371_src_buffer[drop + i];
            }
            g_audio_es1371_cache_start = g_audio_stream_pos;
            g_audio_es1371_cache_size -= drop;
        }
    }
    while (g_audio_es1371_cache_size < g_audio_es1371_src_buffer_size &&
           g_audio_es1371_cache_start + g_audio_es1371_cache_size < g_audio_data_size) {
        uint32_t cache_end = g_audio_es1371_cache_start + g_audio_es1371_cache_size;
        uint32_t bytes_to_read = g_audio_es1371_src_buffer_size - g_audio_es1371_cache_size;
        uint32_t remaining = g_audio_data_size - cache_end;
        uint32_t budget_left = read_budget - total_read;

        if (bytes_to_read > budget_left) {
            bytes_to_read = budget_left;
        }

        if (bytes_to_read > remaining) {
            bytes_to_read = remaining;
        }
        bytes_to_read &= ~3u;
        if (bytes_to_read == 0) {
            break;
        }
        if (!audio_read_source(cache_end,
                               g_audio_es1371_src_buffer + g_audio_es1371_cache_size,
                               bytes_to_read)) {
            log_write("audio: es1371 cache read failed");
            return false;
        }
        g_audio_es1371_cache_size += bytes_to_read;
        total_read += bytes_to_read;
        if (total_read >= read_budget) {
            break;
        }
    }
    return true;
}

static bool audio_fill_es1371_stream_period(uint32_t period_index, bool *has_audio)
{
    uint32_t dst_rate = es1371_sample_rate();
    uint32_t max_dst_frames = g_audio_es1371_period_frames;
    uint32_t max_src_frames;
    uint32_t max_src_bytes;
    uint32_t cache_offset;
    uint32_t cache_available;
    uint32_t bytes_to_read;
    uint32_t src_frames;
    uint32_t dst_frames;
    int16_t *dst;
    const int16_t *src;

    if (has_audio != NULL) {
        *has_audio = false;
    }
    if (period_index >= ES1371_STREAM_PERIODS ||
        (g_audio_pcm_source == NULL && g_audio_stream_path[0] == '\0') ||
        g_audio_stream_rate == 0 ||
        max_dst_frames == 0 || g_audio_es1371_src_buffer == NULL || g_audio_dma == NULL) {
        log_write("audio: es1371 stream invalid");
        g_audio_started = false;
        g_audio_streaming = false;
        return false;
    }
    dst = g_audio_dma + period_index * max_dst_frames * 2u;

    max_src_frames = (uint32_t) (((uint64_t) max_dst_frames * (uint64_t) g_audio_stream_rate) / (uint64_t) dst_rate);
    if (max_src_frames == 0) {
        return false;
    }
    max_src_bytes = max_src_frames * 4u;
    if (max_src_bytes > g_audio_es1371_src_buffer_size) {
        max_src_bytes = g_audio_es1371_src_buffer_size;
    }

    if (g_audio_stream_pos >= g_audio_data_size) {
        memset(dst, 0, max_dst_frames * 4u);
        audio_log_es1371_runtime("audio: es1371 eofzero ", period_index,
                                  es1371_playback_position_frames(&g_audio_device, g_audio_es1371_ring_frames),
                                  0);
        return true;
    }
    if (g_audio_stream_pos < g_audio_es1371_cache_start ||
        g_audio_stream_pos >= g_audio_es1371_cache_start + g_audio_es1371_cache_size) {
        if (!audio_prefetch_es1371_stream_cache(ES1371_CACHE_READ_BYTES)) {
            g_audio_started = false;
            g_audio_streaming = false;
            return false;
        }
    }
    if (g_audio_stream_pos < g_audio_es1371_cache_start ||
        g_audio_stream_pos >= g_audio_es1371_cache_start + g_audio_es1371_cache_size) {
        memset(dst, 0, max_dst_frames * 4u);
        g_audio_es1371_underruns++;
        log_write("audio: es1371 cache underrun");
        return true;
    }
    cache_offset = g_audio_stream_pos - g_audio_es1371_cache_start;
    cache_available = g_audio_es1371_cache_size - cache_offset;
    bytes_to_read = g_audio_data_size - g_audio_stream_pos;
    if (bytes_to_read > max_src_bytes) {
        bytes_to_read = max_src_bytes;
    }
    if (bytes_to_read > cache_available) {
        bytes_to_read = cache_available;
    }
    bytes_to_read &= ~3u;
    if (bytes_to_read == 0) {
        memset(dst, 0, max_dst_frames * 4u);
        g_audio_es1371_underruns++;
        log_write("audio: es1371 cache underrun");
        return true;
    }

    src_frames = bytes_to_read / 4u;
    src = (const int16_t *) (g_audio_es1371_src_buffer + cache_offset);
    dst_frames = (uint32_t) (((uint64_t) src_frames * (uint64_t) dst_rate + (uint64_t) g_audio_stream_rate - 1u) / (uint64_t) g_audio_stream_rate);
    if (dst_frames > max_dst_frames) {
        dst_frames = max_dst_frames;
    }
    if (dst_frames < max_dst_frames) {
        memset(dst + dst_frames * 2u, 0, (max_dst_frames - dst_frames) * 4u);
    }
    for (uint32_t i = 0; i < dst_frames; i++) {
        uint32_t src_index = (uint32_t) (((uint64_t) i * (uint64_t) g_audio_stream_rate) / (uint64_t) dst_rate);

        if (src_index >= src_frames) {
            src_index = src_frames - 1u;
        }
        dst[i * 2u] = src[src_index * 2u];
        dst[i * 2u + 1u] = src[src_index * 2u + 1u];
    }

    g_audio_stream_pos += bytes_to_read;
    audio_prefetch_es1371_stream_cache(ES1371_CACHE_READ_BYTES);
    if (has_audio != NULL) {
        *has_audio = true;
    }
    audio_log_es1371_period_state(period_index, bytes_to_read, dst_frames, dst);
    if (g_audio_stream_pos == bytes_to_read || g_audio_stream_pos >= g_audio_stream_last_log_pos + AC97_STREAM_LOG_BYTES || g_audio_stream_pos >= g_audio_data_size) {
        log_write("audio: es1371 fill");
        g_audio_stream_last_log_pos = g_audio_stream_pos;
    }
    if ((g_audio_es1371_period_events & 0x0Fu) == 0 || g_audio_stream_pos >= g_audio_data_size) {
        audio_log_es1371_runtime("audio: es1371 fillp ", period_index,
                                  es1371_playback_position_frames(&g_audio_device, g_audio_es1371_ring_frames),
                                  bytes_to_read);
    }
    return true;
}

static bool audio_start_es1371_stream(void)
{
    uint32_t dst_rate = es1371_sample_rate();
    uint32_t max_dst_frames = audio_dma_pcm_capacity_frames();
    uint32_t max_src_frames;
    uint32_t max_src_bytes;
    uint32_t cache_bytes;
    bool has_audio = false;

    if ((g_audio_pcm_source == NULL && g_audio_stream_path[0] == '\0') ||
        g_audio_stream_rate == 0 || max_dst_frames == 0) {
        return false;
    }
    if (max_dst_frames > ES1371_STREAM_FRAMES) {
        max_dst_frames = ES1371_STREAM_FRAMES;
    }
    max_dst_frames &= ~(ES1371_STREAM_PERIODS - 1u);
    if (max_dst_frames < ES1371_STREAM_PERIODS) {
        return false;
    }
    g_audio_es1371_period_frames = max_dst_frames / ES1371_STREAM_PERIODS;
    max_src_frames = (uint32_t) (((uint64_t) max_dst_frames * (uint64_t) g_audio_stream_rate) / (uint64_t) dst_rate);
    if (max_src_frames == 0) {
        return false;
    }
    max_src_bytes = max_src_frames * 4u;
    cache_bytes = ES1371_SOURCE_CACHE_BYTES;
    if (cache_bytes < max_src_bytes) {
        cache_bytes = max_src_bytes;
    }
    if (g_audio_es1371_src_buffer != NULL) {
        kfree(g_audio_es1371_src_buffer);
        g_audio_es1371_src_buffer = NULL;
    }
    g_audio_es1371_src_buffer = (uint8_t *) kmalloc(cache_bytes);
    if (g_audio_es1371_src_buffer == NULL) {
        log_write("audio: es1371 stream alloc failed");
        return false;
    }
    g_audio_es1371_src_buffer_size = cache_bytes;
    g_audio_es1371_cache_start = 0;
    g_audio_es1371_cache_size = 0;
    g_audio_es1371_underruns = 0;
    g_audio_es1371_draining = false;
    g_audio_es1371_ring_frames = 0;
    g_audio_es1371_period_events = 0;
    g_audio_es1371_last_hw_period = 0;
    g_audio_es1371_last_refill_period = 0;
    memset(g_audio_dma, 0, max_dst_frames * 4u);
    g_audio_es1371_stop_tick = 0;
    g_audio_es1371_next_period = 0;
    if (!audio_prefetch_es1371_stream_cache(0)) {
        return false;
    }
    for (uint32_t i = 0; i < ES1371_STREAM_PERIODS; i++) {
        bool period_has_audio = false;

        if (!audio_fill_es1371_stream_period(i, &period_has_audio)) {
            return false;
        }
        if (period_has_audio) {
            has_audio = true;
        }
    }
    g_audio_es1371_chunk_start_tick = timer_ticks();
    g_audio_es1371_chunk_ticks = audio_frames_to_ticks(dst_rate, g_audio_es1371_period_frames);
    g_audio_es1371_ring_frames = max_dst_frames;
    g_audio_es1371_last_refill_period = ES1371_STREAM_PERIODS - 1u;
    g_audio_es1371_irq_pending = 0;
    g_audio_es1371_irq_total = 0;
    if (!has_audio) {
        g_audio_es1371_stop_tick = g_audio_es1371_chunk_start_tick + g_audio_es1371_chunk_ticks;
    }
    if (!es1371_prepare_pcm_out(&g_audio_device, &g_dma_buffer, max_dst_frames, g_audio_es1371_period_frames, true)) {
        log_write("audio: es1371 stream prepare failed");
        return false;
    }
    g_audio_started = true;
    g_audio_paused = false;
    g_audio_streaming = true;
    g_audio_stream_last_log_pos = 0;
    g_audio_es1371_next_period = 0;
    audio_log_es1371_runtime("audio: es1371 start ", g_audio_es1371_next_period, 0, ES1371_STREAM_PERIODS);
    log_write("audio: es1371 wav stream start");
    return true;
}

static bool audio_play_wav_file(const char *path)
{
    uint8_t header[512];
    int32_t wav_file_size;
    wav_pcm_fmt_t fmt;
    uint32_t data_offset = 0;
    uint32_t track_rate;
    uint32_t stream_size;

    audio_log_path("audio: wav request ", path);
    if (!g_audio_device.present || path == NULL || !g_audio_hw_initialized) {
        log_write("audio: wav unavailable");
        return false;
    }

    if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        es1371_stop(&g_audio_device);
    } else if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
        audio_ac97_stop_pcm_out();
    }
    g_audio_started = false;
    g_audio_paused = false;
    g_audio_total_samples = 0;
    g_audio_dma_frames = 0;
    g_audio_data_offset = 0;
    g_audio_data_size = 0;
    g_audio_stream_pos = 0;
    g_audio_stream_rate = 0;
    g_audio_streaming = false;
    g_audio_stream_path[0] = '\0';
    g_audio_track_name[0] = '\0';
    g_audio_stream_last_log_pos = 0;
    g_audio_stream_recovery_count = 0;
    g_audio_es1371_chunk_start_tick = 0;
    g_audio_es1371_chunk_ticks = 0;
    g_audio_es1371_stop_tick = 0;
    g_audio_es1371_period_frames = 0;
    g_audio_es1371_next_period = 0;
    g_audio_es1371_cache_start = 0;
    g_audio_es1371_cache_size = 0;
    g_audio_es1371_underruns = 0;
    g_audio_es1371_draining = false;
    g_audio_es1371_ring_frames = 0;
    g_audio_es1371_period_events = 0;
    g_audio_es1371_last_hw_period = 0;
    g_audio_es1371_last_refill_period = 0;
    if (g_audio_es1371_src_buffer != NULL) {
        kfree(g_audio_es1371_src_buffer);
        g_audio_es1371_src_buffer = NULL;
        g_audio_es1371_src_buffer_size = 0;
    }
    g_track.channels = 0;
    g_track.sample_rate = 0;
    g_track.bits_per_sample = 0;
    g_track.data_size = 0;

    wav_file_size = file_size(path);
    if (wav_file_size <= 0) {
        log_write("audio: wav file missing");
        return false;
    }

    if (file_read_at(path, 0, header, sizeof(header)) <= 0) {
        log_write("audio: wav file read failed");
        return false;
    }
    memset(&fmt, 0, sizeof(fmt));
    if (!audio_read_wav_header(header, sizeof(header), &fmt, &data_offset)) {
        log_write("audio: wav header invalid");
        return false;
    }
    if (fmt.format_tag != 1) {
        log_write("audio: wav format unsupported (not PCM)");
        return false;
    }
    if (fmt.channels != 1 && fmt.channels != 2) {
        log_write("audio: wav channel count unsupported");
        return false;
    }
    if (fmt.bits_per_sample != 8 && fmt.bits_per_sample != 16) {
        log_write("audio: wav bits per sample unsupported");
        return false;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_AC97 && fmt.samples_per_sec != 44100 && !g_audio_device.variable_rate_audio) {
        log_write("audio: wav ac97 rate unsupported");
        return false;
    }
    if ((uint32_t) wav_file_size <= data_offset) {
        log_write("audio: wav data missing");
        return false;
    }
    track_rate = fmt.samples_per_sec;
    if (fmt.channels == 2 && fmt.bits_per_sample == 16) {
        stream_size = (uint32_t) wav_file_size - data_offset;
        g_audio_pcm_source = NULL;
        g_audio_pcm_source_size = 0;
        g_audio_data_offset = data_offset;
    } else {
        uint8_t *converted = NULL;
        uint32_t converted_size = 0;
        if (!wav_convert_to_pcm16_stereo(path, data_offset, &fmt, &converted, &converted_size)) {
            log_write("audio: wav conversion failed");
            return false;
        }
        audio_release_pcm_source();
        g_audio_pcm_source = converted;
        g_audio_pcm_source_size = converted_size;
        stream_size = converted_size;
        g_audio_data_offset = 0;
    }
    g_track.channels = 2;
    g_track.sample_rate = track_rate;
    g_track.bits_per_sample = 16;
    g_track.data_size = stream_size;
    g_audio_data_size = stream_size;
    g_audio_stream_pos = 0;
    g_audio_stream_rate = track_rate;
    g_audio_streaming = true;
    g_audio_stream_last_log_pos = 0;
    g_audio_stream_recovery_count = 0;
    g_audio_dma_frames = AC97_DMA_BYTES / 4u;
    g_audio_total_samples = stream_size / 2u;
    if (g_audio_pcm_source == NULL) {
        strcpy(g_audio_stream_path, path);
    } else {
        g_audio_stream_path[0] = 0;
    }
    strcpy(g_audio_track_name, path);
    strcpy(g_audio_track_name, path);
    if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
        if (!audio_start_ac97_stream()) {
            g_audio_streaming = false;
            g_audio_track_name[0] = '\0';
            return false;
        }
    } else if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        audio_log_path("audio: playing wav ", path);
        if (!audio_start_es1371_stream()) {
            g_audio_streaming = false;
            g_audio_track_name[0] = '\0';
            return false;
        }
        return true;
    } else {
        log_write("audio: stream playback unsupported on this device");
        g_audio_streaming = false;
        g_audio_track_name[0] = '\0';
        return false;
    }
    audio_log_path("audio: playing wav ", path);
    return true;
}


/* Public media playback interfaces (audio.h). */
bool wav_play_file(const char *path)
{
    return audio_play_wav_file(path);
}

/* --- Minimal MP3 decoder scaffold ------------------------------------- */
/* MPEG1 Layer3 frame header layout (32-bit big-endian sync word). */
#define MP3H_MPEG1      0x20000000u   /* bit12: MPEG version 1      */
#define MP3H_LAYER3    0x02000000u   /* bit11-10: layer III         */
#define MP3H_SYNC       0xFFE00000u   /* 11 sync bits                */
#define MP3H_BR_IDX(x) (((x) >> 12) & 0x0Fu)
#define MP3H_SR_IDX(x) (((x) >> 10) & 0x03u)
#define MP3H_PADDING(x) (((x) >> 9) & 0x01u)
#define MP3H_STEREO(x) (((x) >> 6) & 0x03u)   /* 0/1 = stereo, 3 = mono */

static const uint16_t mp3_bitrate_mpeg1_l3[16] = {
    0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0
};
static const uint32_t mp3_srates_mpeg1[4] = { 44100u, 48000u, 32000u, 0u };

/* Parse the first 4KiB of an MP3 file for a valid MPEG1 Layer3 frame header.
 * Returns the byte offset of the first audio frame, and fills out the
 * nominal sample-rate / bitrate / stereo flags.
 * 实际解码在 mp3_play_file() -> mp3_decode_file()，这里只做帧几何诊断。 */
static bool mp3_find_first_frame(const char *path, uint32_t *frame_off,
                                  uint32_t *rate, uint32_t *bitrate, bool *stereo)
{
    uint8_t buf[4096];
    int32_t n;
    if (path == NULL || frame_off == NULL) return false;
    n = file_read_at(path, 0, buf, sizeof(buf));
    if (n < 4) return false;
    /* Skip an optional ID3v2 tag if present. */
    uint32_t pos = 0;
    if (n >= 10 && buf[0] == (uint8_t)'I' && buf[1] == (uint8_t)'D' && buf[2] == (uint8_t)'3') {
        uint32_t taglen = ((uint32_t)(buf[6] & 0x7F) << 21) |
                          ((uint32_t)(buf[7] & 0x7F) << 14) |
                          ((uint32_t)(buf[8] & 0x7F) << 7)  |
                          ((uint32_t)(buf[9] & 0x7F));
        pos = 10u + taglen;
        if (pos > (uint32_t)n - 4u) pos = 0;
    }
    while (pos + 4u <= (uint32_t)n) {
        uint32_t hdr = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) |
                       ((uint32_t)buf[pos+2] << 8)  | (uint32_t)buf[pos+3];
        if ((hdr & MP3H_SYNC) == MP3H_SYNC &&
            (hdr & MP3H_MPEG1) != 0 && (hdr & MP3H_LAYER3) != 0) {
            uint32_t br = mp3_bitrate_mpeg1_l3[MP3H_BR_IDX(hdr)];
            uint32_t sr = mp3_srates_mpeg1[MP3H_SR_IDX(hdr)];
            if (br != 0 && sr != 0) {
                *frame_off = pos;
                if (rate) *rate = sr;
                if (bitrate) *bitrate = br * 1000u;
                if (stereo) *stereo = (MP3H_STEREO(hdr) != 3u);
                return true;
            }
        }
        pos++;
    }
    return false;
}

bool mp3_play_file(const char *path)
{
    int16_t *pcm = NULL;
    uint32_t bytes = 0, frames = 0, rate = 0, channels = 0;
    uint32_t dst_rate;
    bool ok;

    audio_log_path("audio: mp3 request ", path);
    if (!g_audio_device.present || path == NULL || !g_audio_hw_initialized) {
        log_write("audio: mp3 unavailable");
        return false;
    }

    /* 完整解码：Huffman + 反量化 + IMDCT + 32 子带合成由
     * drivers/audio/mp3_dec.c（基于 vendored 的公有领域解码器）完成。 */
    if (!mp3_decode_file(path, &pcm, &bytes, &frames, &rate, &channels)) {
        uint32_t frame_off = 0, frate = 0, bitrate = 0;
        bool stereo = true;

        /* 解码失败时至少报告帧几何，便于区分"不是 MP3"和"解码出错"。 */
        if (mp3_find_first_frame(path, &frame_off, &frate, &bitrate, &stereo)) {
            log_write("audio: mp3 frame located but decode failed");
        } else {
            log_write("audio: mp3 no valid MPEG1 L3 frame");
        }
        log_write(mp3_dec_status());
        return false;
    }

    if (frames == 0 || bytes == 0 || channels != 2) {
        kfree(pcm);
        log_write("audio: mp3 decoded no stereo pcm");
        return false;
    }

    /* 设备采样率对齐：ES1371 有固定速率，AC97 走 44.1kHz。 */
    dst_rate = (g_audio_device.kind == AUDIO_DEVICE_ES1371)
                   ? es1371_sample_rate()
                   : AC97_TEST_SAMPLE_RATE;
    if (dst_rate != rate) {
        int16_t *conv = NULL;
        uint32_t conv_frames = 0;

        if (audio_resample_stereo_s16(pcm, frames, rate, dst_rate,
                                      &conv, &conv_frames) &&
            conv != NULL && conv_frames > 0) {
            kfree(pcm);
            pcm = conv;
            frames = conv_frames;
            bytes = conv_frames * 4u;
            rate = dst_rate;
        } else {
            log_write("audio: mp3 resample unavailable, using native rate");
        }
    }

    ok = audio_play_pcm(pcm, bytes, rate, 2, 16);
    kfree(pcm);

    if (ok) {
        log_write(mp3_dec_status());
        audio_log_path("audio: mp3 playing ", path);
    } else {
        log_write("audio: mp3 pcm submit rejected");
    }
    return ok;
}

static bool audio_fill_stream_descriptor(uint32_t index)
{
    uint32_t remaining;
    uint32_t bytes_to_read;
    uint32_t samples;
    uint8_t *dst;

    if (index >= AC97_BDL_COUNT ||
        (g_audio_pcm_source == NULL && g_audio_stream_path[0] == '\0') ||
        g_audio_stream_desc_busy[index]) {
        return false;
    }
    dst = (uint8_t *) g_audio_dma + index * AC97_STREAM_CHUNK_BYTES;

    if (g_audio_stream_pos >= g_audio_data_size) {
        audio_clear_stream_descriptor(index);
        g_audio_stream_eof = true;
        return false;
    }

    remaining = g_audio_data_size - g_audio_stream_pos;
    bytes_to_read = AC97_STREAM_CHUNK_BYTES;
    if (bytes_to_read > remaining) {
        bytes_to_read = remaining;
    }
    if (bytes_to_read == 0) {
        g_audio_stream_eof = true;
        return false;
    }
    if ((bytes_to_read & 3u) != 0) {
        bytes_to_read &= ~3u;
        if (bytes_to_read == 0) {
            g_audio_stream_pos = g_audio_data_size;
            g_audio_stream_eof = true;
            return false;
        }
    }
    memset(dst, 0, AC97_STREAM_CHUNK_BYTES);
    if (!audio_read_source(g_audio_stream_pos, dst, bytes_to_read)) {
        log_write("audio: stream read failed");
        return false;
    }

    samples = bytes_to_read / 2u;
    g_audio_stream_pos += bytes_to_read;
    g_bdl[index].offset = (uint32_t) (g_dma_buffer.physical_address + AC97_BDL_BYTES + index * AC97_STREAM_CHUNK_BYTES);
    g_bdl[index].length = (uint16_t) samples;
    g_bdl[index].control = (g_audio_stream_pos >= g_audio_data_size) ? AC97_BDL_IOC : 0;
    g_audio_stream_desc_busy[index] = true;
    if (g_audio_stream_pos >= g_audio_data_size) {
        g_audio_stream_eof = true;
    }
    if (g_audio_stream_eof || g_audio_stream_pos >= g_audio_stream_last_log_pos + AC97_STREAM_LOG_BYTES) {
        audio_log_stream_state("audio: fill ");
        g_audio_stream_last_log_pos = g_audio_stream_pos;
    }
    return true;
}

static bool audio_start_ac97_stream(void)
{
    if (!g_audio_hw_initialized || g_audio_dma == NULL || g_bdl == NULL || !audio_ac97_set_output_format(g_audio_stream_rate)) {
        return false;
    }

    audio_ac97_reset_pcm_out();
    if (!audio_prime_ac97_stream_ring()) {
        return false;
    }

    audio_write32((uint16_t) (g_audio_device.bus_master_base + AC97_PO_BDBAR), (uint32_t) g_dma_buffer.physical_address);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_LVI), (uint8_t) g_audio_stream_lvi);
    audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
    audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), AC97_X_CR_RPBM);
    audio_log_stream_state("audio: start ");
    g_audio_started = true;
    g_audio_paused = false;
    g_audio_device.ac97_status = audio_read16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR));
    return true;
}

static void __attribute__((unused)) audio_fill_tone(int16_t *buffer, uint32_t *frame_cursor, uint32_t frames, uint32_t sample_rate, uint32_t hz)
{
    uint32_t half_period = sample_rate / (hz * 2u);
    int16_t amplitude = 18000;

    if (half_period == 0) {
        half_period = 1;
    }
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t phase = (i / half_period) & 1u;
        int16_t sample = phase == 0 ? amplitude : (int16_t) -amplitude;
        uint32_t frame = *frame_cursor;

        buffer[frame * 2u] = sample;
        buffer[frame * 2u + 1u] = sample;
        (*frame_cursor)++;
    }
}

static void __attribute__((unused)) audio_fill_silence(int16_t *buffer, uint32_t *frame_cursor, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++) {
        uint32_t frame = *frame_cursor;

        buffer[frame * 2u] = 0;
        buffer[frame * 2u + 1u] = 0;
        (*frame_cursor)++;
    }
}

bool audio_play_startup_chime(void)
{
    log_write("audio: startup test sound disabled");
    return false;
}

void audio_toggle_pause(void)
{
    if (g_audio_started) {
        g_audio_paused = !g_audio_paused;
        if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
            es1371_set_paused(&g_audio_device, g_audio_paused);
        } else if (g_audio_paused) {
            audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), 0);
        } else {
            audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CR), AC97_X_CR_RPBM);
        }
    }
}

bool audio_is_playing(void)
{
    return g_audio_started;
}

bool audio_is_paused(void)
{
    return g_audio_paused;
}

uint8_t audio_volume(void)
{
    return g_audio_volume;
}

void audio_set_volume(uint8_t percent)
{
    if (percent > 100u) {
        percent = 100u;
    }
    g_audio_volume = percent;
    audio_apply_volume();
}

const char *audio_current_track(void)
{
    return g_audio_track_name;
}

/* --- Task 19: music-player control --- */
uint32_t audio_player_position_bytes(void)
{
    return g_audio_stream_pos;
}

uint32_t audio_player_total_bytes(void)
{
    return g_audio_data_size;
}

void audio_player_stop(void)
{
    audio_stop_current_playback();
}

int32_t audio_player_ctl(const audio_player_ctl_request_t *request)
{
    if (request == NULL) {
        return -1;
    }
    switch (request->cmd) {
        case AUDIO_PLAYER_PAUSE_TOGGLE:
            audio_toggle_pause();
            break;
        case AUDIO_PLAYER_STOP:
            audio_stop_current_playback();
            break;
        case AUDIO_PLAYER_GET_POSITION:
            break;
        case AUDIO_PLAYER_PLAY:
            /* Resume from paused state. */
            if (g_audio_paused) {
                audio_toggle_pause();
            }
            break;
        case AUDIO_PLAYER_SEEK: {
            /* Jump to byte offset within the current stream.  For ES1371,
             * we reset the stream caches and restart from the new offset. */
            uint32_t target = request->position;
            if (target > g_audio_data_size) {
                target = g_audio_data_size;
            }
            target &= ~3u;
            if (g_audio_device.kind == AUDIO_DEVICE_ES1371 && g_audio_streaming) {
                g_audio_stream_pos = target;
                g_audio_es1371_cache_start = target;
                g_audio_es1371_cache_size = 0;
                g_audio_es1371_stop_tick = 0;
                g_audio_es1371_draining = false;
                (void) audio_prefetch_es1371_stream_cache(ES1371_CACHE_READ_BYTES);
            } else {
                g_audio_stream_pos = target;
                g_audio_stream_eof = false;
            }
            break;
        }
        default:
            return -1;
    }
    return 0;
}
void audio_update(void)
{
    /* Microphone recording pumps independently of playback. */
    audio_record_pump();
    if (!g_audio_started || g_audio_paused || !g_audio_device.present) {
        return;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        uint64_t now = timer_ticks();
        bool tick_due;
        uint32_t periods = 0;

        if (g_audio_streaming && g_audio_es1371_stop_tick != 0 && now >= g_audio_es1371_stop_tick) {
            es1371_stop(&g_audio_device);
            g_audio_started = false;
            g_audio_streaming = false;
            log_write("audio: playback complete");
            return;
        }
        tick_due = g_audio_streaming && g_audio_es1371_chunk_ticks != 0 &&
                   now - g_audio_es1371_chunk_start_tick >= g_audio_es1371_chunk_ticks;
        if (g_audio_streaming && g_audio_es1371_irq_pending != 0) {
            periods = g_audio_es1371_irq_pending;
            g_audio_es1371_irq_pending = 0;
        } else if (tick_due) {
            periods = 1;
        }
        if (!g_audio_streaming && g_audio_es1371_irq_pending != 0) {
            g_audio_es1371_irq_pending = 0;
            es1371_stop(&g_audio_device);
            g_audio_started = false;
            log_write("audio: playback complete");
            return;
        }
        if (periods != 0) {
            uint32_t catchup = 0;
            uint32_t hw_frame = es1371_playback_position_frames(&g_audio_device, g_audio_es1371_ring_frames);
            uint32_t hw_period = g_audio_es1371_period_frames == 0 ? 0 : hw_frame / g_audio_es1371_period_frames;
            uint32_t target_period = (hw_period + ES1371_STREAM_PERIODS - 1u) & (ES1371_STREAM_PERIODS - 1u);

            if (hw_period != g_audio_es1371_last_hw_period || (g_audio_es1371_period_events & 0x0Fu) == 0) {
                audio_log_es1371_runtime("audio: es1371 upd ", target_period, hw_frame, periods);
                g_audio_es1371_last_hw_period = hw_period;
            }

            while (g_audio_es1371_last_refill_period != target_period && catchup < ES1371_STREAM_PERIODS) {
                bool has_audio = false;
                uint32_t refill_period = (g_audio_es1371_last_refill_period + 1u) & (ES1371_STREAM_PERIODS - 1u);

                if (tick_due && es1371_pcm_interrupt_pending(&g_audio_device)) {
                    es1371_clear_pcm_interrupt(&g_audio_device);
                }
                if (!audio_fill_es1371_stream_period(refill_period, &has_audio)) {
                    return;
                }
                if (!has_audio) {
                    g_audio_es1371_draining = true;
                    audio_log_es1371_runtime("audio: es1371 drain ", refill_period,
                                              es1371_playback_position_frames(&g_audio_device, g_audio_es1371_ring_frames),
                                              periods);
                }
                if (g_audio_es1371_draining && g_audio_es1371_stop_tick == 0 &&
                    refill_period == target_period) {
                    g_audio_es1371_stop_tick = now + g_audio_es1371_chunk_ticks * (ES1371_STREAM_PERIODS - 1u);
                    audio_log_es1371_runtime("audio: es1371 stopwait ", refill_period,
                                              es1371_playback_position_frames(&g_audio_device, g_audio_es1371_ring_frames),
                                              periods);
                }
                es1371_rearm_pcm_out(&g_audio_device);
                g_audio_es1371_last_refill_period = refill_period;
                g_audio_es1371_next_period = (refill_period + 1u) & (ES1371_STREAM_PERIODS - 1u);
                g_audio_es1371_period_events++;
                if (periods != 0) {
                    periods--;
                }
                if (tick_due && now - g_audio_es1371_chunk_start_tick >= g_audio_es1371_chunk_ticks) {
                    g_audio_es1371_chunk_start_tick += g_audio_es1371_chunk_ticks;
                } else {
                    g_audio_es1371_chunk_start_tick = now;
                }
                catchup++;
            }
            if (tick_due && catchup == ES1371_STREAM_PERIODS && now - g_audio_es1371_chunk_start_tick >= g_audio_es1371_chunk_ticks) {
                g_audio_es1371_chunk_start_tick = now;
            }
        }
        return;
    }

    g_audio_device.ac97_status = audio_read16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR));
    if ((g_audio_device.ac97_status & AC97_X_SR_FIFOE) != 0) {
        log_write("audio: ac97 fifo overrun");
        if (g_audio_streaming && g_audio_stream_pos < g_audio_data_size) {
            if (g_audio_stream_recovery_count >= 3U || !audio_restart_ac97_stream_from_current()) {
                g_audio_started = false;
                g_audio_streaming = false;
                log_write("audio: fifo recovery failed");
            } else {
                log_write("audio: fifo recovered");
                audio_log_stream_state("audio: recover ");
            }
            return;
        }
        audio_ac97_reset_pcm_out();
    }
    if (g_audio_streaming) {
        uint32_t civ = audio_read8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_CIV)) & (AC97_BDL_COUNT - 1u);
        uint32_t guard = 0;
        uint32_t prev_civ = g_audio_stream_last_civ;

        if (civ != prev_civ) {
            audio_release_played_stream_descriptors(civ);
        }

        while (!g_audio_stream_eof && ((g_audio_stream_lvi + 1u) & (AC97_BDL_COUNT - 1u)) != civ && guard < AC97_BDL_COUNT) {
            uint32_t next = (g_audio_stream_lvi + 1u) & (AC97_BDL_COUNT - 1u);
            if (!audio_fill_stream_descriptor(next)) {
                break;
            }
            g_audio_stream_lvi = next;
            audio_write8((uint16_t) (g_audio_device.bus_master_base + AC97_PO_LVI), (uint8_t) g_audio_stream_lvi);
            guard++;
        }

        if (g_audio_stream_pos >= g_audio_stream_last_log_pos + AC97_STREAM_LOG_BYTES) {
            audio_log_stream_state("audio: tick ");
            g_audio_stream_last_log_pos = g_audio_stream_pos;
        }
        g_audio_stream_last_civ = civ;
        if ((g_audio_device.ac97_status & AC97_X_SR_DCH) != 0) {
            if (g_audio_stream_eof) {
                g_audio_started = false;
                g_audio_streaming = false;
                log_write("audio: playback complete");
            } else if (g_audio_stream_pos < g_audio_data_size) {
                if (g_audio_stream_recovery_count >= 3U || !audio_restart_ac97_stream_from_current()) {
                    g_audio_started = false;
                    g_audio_streaming = false;
                    log_write("audio: stream underrun unrecoverable");
                    return;
                }
                log_write("audio: stream underrun recovered");
                audio_log_stream_state("audio: recover ");
            }
        }
        return;
    }

    if ((g_audio_device.ac97_status & AC97_X_SR_DCH) != 0) {
        g_audio_started = false;
        log_write("audio: playback complete");
    }
}

/* ============================================================
 *  Microphone recording (ES1371 ADC capture)
 *  - Independent DMA ring fed by the ADC (P1) channel.
 *  - audio_record_pump() drains new ADC frames into a software FIFO;
 *    the user app drains the FIFO via the READ request.
 *  - Format: 16-bit signed, stereo, 44100 Hz, interleaved.
 * ============================================================ */
static void audio_rec_fifo_write(const uint8_t *src, uint32_t bytes)
{
    uint32_t used;
    uint32_t space;
    uint32_t i;

    if (g_rec_fifo == NULL || bytes == 0U) {
        return;
    }
    used = g_rec_fifo_tail - g_rec_fifo_head;
    space = AUDIO_REC_FIFO_BYTES - used;
    if (bytes > space) {
        uint32_t drop = bytes - space;
        g_rec_fifo_head += drop;
    }
    for (i = 0; i < bytes; i++) {
        g_rec_fifo[(g_rec_fifo_tail + i) % AUDIO_REC_FIFO_BYTES] = src[i];
    }
    g_rec_fifo_tail += bytes;
}

static uint32_t audio_rec_fifo_read(uint8_t *dst, uint32_t maxbytes)
{
    uint32_t used;
    uint32_t n;
    uint32_t i;

    if (g_rec_fifo == NULL || dst == NULL) {
        return 0;
    }
    used = g_rec_fifo_tail - g_rec_fifo_head;
    n = used < maxbytes ? used : maxbytes;
    for (i = 0; i < n; i++) {
        dst[i] = g_rec_fifo[(g_rec_fifo_head + i) % AUDIO_REC_FIFO_BYTES];
    }
    g_rec_fifo_head += n;
    return n;
}

bool audio_record_active(void)
{
    return g_rec_active;
}

void audio_record_pump(void)
{
    uint32_t hw_frame;
    uint32_t drained;

    if (!g_rec_active || !g_rec_hw_ready || g_rec_dma == NULL ||
        g_audio_device.kind != AUDIO_DEVICE_ES1371) {
        return;
    }
    hw_frame = es1371_adc_position_frames(&g_audio_device, g_rec_ring_frames);
    if (hw_frame == g_rec_last_hw_frame) {
        return;
    }
    drained = (hw_frame >= g_rec_last_hw_frame)
            ? (hw_frame - g_rec_last_hw_frame)
            : (g_rec_ring_frames - g_rec_last_hw_frame + hw_frame);
    if (drained == 0U || drained > g_rec_ring_frames) {
        g_rec_last_hw_frame = hw_frame;
        return;
    }
    {
        uint32_t off = g_rec_last_hw_frame;
        uint32_t remaining = drained;
        uint32_t peak = 0;

        while (remaining != 0U) {
            uint32_t chunk = remaining;
            uint32_t bytes;
            uint32_t i;
            const uint8_t *src;

            if (off + chunk > g_rec_ring_frames) {
                chunk = g_rec_ring_frames - off;
            }
            bytes = chunk * 4U;
            src = (const uint8_t *) g_rec_dma + off * 4U;
            for (i = 0; i < bytes; i += 2U) {
                int16_t v = (int16_t) ((uint16_t) src[i] | ((uint16_t) src[i + 1U] << 8));
                uint32_t av = (uint32_t) (v < 0 ? -v : v);
                if (av > peak) {
                    peak = av;
                }
            }
            audio_rec_fifo_write(src, bytes);
            off += chunk;
            if (off >= g_rec_ring_frames) {
                off = 0U;
            }
            remaining -= chunk;
        }
        g_rec_level = (uint8_t) (peak > 32767U ? 100U : (peak * 100U / 32768U));
    }
    g_rec_last_hw_frame = hw_frame;
    g_rec_total_frames += drained;
    es1371_adc_rearm(&g_audio_device);
}

int32_t audio_record_ctl(audio_record_request_t *request)
{
    if (request == NULL) {
        return -1;
    }
    switch (request->cmd) {
    case AUDIO_REC_CMD_START: {
        if (g_audio_device.kind != AUDIO_DEVICE_ES1371) {
            return -1;
        }
        if (!g_rec_hw_ready) {
            if (!dma_alloc(AUDIO_REC_DMA_BYTES, 4096, 0xFFFFFFFFu, &g_rec_dma_buffer)) {
                log_write("audio: rec dma alloc failed");
                return -1;
            }
            g_rec_dma = (int16_t *) g_rec_dma_buffer.virtual_address;
            g_rec_fifo = (uint8_t *) kmalloc(AUDIO_REC_FIFO_BYTES);
            if (g_rec_fifo == NULL) {
                log_write("audio: rec fifo alloc failed");
                return -1;
            }
            g_rec_hw_ready = true;
        }
        if (g_rec_active) {
            return 0;
        }
        {
            uint32_t frames = AUDIO_REC_DMA_BYTES / 4U;
            uint32_t period = frames / 8U;

            memset(g_rec_dma, 0, AUDIO_REC_DMA_BYTES);
            es1371_adc_set_rate(&g_audio_device, ES1371_RATE_44100);
            if (!es1371_adc_prepare(&g_audio_device, &g_rec_dma_buffer, frames, period)) {
                log_write("audio: rec prepare failed");
                return -1;
            }
            g_rec_ring_frames = frames;
            g_rec_period_frames = period;
            g_rec_last_hw_frame = 0U;
            g_rec_total_frames = 0U;
            g_rec_level = 0U;
            g_rec_fifo_head = 0U;
            g_rec_fifo_tail = 0U;
            g_rec_active = true;
            log_write("audio: recording started");
        }
        return 0;
    }
    case AUDIO_REC_CMD_STOP: {
        if (g_rec_active) {
            es1371_adc_stop(&g_audio_device);
            g_rec_active = false;
            log_write("audio: recording stopped");
        }
        return 0;
    }
    case AUDIO_REC_CMD_READ: {
        if (!g_rec_active || request->buffer == NULL) {
            return -1;
        }
        request->bytes_copied = audio_rec_fifo_read((uint8_t *) request->buffer, request->capacity);
        return 0;
    }
    case AUDIO_REC_CMD_STATUS: {
        request->frames_total = g_rec_total_frames;
        request->bytes_ready = g_rec_fifo_tail - g_rec_fifo_head;
        request->level = g_rec_level;
        request->channels = 2U;
        request->bits = 16U;
        request->sample_rate = ES1371_RATE_44100;
        return 0;
    }
    default:
        return -1;
    }
}

void audio_shutdown(void)
{
    if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_ES1371 && g_audio_hw_initialized) {
        es1371_stop(&g_audio_device);
    } else if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_AC97 && g_audio_hw_initialized) {
        audio_ac97_stop_pcm_out();
        audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
    }
    audio_release_pcm_source();
    if (g_audio_es1371_src_buffer != NULL) {
        kfree(g_audio_es1371_src_buffer);
        g_audio_es1371_src_buffer = NULL;
    }
    g_track.data_size = 0;
    g_audio_started = false;
    g_audio_paused = false;
    g_audio_streaming = false;
    g_audio_stream_pos = 0;
    g_audio_data_offset = 0;
    g_audio_data_size = 0;
    g_audio_es1371_src_buffer_size = 0;
    g_audio_es1371_chunk_start_tick = 0;
    g_audio_es1371_chunk_ticks = 0;
    g_audio_es1371_stop_tick = 0;
    g_audio_es1371_period_frames = 0;
    g_audio_es1371_next_period = 0;
    g_audio_es1371_cache_start = 0;
    g_audio_es1371_cache_size = 0;
    g_audio_es1371_underruns = 0;
    g_audio_es1371_draining = false;
    g_audio_es1371_ring_frames = 0;
    g_audio_es1371_period_events = 0;
    g_audio_es1371_last_hw_period = 0;
    g_audio_es1371_last_refill_period = 0;
    g_audio_stream_path[0] = '\0';
    g_audio_track_name[0] = '\0';
    log_write("audio: shutdown");
}

/* ====================================================================== */
/* Feature 11: audio mixer                                                */
/* ====================================================================== */

static uint8_t g_app_volumes[AUDIO_MIXER_MAX_APPS];
static int32_t g_app_volume_pids[AUDIO_MIXER_MAX_APPS];
static bool g_audio_muted;
static uint8_t g_audio_pre_mute_volume;
static audio_device_kind_t g_output_device;

static int32_t audio_app_volume_slot(int32_t pid)
{
    for (uint32_t i = 0; i < AUDIO_MIXER_MAX_APPS; i++) {
        if (g_app_volume_pids[i] == pid) {
            return (int32_t) i;
        }
    }
    for (uint32_t i = 0; i < AUDIO_MIXER_MAX_APPS; i++) {
        if (g_app_volume_pids[i] == 0) {
            g_app_volume_pids[i] = pid;
            g_app_volumes[i] = 100u;
            return (int32_t) i;
        }
    }
    return -1;
}

uint8_t audio_app_volume(int32_t pid)
{
    int32_t slot = audio_app_volume_slot(pid);

    return slot < 0 ? 100u : g_app_volumes[slot];
}

void audio_set_app_volume(int32_t pid, uint8_t percent)
{
    int32_t slot;

    if (percent > 100u) {
        percent = 100u;
    }
    slot = audio_app_volume_slot(pid);
    if (slot >= 0) {
        g_app_volumes[slot] = percent;
    }
}

bool audio_muted(void)
{
    return g_audio_muted;
}

void audio_set_muted(bool muted)
{
    if (muted == g_audio_muted) {
        return;
    }
    g_audio_muted = muted;
    if (muted) {
        g_audio_pre_mute_volume = g_audio_volume;
        audio_set_volume(0u);
    } else {
        audio_set_volume(g_audio_pre_mute_volume == 0u ? 50u : g_audio_pre_mute_volume);
    }
}

audio_device_kind_t audio_output_device(void)
{
    return g_output_device;
}

void audio_set_output_device(audio_device_kind_t kind)
{
    g_output_device = kind;
}

void audio_mixer_devices(audio_device_list_t *list)
{
    if (list == NULL) {
        return;
    }
    memset(list, 0, sizeof(*list));
    list->count = 1;
    list->kinds[0] = g_audio_device.kind;
    switch (g_audio_device.kind) {
    case AUDIO_DEVICE_AC97:
        strcpy(list->names[0], "AC97");
        break;
    case AUDIO_DEVICE_HDA:
        strcpy(list->names[0], "HDA");
        break;
    case AUDIO_DEVICE_ES1371:
        strcpy(list->names[0], "ES1371");
        break;
    case AUDIO_DEVICE_SB16:
        strcpy(list->names[0], "SB16");
        break;
    default:
        strcpy(list->names[0], "none");
        break;
    }
}

int32_t audio_mixer_ctl(const audio_mixer_request_t *request)
{
    if (request == NULL) {
        return -1;
    }
    switch (request->cmd) {
    case AUDIO_MIXER_GET_MASTER:
        return (int32_t) audio_volume();
    case AUDIO_MIXER_SET_MASTER:
        audio_set_volume((uint8_t) request->value);
        g_audio_muted = false;
        return 0;
    case AUDIO_MIXER_GET_MUTE:
        return g_audio_muted ? 1 : 0;
    case AUDIO_MIXER_SET_MUTE:
        audio_set_muted(request->value != 0u);
        return 0;
    case AUDIO_MIXER_SET_APP_VOLUME:
        audio_set_app_volume(request->pid, (uint8_t) request->value);
        return 0;
    case AUDIO_MIXER_GET_APP_VOLUME:
        return (int32_t) audio_app_volume(request->pid);
    case AUDIO_MIXER_SET_OUTPUT_DEVICE:
        audio_set_output_device((audio_device_kind_t) request->value);
        return 0;
    case AUDIO_MIXER_GET_OUTPUT_DEVICE:
        return (int32_t) audio_output_device();
    case AUDIO_MIXER_ENUM_DEVICES: {
        audio_device_list_t list;
        audio_mixer_devices(&list);
        return (int32_t) list.count;
    }
    default:
        return -1;
    }
}

/* ====================================================================== */
/*  Standard driver interface: probe / play / record / read / write /      */
/*  info / status. Thin wrappers over the existing AC97/ES1371/HDA paths.  */
/*  When no PCI audio device is present the driver degrades gracefully    */
/*  and prints "audio: not found" instead of faulting.                    */
/* ====================================================================== */

static char g_audio_driver_status[64];

bool audio_probe(void)
{
    if (g_audio_device.present) {
        return true;
    }
    pci_enumerate(audio_detect_callback, &g_audio_device);
    if (!g_audio_device.present) {
        strcpy(g_audio_driver_status, "audio: not found");
        log_write(g_audio_driver_status);
        return false;
    }
    switch (g_audio_device.kind) {
    case AUDIO_DEVICE_AC97:
        strcpy(g_audio_driver_status, "audio: ac97 detected");
        break;
    case AUDIO_DEVICE_ES1371:
        strcpy(g_audio_driver_status, "audio: es1371 detected");
        break;
    case AUDIO_DEVICE_HDA:
        strcpy(g_audio_driver_status, "audio: hda detected");
        break;
    default:
        strcpy(g_audio_driver_status, "audio: pci audio detected");
        break;
    }
    log_write(g_audio_driver_status);
    return true;
}

/* Submit a PCM buffer for playback. Converts non-16-bit / mono input to the
 * 16-bit stereo interleaved format the DMA engines expect. */
bool audio_play(const void *buffer, uint32_t size, uint32_t sample_rate,
                uint16_t bits, uint16_t channels)
{
    const int16_t *src;
    int16_t *conv;
    uint32_t frames;
    uint32_t i;
    bool ok;

    if (!g_audio_device.present || buffer == NULL || size == 0u) {
        return false;
    }
    if (sample_rate == 0u) {
        sample_rate = AC97_TEST_SAMPLE_RATE;
    }

    /* Fast path: already 16-bit stereo. */
    if (bits == 16u && channels == 2u && (size & 3u) == 0u) {
        return audio_play_pcm(buffer, size, sample_rate, 2u, 16u);
    }

    if (channels == 0u || bits == 0u) {
        return false;
    }
    frames = size / ((uint32_t) channels * (bits / 8u));
    if (frames == 0u) {
        return false;
    }
    conv = (int16_t *) kmalloc((uint64_t) frames * 4u);
    if (conv == NULL) {
        return false;
    }
    src = (const int16_t *) buffer;
    for (i = 0; i < frames; i++) {
        int16_t s_l;
        int16_t s_r;

        if (bits == 8u) {
            s_l = (int16_t) ((((const uint8_t *) buffer)[i * channels] - 128) << 8);
            s_r = channels == 2u
                    ? (int16_t) ((((const uint8_t *) buffer)[i * channels + 1u] - 128) << 8)
                    : s_l;
        } else {
            s_l = channels == 2u ? src[i * 2u] : src[i];
            s_r = channels == 2u ? src[i * 2u + 1u] : src[i];
        }
        conv[i * 2u] = s_l;
        conv[i * 2u + 1u] = s_r;
    }
    ok = audio_play_pcm(conv, frames * 4u, sample_rate, 2u, 16u);
    kfree(conv);
    return ok;
}

/* Drain up to max_size bytes of captured PCM (16-bit stereo 44.1k).
 * Starts recording on first call; returns 0 when no capture device. */
uint32_t audio_record(void *buffer, uint32_t max_size)
{
    audio_record_request_t req;
    int32_t rc;

    if (buffer == NULL || max_size == 0u) {
        return 0u;
    }
    memset(&req, 0, sizeof(req));
    req.cmd = AUDIO_REC_CMD_START;
    (void) audio_record_ctl(&req);

    req.cmd = AUDIO_REC_CMD_READ;
    req.buffer = buffer;
    req.capacity = max_size;
    req.bytes_copied = 0;
    rc = audio_record_ctl(&req);
    return rc == 0 ? req.bytes_copied : 0u;
}

/* read = drain recorded samples; write = queue a PCM playback buffer. */
uint32_t audio_read(void *buffer, uint32_t max_size)
{
    return audio_record(buffer, max_size);
}

uint32_t audio_write(const void *buffer, uint32_t size, uint32_t sample_rate,
                     uint16_t bits, uint16_t channels)
{
    if (!audio_play(buffer, size, sample_rate, bits, channels)) {
        return 0u;
    }
    return size;
}

/* Per-channel master volume (left/right 0..100). The AC97 master register
 * is programmed as a single (left<<8|right) attenuation word. */
void audio_set_volume_lr(uint8_t left, uint8_t right)
{
    uint16_t atten;

    if (left > 100u) {
        left = 100u;
    }
    if (right > 100u) {
        right = 100u;
    }
    g_audio_volume = left;
    if (!g_audio_device.present || !g_audio_hw_initialized) {
        return;
    }
    if (g_audio_device.kind == AUDIO_DEVICE_AC97) {
        uint16_t l = (uint16_t) (((100u - left) * 31u) / 100u);
        uint16_t r = (uint16_t) (((100u - right) * 31u) / 100u);

        atten = (uint16_t) ((l << 8) | r);
        audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_MASTER_VOL), atten);
        audio_write16((uint16_t) (g_audio_device.mixer_base + AC97_MIXER_PCM_OUT_VOL), atten);
    } else if (g_audio_device.kind == AUDIO_DEVICE_ES1371) {
        es1371_set_volume(&g_audio_device, left);
    }
}

uint8_t audio_get_volume(void)
{
    return g_audio_volume;
}

const audio_device_info_t *audio_info(void)
{
    return &g_audio_device;
}

const char *audio_status(void)
{
    if (!g_audio_device.present) {
        return "audio: not found";
    }
    return g_audio_driver_status[0] != '\0' ? g_audio_driver_status : "audio: ready";
}
