/*
 * drivers/storage/sdhci.c -- SDHCI SD/SDXC card driver (Task 4).
 *
 * Probes a PCI SDHCI controller (base class 0x08, subclass 0x05), runs the
 * SD card identification sequence, and exposes 512-byte block I/O through
 * the blockdev layer. Transfers use the PIO data path (simplified); DMA can
 * be layered on later.
 *
 * If no controller / card is present the driver simply reports not-ready and
 * leaves the rest of the storage stack untouched.
 */

#include "sdhci.h"
#include "common.h"
#include "pci.h"
#include "string.h"

/* ---- SDHCI register offsets (standard) ---- */
#define SDHCI_DMA_ADDR      0x00u
#define SDHCI_BLOCK_SIZE    0x04u
#define SDHCI_BLOCK_COUNT   0x06u
#define SDHCI_ARGUMENT      0x08u
#define SDHCI_TRANSFER_MODE 0x0Cu
#define SDHCI_COMMAND       0x0Eu
#define SDHCI_RESPONSE      0x10u
#define SDHCI_BUFFER        0x20u
#define SDHCI_PRESENT_STATE 0x24u
#define SDHCI_HOST_CTRL1    0x28u
#define SDHCI_POWER_CTRL   0x29u
#define SDHCI_CLOCK_CTRL    0x2Cu
#define SDHCI_TIMEOUT_CTRL  0x2Eu
#define SDHCI_SOFTWARE_RESET 0x2Fu
#define SDHCI_INT_STATUS    0x30u
#define SDHCI_INT_ENABLE    0x34u
#define SDHCI_SIGNAL_ENABLE 0x38u

/* Present state bits. */
#define SDHCI_PRESENT_CMD_INHIBIT  0x00000001u
#define SDHCI_PRESENT_DAT_INHIBIT  0x00000002u
#define SDHCI_PRESENT_CARD_INSERT  0x00010000u
#define SDHCI_PRESENT_READ_ACTIVE   0x02000000u
#define SDHCI_PRESENT_WRITE_ACTIVE  0x01000000u

/* Power control. */
#define SDHCI_POWER_ON      0x01u
#define SDHCI_POWER_180V    0x0Au
#define SDHCI_POWER_330V    0x0Fu

/* Clock control. */
#define SDHCI_CLOCK_INTERNAL  0x0001u
#define SDHCI_CLOCK_STABLE    0x0002u
#define SDHCI_CLOCK_ENABLE    0x0004u

/* Software reset. */
#define SDHCI_RESET_ALL     0x01u

/* Command register flags (self-consistent encoding; index in low 6 bits). */
#define SDHCI_CMD_DATA      0x0040u
#define SDHCI_CMD_IDXCHK    0x0080u
#define SDHCI_CMD_CRCCHK    0x0100u
#define SDHCI_CMD_RSP_NONE  0x0000u
#define SDHCI_CMD_RSP_SHORT 0x0200u
#define SDHCI_CMD_RSP_LONG  0x0600u
#define SDHCI_CMD_RSP_BUSY  0x0400u

/* Transfer mode. */
#define SDHCI_TM_ACMD       0x02u
#define SDHCI_TM_BLKCNT     0x020u
#define SDHCI_TM_READ       0x10u
#define SDHCI_TM_MULTI      0x20u

/* SD commands. */
#define SD_CMD0_GO_IDLE      0u
#define SD_CMD2_ALL_SEND_CID 2u
#define SD_CMD3_REL_ADDR     3u
#define SD_CMD6_SWITCH_FUNC  6u
#define SD_CMD7_SELECT_CARD  7u
#define SD_CMD8_SEND_IF_COND 8u
#define SD_CMD9_SEND_CSD     9u
#define SD_CMD12_STOP        12u
#define SD_CMD16_SET_BLOCKLEN 16u
#define SD_CMD17_READ_SINGLE 17u
#define SD_CMD18_READ_MULTI  18u
#define SD_CMD24_WRITE_BLOCK 24u
#define SD_CMD25_WRITE_MULTI 25u
#define SD_CMD55_APP_CMD     55u
#define SD_ACMD41_SEND_OP_COND 41u
#define SD_ACMD6_SET_BUS_WIDTH 6u

#define SD_OCR_HCS          0x40000000u

