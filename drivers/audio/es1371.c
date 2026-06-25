#include "common.h"
#include "dma.h"
#include "es1371.h"
#include "kernel.h"
#include "pci.h"

#define PCI_COMMAND_OFFSET            0x04
#define PCI_COMMAND_IO                0x0001
#define PCI_COMMAND_BUS_MASTER        0x0004
#define PCI_VENDOR_ENSONIQ            0x1274
#define PCI_DEVICE_ES1371             0x1371

#define ES1371_REG_CONTROL            0x00
#define ES1371_REG_STATUS             0x04
#define ES1371_REG_MEM_PAGE           0x0C
#define ES1371_REG_SRCONV             0x10
#define ES1371_REG_CODEC              0x14
#define ES1371_REG_LEGACY             0x18
#define ES1371_REG_SERIAL             0x20
#define ES1371_REG_DAC2_FRAMECNT      0x28
#define ES1371_REG_DAC2_FRAMEADR      0x38
#define ES1371_REG_DAC2_FRAMEDEF      0x3C
#define ES1371_FRAMEDEF_CURR_MASK     0xFFFF0000U
#define ES1371_FRAMEDEF_CURR_SHIFT    14
#define ES1371_MEM_PAGE_DAC           0x0C
#define ES1371_STATUS_RESET           0x00000020
#define ES1371_STATUS_DAC2            0x00000002
#define ES1371_CONTROL_SYNCRES        0x00004000
#define ES1371_CONTROL_DAC2_EN        0x00000020
#define ES1371_DMA_BYTES              (1024U * 1024U)
#define ES1371_SAMPLE_RATE            8000U
#define ES1371_STEREO_CHANNELS        2U
#define ES1371_CODEC_READY            0x80000000U
#define ES1371_CODEC_BUSY             0x40000000U
#define ES1371_CODEC_MASTER_VOL       0x02
#define ES1371_CODEC_PCM_OUT_VOL      0x18
#define ES1371_SRC_WE                 0x01000000U
#define ES1371_SRC_BUSY               0x00800000U
#define ES1371_SRC_DISABLE            0x00400000U
#define ES1371_SRC_DDAC1              0x00200000U
#define ES1371_SRC_DDAC2              0x00100000U
#define ES1371_SRC_DADC               0x00080000U
#define ES1371_SRC_DAC1               0x70U
#define ES1371_SRC_DAC2               0x74U
#define ES1371_SRC_VOL_ADC            0x6CU
#define ES1371_SRC_VOL_DAC1           0x7CU
#define ES1371_SRC_VOL_DAC2           0x7EU
#define ES1371_SRC_TRUNC_N            0x00U
#define ES1371_SRC_INT_REGS           0x01U
#define ES1371_SRC_VFREQ_FRAC         0x03U
#define ES1371_SERIAL_P2_END_INC(x)   (((x) & 0x07U) << 19)
#define ES1371_SERIAL_P2_ST_INC(x)    (((x) & 0x07U) << 16)
#define ES1371_SERIAL_P2_LOOP_SEL     0x00004000U
#define ES1371_SERIAL_P2_PAUSE        0x00001000U
#define ES1371_SERIAL_P2_INT_EN       0x00000200U
#define ES1371_SERIAL_P2_DAC_SEN      0x00000040U
#define ES1371_SERIAL_P2_MODE(x)      (((x) & 0x03U) << 2)
#define ES1371_SERIAL_P2_STEREO_16    0x03U

bool es1371_supported(const pci_device_info_t *info)
{
    return info != NULL && info->vendor_id == PCI_VENDOR_ENSONIQ && info->device_id == PCI_DEVICE_ES1371;
}

uint32_t es1371_sample_rate(void)
{
    return ES1371_SAMPLE_RATE;
}

static bool es1371_io_bar_valid(uint32_t base, uint32_t size)
{
    return base >= 0x100u && base + size <= 0x10000u;
}

static void es1371_write32(const audio_device_info_t *device, uint32_t reg, uint32_t value)
{
    outl((uint16_t) (device->mixer_base + reg), value);
}

static uint32_t es1371_read32(const audio_device_info_t *device, uint32_t reg)
{
    return inl((uint16_t) (device->mixer_base + reg));
}

