#include "aac.h"
#include "audio.h"
#include "common.h"
#include "dma.h"
#include "es1371.h"
#include "file.h"
#include "hda.h"
#include "interrupt.h"
#include "kernel.h"
#include "memory.h"
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
static char g_audio_stream_path[AUDIO_TRACK_PATH_MAX];
static char g_audio_track_name[64];

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
bool audio_play_wav_file(const char *path);

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

static uint32_t audio_read32(uint16_t port)
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

static bool audio_resample_stereo_s16(const int16_t *src, uint32_t src_frames, uint32_t src_rate, uint32_t dst_rate, int16_t **out_pcm, uint32_t *out_frames)
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
        uint64_t src_index64 = ((uint64_t) i * (uint64_t) src_rate) / (uint64_t) dst_rate;
        uint32_t src_index = (uint32_t) src_index64;

        if (src_index >= src_frames) {
            src_index = src_frames - 1u;
        }
        dst[i * 2u] = src[src_index * 2u];
        dst[i * 2u + 1u] = src[src_index * 2u + 1u];
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

static void audio_pc_speaker_tone(uint32_t hz, uint32_t loops)
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

static bool audio_ac97_prepare_pcm_out(uint32_t frame_count, uint32_t sample_rate, const char *track_name)
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
    g_audio_volume = 80;
    g_audio_dma_frames = 0;
    g_audio_total_samples = 0;
    g_audio_track_name[0] = '\0';
    g_audio_es1371_src_buffer = NULL;
    g_audio_es1371_src_buffer_size = 0;
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
        strcpy(line + 7, "hda detected");
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

bool audio_play_file(const char *path)
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

    if (g_audio_es1371_src_buffer == NULL || g_audio_es1371_src_buffer_size == 0 || g_audio_stream_path[0] == '\0') {
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
        if (file_read_at(g_audio_stream_path,
                         g_audio_data_offset + cache_end,
                         g_audio_es1371_src_buffer + g_audio_es1371_cache_size,
                         bytes_to_read) != (int32_t) bytes_to_read) {
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
    if (period_index >= ES1371_STREAM_PERIODS || g_audio_stream_path[0] == '\0' || g_audio_stream_rate == 0 ||
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

    if (g_audio_stream_path[0] == '\0' || g_audio_stream_rate == 0 || max_dst_frames == 0) {
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

bool audio_play_wav_file(const char *path)
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
    if (fmt.format_tag != 1 || fmt.channels != 2 || fmt.bits_per_sample != 16) {
        log_write("audio: wav format unsupported");
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
    stream_size = (uint32_t) wav_file_size - data_offset;
    g_track.channels = (uint8_t) fmt.channels;
    g_track.sample_rate = track_rate;
    g_track.bits_per_sample = fmt.bits_per_sample;
    g_track.data_size = stream_size;
    g_audio_data_offset = data_offset;
    g_audio_data_size = stream_size;
    g_audio_stream_pos = 0;
    g_audio_stream_rate = track_rate;
    g_audio_streaming = true;
    g_audio_stream_last_log_pos = 0;
    g_audio_stream_recovery_count = 0;
    g_audio_dma_frames = AC97_DMA_BYTES / 4u;
    g_audio_total_samples = stream_size / 2u;
    strcpy(g_audio_stream_path, path);
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

static bool audio_fill_stream_descriptor(uint32_t index)
{
    uint32_t remaining;
    uint32_t bytes_to_read;
    uint32_t samples;
    uint8_t *dst;

    if (index >= AC97_BDL_COUNT || g_audio_stream_path[0] == '\0' || g_audio_stream_desc_busy[index]) {
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
    if (file_read_at(g_audio_stream_path, g_audio_data_offset + g_audio_stream_pos, dst, bytes_to_read) != (int32_t) bytes_to_read) {
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

static void audio_fill_tone(int16_t *buffer, uint32_t *frame_cursor, uint32_t frames, uint32_t sample_rate, uint32_t hz)
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

static void audio_fill_silence(int16_t *buffer, uint32_t *frame_cursor, uint32_t frames)
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

void audio_update(void)
{
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

void audio_shutdown(void)
{
    if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_ES1371 && g_audio_hw_initialized) {
        es1371_stop(&g_audio_device);
    } else if (g_audio_device.present && g_audio_device.kind == AUDIO_DEVICE_AC97 && g_audio_hw_initialized) {
        audio_ac97_stop_pcm_out();
        audio_write16((uint16_t) (g_audio_device.bus_master_base + AC97_PO_SR), AC97_X_SR_CLEAR);
    }
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