typedef struct {
    volatile uint8_t *base;
    bool             present;
    bool             card_ready;
    uint16_t         rca;
    bool             sdhc;
    uint64_t         capacity_sectors;
    uint32_t         clock_hz;
} sdhci_hw_t;

static sdhci_hw_t g_hw;
static sdhci_info_t g_info;

static uint8_t  sdhci_read8(uint32_t off)  { return g_hw.base[off]; }
static uint16_t sdhci_read16(uint32_t off) { return *(volatile uint16_t *)(g_hw.base + off); }
static uint32_t sdhci_read32(uint32_t off) { return *(volatile uint32_t *)(g_hw.base + off); }
static void sdhci_write8(uint32_t off, uint8_t v)  { g_hw.base[off] = v; }
static void sdhci_write16(uint32_t off, uint16_t v) { *(volatile uint16_t *)(g_hw.base + off) = v; }
static void sdhci_write32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(g_hw.base + off) = v; }

static void sdhci_delay(uint32_t us)
{
    volatile uint32_t i;
    for (i = 0; i < us * 8U; i++) { }
}

static bool sdhci_wait_inhibit(uint32_t timeout)
{
    while (timeout-- > 0U) {
        if ((sdhci_read32(SDHCI_PRESENT_STATE) &
             (SDHCI_PRESENT_CMD_INHIBIT | SDHCI_PRESENT_DAT_INHIBIT)) == 0U) {
            return true;
        }
        sdhci_delay(1);
    }
    return false;
}

static void sdhci_set_clock_src(uint32_t rate_hz)
{
    uint16_t clk;
    uint16_t divisor;
    /* Base SD clock is typically 50 MHz (PLL). Program divisor. */
    if (rate_hz >= 25000000U) divisor = 2U;       /* 25 MHz */
    else divisor = 125U;                          /* 400 kHz */

    sdhci_write16(SDHCI_CLOCK_CTRL, 0U);
    sdhci_delay(100);
    clk = SDHCI_CLOCK_INTERNAL | (uint16_t) (divisor & 0x3FFu);
    sdhci_write16(SDHCI_CLOCK_CTRL, clk);
    sdhci_delay(100);
    while ((sdhci_read16(SDHCI_CLOCK_CTRL) & SDHCI_CLOCK_STABLE) == 0U) { }
    sdhci_write16(SDHCI_CLOCK_CTRL, (uint16_t)(clk | SDHCI_CLOCK_ENABLE));
    g_hw.clock_hz = rate_hz;
}

static uint32_t sdhci_send_cmd(uint32_t cmd, uint32_t arg, uint16_t flags)
{
    uint32_t rsp;
    if (!sdhci_wait_inhibit(100000U)) return 0xFFFFFFFFu;

    sdhci_write32(SDHCI_ARGUMENT, arg);
    sdhci_write16(SDHCI_COMMAND, (uint16_t)((cmd & 0x3Fu) | flags));

    if (!sdhci_wait_inhibit(100000U)) return 0xFFFFFFFFu;

    if ((flags & SDHCI_CMD_RSP_NONE) == 0U) {
        rsp = sdhci_read32(SDHCI_RESPONSE);
    } else {
        rsp = 0;
    }
    return rsp;
}

static uint32_t sdhci_send_acmd(uint32_t acmd, uint32_t arg, uint16_t flags)
{
    sdhci_send_cmd(SD_CMD55_APP_CMD, (uint32_t) g_hw.rca << 16,
                   SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK);
    return sdhci_send_cmd(acmd, arg, flags);
}

static void sdhci_parse_csd(const uint32_t *csd)
{
    /* csd[0..3] = RESPONSE[0..3] of CMD9 (R2, 128-bit). */
    uint32_t c_size;
    uint32_t mult;
    if ((csd[0] >> 30) == 1U) {
        /* CSD v2.0: SDHC/SDXC, block addressing. */
        c_size = ((csd[1] >> 8) & 0x3FU) | ((csd[2] & 0x3FU) << 16);
        g_hw.sdhc = true;
        g_hw.capacity_sectors = ((uint64_t) (c_size + 1U)) * 1024ULL;
    } else {
        g_hw.sdhc = false;
        mult = ((csd[2] >> 15) & 0x07u);
        c_size = ((csd[1] >> 10) & 0xFFFu) | ((csd[2] & 0x3Fu) << 2);
        g_hw.capacity_sectors = ((uint64_t)(c_size + 1U) << (mult + 2U)) / 512ULL;
    }
}

