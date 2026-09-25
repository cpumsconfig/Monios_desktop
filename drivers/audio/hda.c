#include "common.h"
#include "hda.h"
#include "kernel.h"
#include "pci.h"

#define PCI_CLASS_MULTIMEDIA     0x04
#define PCI_SUBCLASS_HDA         0x03
#define PCI_COMMAND_OFFSET       0x04
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUS_MASTER   0x0004

/* --- HDA MMIO register map (Intel ICH / High Definition Audio) --------- */
#define HDA_REG_GCAP             0x00u   /* 16b global capabilities          */
#define HDA_REG_GCTL             0x08u   /* 32b global control               */
#define HDA_REG_WAKEEN           0x0Cu   /* 16b wake enable                   */
#define HDA_REG_STATESTS         0x0Eu   /* 16b codec reset state            */
#define HDA_REG_GSTS             0x10u   /* 16b global status                */
#define HDA_REG_OUTPAY           0x24u   /* 16b output payload size          */
#define HDA_REG_INPAY            0x26u   /* 16b input payload size            */
#define HDA_REG_CORBLBASE        0x34u   /* 32b CORB base low                */
#define HDA_REG_CORBUBASE        0x38u   /* 32b CORB base high               */
#define HDA_REG_CORB_RP          0x40u   /* 8b  CORB read pointer            */
#define HDA_REG_CORB_WP          0x41u   /* 8b  CORB write pointer           */
#define HDA_REG_CORB_CTL         0x42u   /* 8b  CORB control (size/reset)    */
#define HDA_REG_RIRBLBASE        0x44u   /* 32b RIRB base low                */
#define HDA_REG_RIRBUBASE        0x48u   /* 32b RIRB base high               */
#define HDA_REG_RIRB_WP          0x4Cu   /* 16b RIRB write pointer           */
#define HDA_REG_RIRB_RP          0x4Eu   /* 16b RIRB read pointer            */
#define HDA_REG_RIRB_CTL         0x50u   /* 16b RIRB control                 */
#define HDA_REG_PWRCTL           0x61u   /* 8b  power status control         */
#define HDA_REG_IC               0x64u   /* 16b interrupt control            */
#define HDA_REG_IR               0x68u   /* 16b interrupt status             */

#define HDA_GCTL_RESET           0x00000001u
#define HDA_GCTL_UNSOL           0x00000100u

#define HDA_CORB_CTL_RESET       0x01u
#define HDA_RIRB_CTL_RESET       0x0001u
#define HDA_RIRB_CTL_ENABLE      0x0002u
#define HDA_RIRB_CTL_DMA_ENABLE  0x0001u

/* Stream descriptors: output @ 0x80, input @ 0xC0, stride 0x20 */
#define HDA_STREAM_OUT_BASE      0x80u
#define HDA_STREAM_IN_BASE       0xC0u
#define HDA_STREAM_STRIDE        0x20u

/* SDnCTL bits (Intel HDA spec 3.3.35). There is deliberately no per-direction
 * bit here: the stream descriptor is direction-agnostic, the direction is a
 * property of the codec converter it is bound to. Bits 23:20 carry the stream
 * tag (STRM); we keep it at 0 so it keeps matching the codec power-on default. */
#define HDA_SD_CTL_SRST          0x00000001u   /* stream reset                  */
#define HDA_SD_CTL_RUN           0x00000002u   /* stream run                    */
#define HDA_SD_CTL_IOCE          0x00000004u   /* interrupt on completion enable */

/* Stream descriptor register offsets, relative to the descriptor base
 * (output 0x80 + n*0x20, capture 0xC0 + n*0x20). Layout per HDA spec 3.3.35+. */
#define HDA_SD_CTL               0x00u   /* 32b stream descriptor control   */
#define HDA_SD_STS               0x03u   /* 8b  stream descriptor status    */
#define HDA_SD_LPIB              0x04u   /* 32b link position in buffer     */
#define HDA_SD_CBL               0x08u   /* 32b cyclic buffer length        */
#define HDA_SD_LVI               0x0Cu   /* 16b last valid index            */
#define HDA_SD_FMT               0x12u   /* 16b stream format               */
#define HDA_SD_BDPL              0x18u   /* 32b BDL base address, low       */
#define HDA_SD_BDPU              0x1Cu   /* 32b BDL base address, high      */

