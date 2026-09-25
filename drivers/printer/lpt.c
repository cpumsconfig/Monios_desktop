/*
 * drivers/printer/lpt.c -- standard parallel port (SPP) printer driver.
 *
 * LPT1 at I/O 0x378. Implements the classic Centronics/SPP handshake and a
 * basic subset of ESC/POS text commands. When no physical port is attached
 * (stock QEMU) output is captured in an in-kernel spool ring so the system
 * still produces observable print data.
 */

#include "lpt.h"
#include "common.h"
#include "app_memory.h"
#include "exec.h"
#include "string.h"

#define LPT_DATA   (LPT1_BASE + 0u)
#define LPT_STAT   (LPT1_BASE + 1u)
#define LPT_CTRL   (LPT1_BASE + 2u)

#define LPT_SPOOL_MAX (64u * 1024u)

static uint8_t  g_spool[LPT_SPOOL_MAX];
static uint32_t g_spool_len;
static bool     g_have_hw;

static void lpt_delay(void)
{
    /* ~1 us-ish port delay via a dummy PC port read */
    (void) inb(0x80U);
    (void) inb(0x80U);
}

void lpt_init(void)
{
    uint8_t s;
    g_spool_len = 0U;

    /* Control: select + no IRQ, strobe low, init inactive (high). */
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT | LPT_CTRL_INIT));
    lpt_delay();

    /* Probe: a real port usually presents 0x?0/0xE0-ish status. Treat a
     * constant 0xFF or 0x00 as "no hardware". */
    s = inb(LPT_STAT);
    g_have_hw = (s != 0xFFU && s != 0x00U);
}

bool lpt_here(void)
{
    return g_have_hw;
}

uint8_t lpt_status(void)
{
    return inb(LPT_STAT);
}

void lpt_reset(void)
{
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT | LPT_CTRL_INIT));
    lpt_delay();
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT | 0u));   /* pull nInit low */
    lpt_delay();
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT | LPT_CTRL_INIT));
    lpt_delay();
    g_spool_len = 0U;
}

static void lpt_spool_append(const uint8_t *data, uint32_t len)
{
    uint32_t space = LPT_SPOOL_MAX - g_spool_len;
    if (len > space) len = space;
    if (len == 0U) return;
    memcpy(g_spool + g_spool_len, data, len);
    g_spool_len += len;
}

int32_t lpt_write_byte(uint8_t data)
{
    uint32_t tries = 100000U;

    /* Wait for not-busy. */
    while (tries-- > 0U) {
        if ((inb(LPT_STAT) & LPT_STATUS_BUSY) != 0U) break;
    }

    outb(LPT_DATA, data);
    lpt_delay();

    /* Strobe pulse. */
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT | LPT_CTRL_STROBE));
    lpt_delay();
    outb(LPT_CTRL, (uint8_t) (LPT_CTRL_SELECT));
    lpt_delay();

    /* Always mirror into the software spool. */
    lpt_spool_append(&data, 1U);
    return 0;
}

int32_t lpt_write(const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *) buf;
    uint32_t i;
    if (buf == NULL || len == 0U) return -1;
    for (i = 0U; i < len; i++) {
        lpt_write_byte(p[i]);
    }
    return 0;
}

int32_t lpt_print_text(const char *text, uint32_t len)
{
    /* ESC/POS: init + print. CR/LF handled by caller/newlines. */
    static const uint8_t esc_init[3] = { 0x1BU, '@', 0x1BU };
    lpt_write(esc_init, 3U);
    return lpt_write(text, len);
}

uint32_t lpt_spool_bytes(void)
{
    return g_spool_len;
}

uint32_t lpt_spool_read(uint8_t *out, uint32_t max)
{
    uint32_t n = g_spool_len;
    if (n > max) n = max;
    if (n > 0U) memcpy(out, g_spool, n);
    return n;
}

int32_t lpt_ctl(uint64_t rbx, uint64_t rcx, uint64_t rdx)
{
    switch (rbx) {
    case LPT_CTL_INIT:
        lpt_init();
        return 0;
    case LPT_CTL_RESET:
        lpt_reset();
        return 0;
    case LPT_CTL_STATUS: {
        uint8_t s = lpt_status();
        uint32_t packed = 0U;
        if ((s & LPT_STATUS_BUSY) == 0U) packed |= 1U;  /* busy */
        if (s & LPT_STATUS_PAPER)       packed |= 2U;  /* paper out */
        if ((s & LPT_STATUS_ERROR) == 0U) packed |= 4U; /* error */
        if (s & LPT_STATUS_SELECT)       packed |= 8U;  /* online */
        if (!g_have_hw) packed |= 0x80000000U;          /* emulated */
        return (int32_t) packed;
    }
    case LPT_CTL_WRITE: {
        /* rcx = user text buffer, rdx = length */
        uint64_t len = rdx;
        uint64_t off = 0U;
        if (len == 0U) return -1;
        while (off < len) {
            uint8_t tmp[256U];
            uint64_t chunk = len - off;
            if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
            if (exec_active()) {
                if (!app_memory_copy_from_user(tmp,
                        (const void *) (uintptr_t) (rcx + off), chunk)) return -1;
            } else {
                memcpy(tmp, (const void *) (uintptr_t) (rcx + off), chunk);
            }
            lpt_write(tmp, (uint32_t) chunk);
            off += chunk;
        }
        return 0;
    }
    case LPT_CTL_SPOOL: {
        /* rcx = user out buffer, rdx = capacity */
        uint8_t tmp[256U];
        uint32_t cap = (uint32_t) rdx;
        uint32_t n = lpt_spool_read(tmp, cap < sizeof(tmp) ? cap : (uint32_t) sizeof(tmp));
        if (exec_active()) {
            if (!app_memory_copy_to_user((void *) (uintptr_t) rcx, tmp, n)) return -1;
        } else {
            memcpy((void *) (uintptr_t) rcx, tmp, n);
        }
        return (int32_t) n;
    }
    default:
        return -1;
    }
}