static bool sdhci_init_card(void)
{
    uint32_t ocr;
    uint32_t if_cond;
    uint32_t cid[4];
    uint32_t rca;
    uint32_t csd[4];
    uint32_t i;

    /* 1) Idle at 400 kHz. */
    sdhci_set_clock_src(400000U);
    sdhci_send_cmd(SD_CMD0_GO_IDLE, 0U, SDHCI_CMD_RSP_NONE);
    sdhci_delay(1000);

    /* 2) CMD8 interface condition (voltage + check SDC v2+). */
    if_cond = sdhci_send_cmd(SD_CMD8_SEND_IF_COND, 0x000001AAu,
                             SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK);
    (void) if_cond;

    /* 3) ACMD41 with HCS until card leaves idle. */
    for (i = 0; i < 100U; i++) {
        ocr = sdhci_send_acmd(SD_ACMD41_SEND_OP_COND, SD_OCR_HCS,
                              SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK);
        if (ocr & 0x80000000u) break;
        sdhci_delay(1000);
    }
    g_hw.sdhc = (ocr & SD_OCR_HCS) != 0U;

    /* 4) CMD2 CID. */
    cid[0] = sdhci_send_cmd(SD_CMD2_ALL_SEND_CID, 0U,
                            SDHCI_CMD_RSP_LONG | SDHCI_CMD_CRCCHK);
    cid[1] = sdhci_read32(SDHCI_RESPONSE + 4);
    cid[2] = sdhci_read32(SDHCI_RESPONSE + 8);
    cid[3] = sdhci_read32(SDHCI_RESPONSE + 12);
    (void) cid;

    /* 5) CMD3 relative address. */
    rca = sdhci_send_cmd(SD_CMD3_REL_ADDR, 0U,
                         SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK);
    g_hw.rca = (uint16_t) ((rca >> 16) & 0xFFFFu);

    /* 6) CMD7 select card. */
    sdhci_send_cmd(SD_CMD7_SELECT_CARD, (uint32_t) g_hw.rca << 16,
                   SDHCI_CMD_RSP_BUSY | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK);

    /* 7) CMD9 CSD. */
    csd[0] = sdhci_send_cmd(SD_CMD9_SEND_CSD, (uint32_t) g_hw.rca << 16,
                            SDHCI_CMD_RSP_LONG | SDHCI_CMD_CRCCHK);
    csd[1] = sdhci_read32(SDHCI_RESPONSE + 4);
    csd[2] = sdhci_read32(SDHCI_RESPONSE + 8);
    csd[3] = sdhci_read32(SDHCI_RESPONSE + 12);
    sdhci_parse_csd(csd);

    /* 8) Switch to 25 MHz. */
    sdhci_set_clock_src(25000000U);

    /* 9) ACMD6 4-bit bus. */
    sdhci_send_acmd(SD_ACMD6_SET_BUS_WIDTH, 2U,
                    SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK);
    sdhci_write8(SDHCI_HOST_CTRL1, sdhci_read8(SDHCI_HOST_CTRL1) | 2u);

    /* 10) Block length 512. */
    sdhci_send_cmd(SD_CMD16_SET_BLOCKLEN, 512U,
                   SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK);

    g_hw.card_ready = true;
    return true;
}

static bool sdhci_read_block(uint64_t lba, uint8_t *buf)
{
    uint32_t arg = g_hw.sdhc ? (uint32_t) lba : (uint32_t)(lba * 512U);
    uint32_t i;

    sdhci_write16(SDHCI_BLOCK_SIZE, 512U);
    sdhci_write16(SDHCI_BLOCK_COUNT, 1U);
    sdhci_write16(SDHCI_TRANSFER_MODE, 0U);

    sdhci_send_cmd(SD_CMD17_READ_SINGLE, arg,
                   SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK |
                   SDHCI_CMD_DATA | SDHCI_TM_READ);

    /* PIO: drain 512 bytes from buffer port. */
    for (i = 0U; i < 128U; i++) {
        uint32_t w = sdhci_read32(SDHCI_BUFFER);
        buf[i * 4U + 0U] = (uint8_t) w;
        buf[i * 4U + 1U] = (uint8_t)(w >> 8);
        buf[i * 4U + 2U] = (uint8_t)(w >> 16);
        buf[i * 4U + 3U] = (uint8_t)(w >> 24);
    }
    return true;
}