/* --- HDA verbs -------------------------------------------------------- */
#define HDA_VERB_GET_PARAM       0x000Fu
#define HDA_PARAM_CODEC_VENDOR   0x00u
#define HDA_PARAM_AFCG            0x04u
#define HDA_PARAM_AFCG_COUNT     0x05u
#define HDA_PARAM_WIDGET_CAPS    0x04u
#define HDA_PIN_CAPS             0x09u
#define HDA_CONN_LIST_LEN        0x0Eu
#define HDA_PIN_CTRL             0x0Cu
#define HDA_VERB_SET_STREAM_FMT  0x200u
#define HDA_VERB_SET_BDI         0x600u
#define HDA_VERB_SET_PIN_WIDGET  0x707u
#define HDA_VERB_SET_AMP_GAIN    0x300u
#define HDA_VERB_SET_CONV_SELECT 0x701u

#define HDA_PIN_WIDGET_OUT_ENABLE 0x40u
#define HDA_PIN_WIDGET_IN_ENABLE  0x20u

/* Buffer Descriptor List entry (64-bit address + 32-bit length + 32-bit flags) */
typedef struct {
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t length;
    uint32_t flags;
} __attribute__((packed)) hda_bdle_t;

#define HDA_BDL_IOC              0x01u

#define HDA_CORB_ENTRIES         256u
#define HDA_RIRB_ENTRIES         256u
#define HDA_STREAM_BDL_ENTRIES   16u
#define HDA_STREAM_BYTES          (256u * 1024u)

/* DMA-able static buffers (identity-mapped low memory region). */
static uint32_t g_corb[HDA_CORB_ENTRIES] __attribute__((aligned(1024)));
static uint32_t g_rirb[HDA_RIRB_ENTRIES * 2u] __attribute__((aligned(1024)));
static hda_bdle_t g_play_bdl[HDA_STREAM_BDL_ENTRIES] __attribute__((aligned(1024)));
static hda_bdle_t g_rec_bdl[HDA_STREAM_BDL_ENTRIES] __attribute__((aligned(1024)));
static uint8_t g_play_buffer[HDA_STREAM_BYTES] __attribute__((aligned(4096)));
static uint8_t g_rec_buffer[HDA_STREAM_BYTES] __attribute__((aligned(4096)));

static hda_info_t g_hda_info;
static bool g_hda_codec_ready;
static uint8_t g_hda_codec_addr;
static uint16_t g_hda_out_amp_gain;
static uint32_t g_hda_play_rate;
static bool g_hda_play_active;
static bool g_hda_rec_active;

/* ---------------------------------------------------------------------- */
static uint32_t hda_bar_base(uint32_t bar)
{
    if ((bar & 1u) != 0) {
        return 0;
    }
    return bar & 0xFFFFFFF0u;
}

static bool hda_mmio_identity_mapped(uint32_t base)
{
    return base < 0x40000000U || base >= 0xC0000000U;
}