static void es1371_busy_delay(uint32_t loops)
{
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

static void es1371_enable_pci(const audio_device_info_t *device)
{
    uint16_t command = pci_config_read16(device->bus, device->slot, device->func, PCI_COMMAND_OFFSET);

    command |= PCI_COMMAND_IO | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(device->bus, device->slot, device->func, PCI_COMMAND_OFFSET, command);
}

static void es1371_codec_write(const audio_device_info_t *device, uint8_t reg, uint16_t value)
{
    for (uint32_t i = 0; i < 10000; i++) {
        uint32_t codec = es1371_read32(device, ES1371_REG_CODEC);

        if ((codec & ES1371_CODEC_BUSY) == 0 && (codec & ES1371_CODEC_READY) != 0) {
            break;
        }
        io_wait();
    }
    es1371_write32(device, ES1371_REG_CODEC, ((uint32_t) reg << 16) | value);
}

static uint16_t es1371_volume_register(uint8_t percent)
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

static uint32_t es1371_wait_src_ready(const audio_device_info_t *device)
{
    uint32_t value = 0;

    for (uint32_t i = 0; i < 10000; i++) {
        value = es1371_read32(device, ES1371_REG_SRCONV);
        if ((value & ES1371_SRC_BUSY) == 0) {
            return value;
        }
        io_wait();
    }
    return value;
}

static void es1371_src_write(const audio_device_info_t *device, uint8_t reg, uint16_t value)
{
    uint32_t src = es1371_wait_src_ready(device) & (ES1371_SRC_DISABLE | ES1371_SRC_DDAC1 | ES1371_SRC_DDAC2 | ES1371_SRC_DADC);

    src |= ((uint32_t) reg << 25);
    src |= value;
    es1371_write32(device, ES1371_REG_SRCONV, src | ES1371_SRC_WE);
}

static uint16_t es1371_src_read(const audio_device_info_t *device, uint8_t reg)
{
    uint32_t src = es1371_wait_src_ready(device) & (ES1371_SRC_DISABLE | ES1371_SRC_DDAC1 | ES1371_SRC_DDAC2 | ES1371_SRC_DADC);

    src |= ((uint32_t) reg << 25);
    es1371_write32(device, ES1371_REG_SRCONV, src);
    return (uint16_t) (es1371_wait_src_ready(device) & 0xFFFFu);
}

static void es1371_set_dac2_rate(const audio_device_info_t *device, uint32_t rate)
{
    uint32_t freq;
    uint32_t src;

    if (rate > ES1371_SAMPLE_RATE) {
        rate = ES1371_SAMPLE_RATE;
    }
    if (rate < 4000U) {
        rate = 4000U;
    }
    freq = (rate << 15) / 3000U;
    src = (es1371_wait_src_ready(device) & (ES1371_SRC_DISABLE | ES1371_SRC_DDAC1 | ES1371_SRC_DADC)) | ES1371_SRC_DDAC2;
    es1371_write32(device, ES1371_REG_SRCONV, src);
    es1371_src_write(device,
                     ES1371_SRC_DAC2 + ES1371_SRC_INT_REGS,
                     (uint16_t) ((es1371_src_read(device, ES1371_SRC_DAC2 + ES1371_SRC_INT_REGS) & 0x00FFu) |
                                 ((freq >> 5) & 0xFC00u)));
    es1371_src_write(device, ES1371_SRC_DAC2 + ES1371_SRC_VFREQ_FRAC, (uint16_t) (freq & 0x7FFFu));
    src = es1371_wait_src_ready(device) & (ES1371_SRC_DISABLE | ES1371_SRC_DDAC1 | ES1371_SRC_DADC);
    es1371_write32(device, ES1371_REG_SRCONV, src);
}

void es1371_stop(const audio_device_info_t *device)
{
    uint32_t control;

    if (device == NULL || device->mixer_base == 0) {
        return;
    }
    control = es1371_read32(device, ES1371_REG_CONTROL);
    es1371_write32(device, ES1371_REG_CONTROL, control & ~ES1371_CONTROL_DAC2_EN);
    es1371_write32(device, ES1371_REG_SERIAL, 0);
}

bool es1371_init(audio_device_info_t *device, dma_buffer_t *dma_buffer, int16_t **pcm_buffer)
{
    if (device == NULL || dma_buffer == NULL || pcm_buffer == NULL || device->kind != AUDIO_DEVICE_ES1371) {
        return false;
    }
    if (!es1371_io_bar_valid(device->mixer_base, 0x40U)) {
        log_write("audio: es1371 io bar invalid");
        return false;
    }

    es1371_enable_pci(device);
    es1371_write32(device, ES1371_REG_STATUS, ES1371_STATUS_RESET);
    for (uint32_t i = 0; i < 10000; i++) {
        if ((es1371_read32(device, ES1371_REG_STATUS) & ES1371_STATUS_RESET) == 0) {
            break;
        }
        io_wait();
    }

    if (!dma_alloc(ES1371_DMA_BYTES, 4096, 0xFFFFFFFFu, dma_buffer)) {
        log_write("audio: es1371 dma alloc failed");
        return false;
    }
    *pcm_buffer = (int16_t *) dma_buffer->virtual_address;
    memset(*pcm_buffer, 0, ES1371_DMA_BYTES);
    es1371_write32(device, ES1371_REG_CONTROL, 0);
    es1371_write32(device, ES1371_REG_SERIAL, 0);
    es1371_write32(device, ES1371_REG_LEGACY, 0);
    es1371_write32(device, ES1371_REG_CONTROL, ES1371_CONTROL_SYNCRES);
    (void) es1371_read32(device, ES1371_REG_CONTROL);
    es1371_busy_delay(10000);
    es1371_write32(device, ES1371_REG_CONTROL, 0);
    es1371_write32(device, ES1371_REG_SRCONV, ES1371_SRC_DISABLE);
    for (uint8_t reg = 0; reg < 0x80; reg++) {
        es1371_src_write(device, reg, 0);
    }
    es1371_src_write(device, ES1371_SRC_DAC1 + ES1371_SRC_TRUNC_N, 16u << 4);
    es1371_src_write(device, ES1371_SRC_DAC1 + ES1371_SRC_INT_REGS, 16u << 10);
    es1371_src_write(device, ES1371_SRC_DAC2 + ES1371_SRC_TRUNC_N, 16u << 4);
    es1371_src_write(device, ES1371_SRC_DAC2 + ES1371_SRC_INT_REGS, 16u << 10);
    es1371_src_write(device, ES1371_SRC_VOL_ADC, 1u << 12);
    es1371_src_write(device, ES1371_SRC_VOL_ADC + 1u, 1u << 12);
    es1371_src_write(device, ES1371_SRC_VOL_DAC1, 1u << 12);
    es1371_src_write(device, ES1371_SRC_VOL_DAC1 + 1u, 1u << 12);
    es1371_src_write(device, ES1371_SRC_VOL_DAC2, 1u << 12);
    es1371_src_write(device, ES1371_SRC_VOL_DAC2 + 1u, 1u << 12);
    es1371_set_dac2_rate(device, ES1371_SAMPLE_RATE);
    es1371_write32(device, ES1371_REG_SRCONV, 0);
    es1371_codec_write(device, 0x00, 0x0000);
    es1371_codec_write(device, ES1371_CODEC_MASTER_VOL, 0x0000);
    es1371_codec_write(device, ES1371_CODEC_PCM_OUT_VOL, 0x0000);
    es1371_stop(device);
    log_write("audio: es1371 pcm ready");
    return true;
}

bool es1371_prepare_pcm_out(const audio_device_info_t *device, const dma_buffer_t *dma_buffer, uint32_t frame_count, uint32_t period_frames, bool loop)
{
    uint32_t control;
    uint32_t dma_bytes;
    uint32_t dma_longwords;
    uint32_t interrupt_longwords;
    uint32_t serial;

    if (device == NULL || dma_buffer == NULL || frame_count == 0 || frame_count > ES1371_DMA_BYTES / 4u) {
        return false;
    }
    if (period_frames == 0 || period_frames > frame_count) {
        period_frames = frame_count;
    }
    dma_bytes = frame_count * ES1371_STEREO_CHANNELS * sizeof(int16_t);
    dma_longwords = dma_bytes / 4u;
    if (dma_longwords == 0 || dma_longwords > 0x10000U) {
        return false;
    }
    interrupt_longwords = loop ? period_frames : dma_longwords;
    if (interrupt_longwords == 0) {
        return false;
    }

    es1371_stop(device);
    es1371_write32(device, ES1371_REG_MEM_PAGE, ES1371_MEM_PAGE_DAC);
    es1371_write32(device, ES1371_REG_DAC2_FRAMEADR, (uint32_t) dma_buffer->physical_address);
    es1371_write32(device, ES1371_REG_DAC2_FRAMEDEF, dma_longwords - 1u);
    serial = ES1371_SERIAL_P2_INT_EN |
             ES1371_SERIAL_P2_MODE(ES1371_SERIAL_P2_STEREO_16) |
             ES1371_SERIAL_P2_END_INC(2) |
             ES1371_SERIAL_P2_ST_INC(0);
    if (!loop) {
        serial |= ES1371_SERIAL_P2_LOOP_SEL;
    }
    es1371_write32(device, ES1371_REG_SERIAL, serial);
    es1371_write32(device, ES1371_REG_DAC2_FRAMECNT, interrupt_longwords - 1u);
    control = es1371_read32(device, ES1371_REG_CONTROL);
    es1371_write32(device, ES1371_REG_CONTROL, control | ES1371_CONTROL_DAC2_EN);
    return true;
}

bool es1371_pcm_interrupt_pending(const audio_device_info_t *device)
{
    if (device == NULL || device->mixer_base == 0) {
        return false;
    }
    return (es1371_read32(device, ES1371_REG_STATUS) & ES1371_STATUS_DAC2) != 0;
}

uint32_t es1371_playback_position_frames(const audio_device_info_t *device, uint32_t ring_frames)
{
    uint32_t framedef;
    uint32_t byte_pos;
    uint32_t frame_pos;

    if (device == NULL || device->mixer_base == 0 || ring_frames == 0) {
        return 0;
    }
    es1371_write32(device, ES1371_REG_MEM_PAGE, ES1371_MEM_PAGE_DAC);
    framedef = es1371_read32(device, ES1371_REG_DAC2_FRAMEDEF);
    byte_pos = (framedef & ES1371_FRAMEDEF_CURR_MASK) >> ES1371_FRAMEDEF_CURR_SHIFT;
    frame_pos = byte_pos / (ES1371_STEREO_CHANNELS * sizeof(int16_t));
    if (frame_pos >= ring_frames) {
        frame_pos %= ring_frames;
    }
    return frame_pos;
}

uint32_t es1371_debug_status(const audio_device_info_t *device)
{
    if (device == NULL || device->mixer_base == 0) {
        return 0;
    }
    return es1371_read32(device, ES1371_REG_STATUS);
}

void es1371_clear_pcm_interrupt(const audio_device_info_t *device)
{
    uint32_t serial;

    if (device == NULL || device->mixer_base == 0) {
        return;
    }
    serial = es1371_read32(device, ES1371_REG_SERIAL);
    es1371_write32(device, ES1371_REG_SERIAL, serial & ~ES1371_SERIAL_P2_INT_EN);
    es1371_write32(device, ES1371_REG_SERIAL, serial | ES1371_SERIAL_P2_INT_EN);
}

void es1371_rearm_pcm_out(const audio_device_info_t *device)
{
    uint32_t serial;

    if (device == NULL || device->mixer_base == 0) {
        return;
    }
    serial = es1371_read32(device, ES1371_REG_SERIAL);
    es1371_write32(device, ES1371_REG_SERIAL, serial & ~ES1371_SERIAL_P2_INT_EN);
    es1371_write32(device, ES1371_REG_SERIAL, serial | ES1371_SERIAL_P2_INT_EN);
}

void es1371_set_paused(const audio_device_info_t *device, bool paused)
{
    uint32_t control;

    if (device == NULL || device->mixer_base == 0) {
        return;
    }
    control = es1371_read32(device, ES1371_REG_CONTROL);
    if (paused) {
        es1371_write32(device, ES1371_REG_CONTROL, control & ~ES1371_CONTROL_DAC2_EN);
    } else {
        es1371_write32(device, ES1371_REG_CONTROL, control | ES1371_CONTROL_DAC2_EN);
    }
}

void es1371_set_volume(const audio_device_info_t *device, uint8_t percent)
{
    uint16_t value;

    if (device == NULL || device->mixer_base == 0) {
        return;
    }
    value = es1371_volume_register(percent);
    es1371_codec_write(device, ES1371_CODEC_MASTER_VOL, value);
    es1371_codec_write(device, ES1371_CODEC_PCM_OUT_VOL, value);
}