bool sdhci_read_sector(uint64_t lba, void *buffer)
{
    if (!g_hw.card_ready) return false;
    return sdhci_read_block(lba, (uint8_t *) buffer);
}

bool sdhci_read_sectors(uint64_t lba, uint32_t count, void *buffer)
{
    uint8_t *p = (uint8_t *) buffer;
    uint32_t i;
    if (!g_hw.card_ready) return false;
    for (i = 0U; i < count; i++) {
        if (!sdhci_read_block(lba + i, p + (uint64_t) i * 512U)) return false;
    }
    return true;
}

static bool sdhci_write_block(uint64_t lba, const uint8_t *buf)
{
    uint32_t arg = g_hw.sdhc ? (uint32_t) lba : (uint32_t)(lba * 512U);
    uint32_t i;
    sdhci_write16(SDHCI_BLOCK_SIZE, 512U);
    sdhci_write16(SDHCI_BLOCK_COUNT, 1U);
    sdhci_write16(SDHCI_TRANSFER_MODE, 0U);

    sdhci_send_cmd(SD_CMD24_WRITE_BLOCK, arg,
                   SDHCI_CMD_RSP_SHORT | SDHCI_CMD_CRCCHK | SDHCI_CMD_IDXCHK |
                   SDHCI_CMD_DATA);

    for (i = 0U; i < 128U; i++) {
        uint32_t w = (uint32_t) buf[i * 4U] |
                     ((uint32_t) buf[i * 4U + 1U] << 8) |
                     ((uint32_t) buf[i * 4U + 2U] << 16) |
                     ((uint32_t) buf[i * 4U + 3U] << 24);
        sdhci_write32(SDHCI_BUFFER, w);
    }
    return true;
}

bool sdhci_write_sector(uint64_t lba, const void *buffer)
{
    if (!g_hw.card_ready) return false;
    return sdhci_write_block(lba, (const uint8_t *) buffer);
}

bool sdhci_write_sectors(uint64_t lba, uint32_t count, const void *buffer)
{
    const uint8_t *p = (const uint8_t *) buffer;
    uint32_t i;
    if (!g_hw.card_ready) return false;
    for (i = 0U; i < count; i++) {
        if (!sdhci_write_block(lba + i, p + (uint64_t) i * 512U)) return false;
    }
    return true;
}

bool sdhci_detect_hotplug(void)
{
    if (!g_hw.present) return false;
    return (sdhci_read32(SDHCI_PRESENT_STATE) & SDHCI_PRESENT_CARD_INSERT) != 0U;
}

bool sdhci_init(void)
{
    pci_device_info_t info;

    g_hw.present = false;
    g_hw.card_ready = false;
    g_hw.base = NULL;
    g_hw.capacity_sectors = 0U;

    /* Base class 0x08 (Base System Peripheral), subclass 0x05 (SDHCI). */
    if (!pci_find_first(0x08U, 0x05U, &info)) {
        return false;
    }
    g_hw.base = (volatile uint8_t *) (uintptr_t) info.bar_address[0];
    if (g_hw.base == NULL) return false;
    g_hw.present = true;

    /* Full software reset. */
    sdhci_write8(SDHCI_SOFTWARE_RESET, SDHCI_RESET_ALL);
    sdhci_delay(10000);

    /* Power up the slot. */
    sdhci_write8(SDHCI_POWER_CTRL, SDHCI_POWER_330V);
    sdhci_delay(10000);

    /* Enable interrupts (masked, polling used). */
    sdhci_write16(SDHCI_INT_ENABLE, 0xFFFFu);
    sdhci_write16(SDHCI_SIGNAL_ENABLE, 0u);

    if (!sdhci_detect_hotplug()) {
        /* No card inserted yet; leave driver ready for hotplug. */
        goto out;
    }

    if (!sdhci_init_card()) {
        g_hw.card_ready = false;
    }

out:
    g_info.present = g_hw.present;
    g_info.card_present = g_hw.card_ready;
    g_info.block_size = 512U;
    g_info.capacity_sectors = g_hw.capacity_sectors;
    g_info.rca = g_hw.rca;
    g_info.sdhc = g_hw.sdhc;
    return g_hw.present;
}

bool sdhci_ready(void)
{
    return g_hw.present && g_hw.card_ready;
}

const sdhci_info_t *sdhci_info(void)
{
    return &g_info;
}