static void hda_write8(uint32_t offset, uint8_t value)
{
    volatile uint8_t *ptr = (volatile uint8_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    *ptr = value;
}

static void hda_write16(uint32_t offset, uint16_t value)
{
    volatile uint16_t *ptr = (volatile uint16_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    *ptr = value;
}

static void hda_write32(uint32_t offset, uint32_t value)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    *ptr = value;
}

static uint8_t hda_read8(uint32_t offset)
{
    volatile uint8_t *ptr = (volatile uint8_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    return *ptr;
}

static uint16_t hda_read16(uint32_t offset)
{
    volatile uint16_t *ptr = (volatile uint16_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    return *ptr;
}

static uint32_t hda_read32(uint32_t offset)
{
    volatile uint32_t *ptr = (volatile uint32_t *) (uint64_t) (g_hda_info.mmio_base + offset);
    return *ptr;
}

static void hda_busy_delay(uint32_t loops)
{
    for (volatile uint32_t i = 0; i < loops; i++) {
    }
}

/* Build a 32-bit codec verb: [codec:4][node:8][verb:12][payload:8] packed
 * into the 24-bit CORB entry used by the controller. */
static uint32_t hda_make_verb(uint8_t codec, uint8_t node, uint16_t verb, uint8_t payload)
{
    return ((uint32_t) codec << 28) | ((uint32_t) node << 20) |
           ((uint32_t) verb << 8) | (uint32_t) payload;
}

/* Send one verb through the CORB and wait for its RIRB response. */
static bool hda_codec_command(uint8_t codec, uint8_t node, uint16_t verb,
                              uint8_t payload, uint32_t *response)
{
    uint32_t entry;
    uint8_t wp;
    uint16_t rirb_wp;
    uint32_t timeout;

    if (!g_hda_info.mmio_ready) {
        if (response != NULL) {
            *response = 0;
        }
        return false;
    }

    entry = hda_make_verb(codec, node, verb, payload);
    wp = hda_read8(HDA_REG_CORB_WP);
    g_corb[wp] = entry;
    hda_write8(HDA_REG_CORB_WP, (uint8_t) ((wp + 1u) & (HDA_CORB_ENTRIES - 1u)));

    /* Wait for the controller to push the response into the RIRB. */
    for (timeout = 0; timeout < 100000u; timeout++) {
        rirb_wp = hda_read16(HDA_REG_RIRB_WP);
        if (rirb_wp != 0u) {
            break;
        }
        hda_busy_delay(50);
    }
    if (timeout >= 100000u) {
        if (response != NULL) {
            *response = 0;
        }
        return false;
    }

    if (response != NULL) {
        uint16_t idx = (uint16_t) ((rirb_wp - 1u) & (HDA_RIRB_ENTRIES - 1u));
        *response = g_rirb[idx * 2u];
    }
    return true;
}

static uint32_t hda_codec_read_param(uint8_t codec, uint8_t node, uint8_t param)
{
    uint32_t resp = 0;

    hda_codec_command(codec, node, HDA_VERB_GET_PARAM, param, &resp);
    return resp;
}

/* True when `base` is the descriptor window that belongs to `output`.
 * Output descriptors sit at 0x80 + n*0x20, capture descriptors at 0xC0 + n*0x20. */
static bool hda_stream_dir_ok(uint32_t base, bool output)
{
    return base == (output ? HDA_STREAM_OUT_BASE : HDA_STREAM_IN_BASE);
}

/* Program a stream descriptor (output base 0x80, capture base 0xC0).
 * `buffer_bytes` is the size of the cyclic buffer, i.e. the sum of every BDL
 * entry length -- not the size of the descriptor list itself. */
static void hda_stream_setup(uint32_t base, bool output, const hda_bdle_t *bdl,
                             uint32_t buffer_bytes, uint16_t format)
{
    if (bdl == NULL || !hda_stream_dir_ok(base, output)) {
        log_write("hda: stream descriptor/direction mismatch");
        return;
    }

    /* Reset the descriptor, then leave it out of reset but stopped. */
    hda_write32(base + HDA_SD_CTL, HDA_SD_CTL_SRST);
    hda_busy_delay(100);
    hda_write32(base + HDA_SD_CTL, 0u);

    /* Cyclic buffer length, BDL base address and last valid index must all be
     * programmed before RUN is set, otherwise the DMA engine reads a null BDL
     * base and walks off into physical address 0. */
    hda_write32(base + HDA_SD_CBL, buffer_bytes);
    hda_write32(base + HDA_SD_BDPL, (uint32_t) (uintptr_t) bdl);
    hda_write32(base + HDA_SD_BDPU, 0u);
    hda_write16(base + HDA_SD_LVI, (uint16_t) (HDA_STREAM_BDL_ENTRIES - 1u));
    hda_write16(base + HDA_SD_FMT, format);

    /* Verify the two values the DMA engine depends on most. */
    if (hda_read32(base + HDA_SD_CBL) != buffer_bytes ||
        hda_read32(base + HDA_SD_BDPL) != (uint32_t) (uintptr_t) bdl) {
        log_write("hda: stream descriptor read-back mismatch");
    }
}

static void hda_stream_start(uint32_t base, bool output)
{
    uint32_t ctl;

    if (!hda_stream_dir_ok(base, output)) {
        return;
    }
    ctl = hda_read32(base + HDA_SD_CTL);
    ctl &= ~HDA_SD_CTL_SRST;
    ctl |= HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE;
    hda_write32(base + HDA_SD_CTL, ctl);
}

static void hda_stream_stop(uint32_t base)
{
    uint32_t ctl = hda_read32(base + HDA_SD_CTL);

    ctl &= ~(HDA_SD_CTL_RUN | HDA_SD_CTL_IOCE);
    hda_write32(base + HDA_SD_CTL, ctl);

    hda_write32(base + HDA_SD_CTL, HDA_SD_CTL_SRST);
    hda_busy_delay(100);
    hda_write32(base + HDA_SD_CTL, 0u);
}

/* Derive the SDnFMT value from a PCM description (Intel HDA spec 3.3.42):
 *   [3:0]   channels - 1
 *   [6:4]   sample size: 0 = 8, 1 = 16, 2 = 20, 3 = 24, 4 = 32 bits
 *   [10:8]  sample rate: 0 = 48 kHz, 1 = 44.1 kHz, 2 = 48k/2, 3 = 44.1k/2, ...
 *   [14:15] stream type, 00 = PCM
 * 48 kHz / 16-bit / stereo therefore encodes as 0x0011 and 44.1 kHz as 0x0111. */
static uint16_t hda_stream_format(uint32_t sample_rate, uint16_t bits, uint16_t channels)
{
    uint16_t bits_field;
    uint16_t rate_field;

    if (bits == 8u) {
        bits_field = 0u;
    } else if (bits == 20u) {
        bits_field = 2u;
    } else if (bits == 24u) {
        bits_field = 3u;
    } else if (bits == 32u) {
        bits_field = 4u;
    } else {
        bits_field = 1u;   /* 16-bit (default) */
    }

    if (sample_rate / 2u == 44100u) {
        rate_field = 3u;   /* 22.05 kHz */
    } else if (sample_rate == 44100u) {
        rate_field = 1u;   /* 44.1 kHz  */
    } else if (sample_rate / 2u == 48000u) {
        rate_field = 2u;   /* 24 kHz    */
    } else {
        rate_field = 0u;   /* 48 kHz (default) */
    }

    if (channels == 0u) {
        channels = 1u;
    }
    if (channels > 16u) {
        channels = 16u;
    }

    return (uint16_t) ((rate_field << 8) | (bits_field << 4) |
                       (uint16_t) (channels - 1u));
}

static void hda_program_bdl(hda_bdle_t *bdl, const void *buffer, uint32_t bytes)
{
    uint32_t chunk = bytes / HDA_STREAM_BDL_ENTRIES;

    for (uint32_t i = 0; i < HDA_STREAM_BDL_ENTRIES; i++) {
        bdl[i].addr_low = (uint32_t) (uintptr_t) buffer + i * chunk;
        bdl[i].addr_high = 0u;
        bdl[i].length = chunk;
        bdl[i].flags = (i == HDA_STREAM_BDL_ENTRIES - 1u) ? HDA_BDL_IOC : 0u;
    }
}

/* ---------------------------------------------------------------------- */
bool hda_driver_init(void)
{
    pci_device_info_t info;
    uint16_t command;

    memset(&g_hda_info, 0, sizeof(g_hda_info));
    g_hda_codec_ready = false;
    g_hda_codec_addr = 0xFFu;
    g_hda_out_amp_gain = 0;
    g_hda_play_rate = 0;
    g_hda_play_active = false;
    g_hda_rec_active = false;
    strcpy(g_hda_info.status, "hda: not found");

    if (!pci_find_first(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HDA, &info)) {
        log_write(g_hda_info.status);
        return false;
    }

    g_hda_info.present = true;
    g_hda_info.vendor_id = info.vendor_id;
    g_hda_info.device_id = info.device_id;
    g_hda_info.bus = info.bus;
    g_hda_info.slot = info.slot;
    g_hda_info.func = info.func;
    g_hda_info.irq = info.interrupt_line;
    g_hda_info.mmio_base = hda_bar_base(info.bar0);
    if (g_hda_info.mmio_base == 0 ||
        !hda_mmio_identity_mapped(g_hda_info.mmio_base)) {
        strcpy(g_hda_info.status, "hda: mmio bar missing");
        log_write(g_hda_info.status);
        return false;
    }

    command = pci_config_read16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET);
    command |= PCI_COMMAND_MEMORY | PCI_COMMAND_BUS_MASTER;
    pci_config_write16(info.bus, info.slot, info.func, PCI_COMMAND_OFFSET, command);

    g_hda_info.global_cap = hda_read16(HDA_REG_GCAP);
    g_hda_info.mmio_ready = true;

    /* Controller reset: take out of reset, wait for codecs to report up. */
    hda_write32(HDA_REG_GCTL, 0u);
    hda_busy_delay(1000);
    hda_write32(HDA_REG_GCTL, HDA_GCTL_UNSOL);
    hda_busy_delay(1000);

    /* Set up CORB: point at our ring, size it out, enable. */
    memset(g_corb, 0, sizeof(g_corb));
    memset(g_rirb, 0, sizeof(g_rirb));
    hda_write32(HDA_REG_CORBLBASE, (uint32_t) (uintptr_t) g_corb);
    hda_write32(HDA_REG_CORBUBASE, 0u);
    hda_write8(HDA_REG_CORB_CTL, HDA_CORB_CTL_RESET);
    hda_busy_delay(100);
    hda_write8(HDA_REG_CORB_CTL, 0u);   /* 256 entries */
    hda_write8(HDA_REG_CORB_RP, 0u);
    hda_write8(HDA_REG_CORB_WP, 0u);

    /* Set up RIRB. */
    hda_write32(HDA_REG_RIRBLBASE, (uint32_t) (uintptr_t) g_rirb);
    hda_write32(HDA_REG_RIRBUBASE, 0u);
    hda_write16(HDA_REG_RIRB_CTL, HDA_RIRB_CTL_RESET);
    hda_busy_delay(100);
    hda_write16(HDA_REG_RIRB_CTL, (uint16_t) (HDA_RIRB_CTL_DMA_ENABLE | HDA_RIRB_CTL_ENABLE));
    hda_write16(HDA_REG_RIRB_WP, 0u);

    /* Codec discovery: STATESTS bits indicate which codec addresses came up. */
    {
        uint16_t state = hda_read16(HDA_REG_STATESTS);
        bool found = false;

        for (uint8_t addr = 0; addr < 4u; addr++) {
            uint32_t vid;

            if ((state & (1u << addr)) == 0u) {
                continue;
            }
            vid = hda_codec_read_param(addr, 0x00u, HDA_PARAM_CODEC_VENDOR);
            if (vid != 0u && vid != 0xFFFFFFFFu) {
                g_hda_codec_addr = addr;
                g_hda_codec_ready = true;
                found = true;
                break;
            }
        }
        /* Ack the state bits so we don't re-read stale presence. */
        hda_write16(HDA_REG_STATESTS, state);
        if (!found) {
            strcpy(g_hda_info.status, "hda: no codec responded");
            log_write(g_hda_info.status);
            return false;
        }
    }

    /* Walk the Audio Function Group / widgets of the discovered codec.
     * We probe a small set of candidate output converter nodes (0x02..0x08)
     * and pin complexes; configure the first usable output pin. */
    for (uint8_t node = 0x02u; node <= 0x08u; node++) {
        uint32_t widget_caps = hda_codec_read_param(g_hda_codec_addr, node, HDA_PARAM_WIDGET_CAPS);

        if (widget_caps != 0u && widget_caps != 0xFFFFFFFFu) {
            /* Route output converter 0x02 to the pin widget, enable it. */
            hda_codec_command(g_hda_codec_addr, node, HDA_VERB_SET_CONV_SELECT, 0x00u, NULL);
        }
    }
    for (uint8_t pin = 0x14u; pin <= 0x17u; pin++) {
        hda_codec_command(g_hda_codec_addr, pin, HDA_VERB_SET_PIN_WIDGET,
                          (uint8_t) HDA_PIN_WIDGET_OUT_ENABLE, NULL);
    }
    /* Set master output amplifier gain (0 = max attenuation off). */
    g_hda_out_amp_gain = 0x2Fu;
    hda_codec_command(g_hda_codec_addr, 0x02u, HDA_VERB_SET_AMP_GAIN,
                     (uint8_t) g_hda_out_amp_gain, NULL);

    strcpy(g_hda_info.status, "hda: controller mmio ready");
    log_write(g_hda_info.status);
    return true;
}

bool hda_probe(void)
{
    if (g_hda_info.present) {
        return true;
    }
    return hda_driver_init();
}

/* Configure the playback DMA stream and start it. */
bool hda_play(const void *buffer, uint32_t size, uint32_t sample_rate,
              uint16_t bits, uint16_t channels)
{
    uint16_t format;

    if (!g_hda_info.mmio_ready || !g_hda_codec_ready) {
        return false;
    }
    if (buffer == NULL || size == 0u || size > HDA_STREAM_BYTES) {
        return false;
    }
    if (g_hda_play_active) {
        hda_stream_stop(HDA_STREAM_OUT_BASE);
    }

    hda_program_bdl(g_play_bdl, g_play_buffer, HDA_STREAM_BYTES);
    memcpy(g_play_buffer, buffer, size);
    /* The cyclic buffer is always HDA_STREAM_BYTES long: silence the tail so
     * leftover samples from the previous playback are not replayed. */
    if (size < HDA_STREAM_BYTES) {
        memset(g_play_buffer + size, 0, HDA_STREAM_BYTES - size);
    }
    format = hda_stream_format(sample_rate, bits, channels);
    hda_stream_setup(HDA_STREAM_OUT_BASE, true, g_play_bdl,
                     HDA_STREAM_BYTES, format);
    hda_stream_start(HDA_STREAM_OUT_BASE, true);

    g_hda_play_rate = sample_rate;
    g_hda_play_active = true;
    strcpy(g_hda_info.status, "hda: playback streaming");
    return true;
}

/* Configure the capture DMA stream and start recording. */
bool hda_record(void *buffer, uint32_t max_size)
{
    uint16_t format;

    if (!g_hda_info.mmio_ready || !g_hda_codec_ready) {
        return false;
    }
    if (buffer == NULL || max_size == 0u || max_size > HDA_STREAM_BYTES) {
        return false;
    }
    (void) buffer;

    hda_program_bdl(g_rec_bdl, g_rec_buffer, HDA_STREAM_BYTES);
    memset(g_rec_buffer, 0, HDA_STREAM_BYTES);
    format = hda_stream_format(48000u, 16u, 2u);
    hda_stream_setup(HDA_STREAM_IN_BASE, false, g_rec_bdl,
                     HDA_STREAM_BYTES, format);
    hda_stream_start(HDA_STREAM_IN_BASE, false);

    g_hda_rec_active = true;
    strcpy(g_hda_info.status, "hda: capture streaming");
    return true;
}

/* Read captured PCM samples out of the record DMA ring. */
uint32_t hda_read(void *buffer, uint32_t max_size)
{
    uint32_t n;

    if (buffer == NULL || !g_hda_rec_active) {
        return 0u;
    }
    n = max_size;
    if (n > HDA_STREAM_BYTES) {
        n = HDA_STREAM_BYTES;
    }
    memcpy(buffer, g_rec_buffer, n);
    return n;
}

/* Queue a PCM buffer for playback (thin wrapper over hda_play). */
uint32_t hda_write(const void *buffer, uint32_t size, uint32_t sample_rate,
                   uint16_t bits, uint16_t channels)
{
    if (!hda_play(buffer, size, sample_rate, bits, channels)) {
        return 0u;
    }
    return size;
}

void hda_set_volume(uint8_t percent)
{
    uint8_t gain;

    if (!g_hda_info.mmio_ready || !g_hda_codec_ready) {
        return;
    }
    if (percent > 100u) {
        percent = 100u;
    }
    gain = (uint8_t) ((uint16_t) percent * 0x2Fu / 100u);
    g_hda_out_amp_gain = gain;
    hda_codec_command(g_hda_codec_addr, 0x02u, HDA_VERB_SET_AMP_GAIN, gain, NULL);
}

uint8_t hda_get_volume(void)
{
    return (uint8_t) ((uint16_t) g_hda_out_amp_gain * 100u / 0x2Fu);
}

void hda_shutdown(void)
{
    if (g_hda_info.present && g_hda_info.mmio_ready) {
        hda_stream_stop(HDA_STREAM_OUT_BASE);
        hda_stream_stop(HDA_STREAM_IN_BASE);
        hda_write32(HDA_REG_GCTL, 0u);
        g_hda_play_active = false;
        g_hda_rec_active = false;
        strcpy(g_hda_info.status, "hda: shutdown");
        log_write(g_hda_info.status);
    }
}

const hda_info_t *hda_info(void)
{
    return &g_hda_info;
}

const char *hda_status(void)
{
    return g_hda_info.status;
}
