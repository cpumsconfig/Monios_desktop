#include "crash_dump.h"
#include "common.h"
#include "interrupt.h"
#include "fat16.h"
#include "kernel_layout.h"

#define DUMP_BUF_PA   0x9F000U
#define DUMP_BUF_SIZE 0x10000U

static volatile crash_dump_header_t * const dump_hdr =
    (volatile crash_dump_header_t * const)DUMP_BUF_PA;

static uint64_t g_kernel_phys;
static uint64_t g_kernel_size;

/* ── Serial helpers ─────────────────────────────────────────── */
static void dump_serial_init(void)
{
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void dump_serial_putc(char ch)
{
    while ((inb(0x3FD) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)ch);
}

static void dump_serial_write_str(const char *s)
{
    while (*s) dump_serial_putc(*s++);
}

static void dump_hex64(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[17];
    for (int8_t i = 15; i >= 0; i--) buf[15 - i] = hex[(v >> (i * 4)) & 0xF];
    buf[16] = '\0';
    dump_serial_write_str(buf);
}

static void dump_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for (int8_t i = 7; i >= 0; i--) buf[7 - i] = hex[(v >> (i * 4)) & 0xF];
    buf[8] = '\0';
    dump_serial_write_str(buf);
}

/* ── Public API ─────────────────────────────────────────────── */
void crash_dump_init(void)
{
    dump_serial_init();
    g_kernel_phys = KERNEL_PHYS_BASE;
    g_kernel_size = KERNEL_EARLY_MAP_LIMIT - KERNEL_PHYS_BASE;
    dump_hdr->magic = 0;
    dump_hdr->version = 0;
}

void crash_dump_capture(const char *process_name,
                        uint64_t vector, uint64_t error_code,
                        uint64_t rip, uint64_t rsp, uint64_t rbp,
                        uint64_t rflags)
{
    uint64_t cr0, cr2, cr3, cr4, gs_base;
    uint64_t uptime = timer_ticks();

    asm volatile ("mov %%cr0, %0; mov %%cr2, %1; mov %%cr3, %2; mov %%cr4, %3\n"
                  : "=r"(cr0), "=r"(cr2), "=r"(cr3), "=r"(cr4));

    uint32_t msr_lo, msr_hi;
    asm volatile ("rdmsr" : "=a"(msr_lo), "=d"(msr_hi) : "c"(0xC0000101) : "memory");
    gs_base = ((uint64_t)msr_hi << 32) | msr_lo;

    dump_hdr->magic       = CRASH_DUMP_MAGIC;
    dump_hdr->version     = CRASH_DUMP_VERSION;
    dump_hdr->kernel_phys = g_kernel_phys;
    dump_hdr->kernel_size = g_kernel_size;
    dump_hdr->rip         = rip;
    dump_hdr->rsp         = rsp;
    dump_hdr->rbp         = rbp;
    dump_hdr->vector      = vector;
    dump_hdr->error_code  = error_code;
    dump_hdr->rflags      = rflags;
    dump_hdr->cr0         = cr0;
    dump_hdr->cr2         = cr2;
    dump_hdr->cr3         = cr3;
    dump_hdr->cr4         = cr4;
    dump_hdr->gs_base     = gs_base;
    dump_hdr->kernel_rip  = KERNEL_PHYS_BASE;
    dump_hdr->uptime_ticks = uptime;

    if (process_name != NULL) {
        uint32_t i = 0;
        while (process_name[i] && i < sizeof(dump_hdr->reserved) - 1) {
            dump_hdr->reserved[i] = (uint8_t)process_name[i];
            i++;
        }
        dump_hdr->reserved[i] = '\0';
    }

    dump_hdr->dump_size = sizeof(crash_dump_header_t);
}

void crash_dump_flush_serial(void)
{
    if (dump_hdr->magic != CRASH_DUMP_MAGIC) {
        dump_serial_write_str("CRASH_DUMP: no valid dump\r\n");
        return;
    }

    dump_serial_write_str("\r\n===== CRASH DUMP SERIAL =====\r\n");
    dump_serial_write_str("MONIOS KERNEL CRASH DUMP\r\n");
    dump_serial_write_str("magic=0x");
    dump_hex64(dump_hdr->magic);
    dump_serial_write_str(" version=");
    dump_hex32((uint32_t)dump_hdr->version);
    dump_serial_write_str("\r\nRIP=0x");
    dump_hex64(dump_hdr->rip);
    dump_serial_write_str(" RSP=0x");
    dump_hex64(dump_hdr->rsp);
    dump_serial_write_str(" RBP=0x");
    dump_hex64(dump_hdr->rbp);
    dump_serial_write_str("\r\nvector=0x");
    dump_hex64(dump_hdr->vector);
    dump_serial_write_str(" error_code=0x");
    dump_hex64(dump_hdr->error_code);
    dump_serial_write_str(" rflags=0x");
    dump_hex64(dump_hdr->rflags);
    dump_serial_write_str("\r\nCR0=0x");
    dump_hex64(dump_hdr->cr0);
    dump_serial_write_str(" CR2=0x");
    dump_hex64(dump_hdr->cr2);
    dump_serial_write_str(" CR3=0x");
    dump_hex64(dump_hdr->cr3);
    dump_serial_write_str(" CR4=0x");
    dump_hex64(dump_hdr->cr4);
    dump_serial_write_str("\r\nGS.base=0x");
    dump_hex64(dump_hdr->gs_base);
    dump_serial_write_str(" uptime=0x");
    dump_hex64(dump_hdr->uptime_ticks);
    dump_serial_write_str("\r\nkernel_phys=0x");
    dump_hex64(dump_hdr->kernel_phys);
    dump_serial_write_str(" kernel_size=0x");
    dump_hex64(dump_hdr->kernel_size);
    dump_serial_write_str(" dump_size=");
    dump_hex32(dump_hdr->dump_size);
    dump_serial_write_str("\r\nreserved=\"");
    for (uint32_t i = 0; i < sizeof(dump_hdr->reserved) && dump_hdr->reserved[i]; i++) {
        char c = (char)dump_hdr->reserved[i];
        if (c >= 32 && c < 127) dump_serial_putc(c);
    }
    dump_serial_write_str("\"\r\n");

    /* Hex dump of kernel image start */
    dump_serial_write_str("\r\n-- kernel (runtime phys base, first 256 bytes) --\r\n");
    uint32_t col = 0;
    for (uint32_t i = 0; i < 256 && (dump_hdr->kernel_phys + i) < KERNEL_EARLY_MAP_LIMIT; i++) {
        uint8_t b = ((const volatile uint8_t *)(uintptr_t)(dump_hdr->kernel_phys + i))[0];
        dump_serial_putc("0123456789ABCDEF"[b >> 4]);
        dump_serial_putc("0123456789ABCDEF"[b & 0x0F]);
        dump_serial_putc(' ');
        col++;
        if (col == 16) {
            dump_serial_putc('\r');
            dump_serial_putc('\n');
            col = 0;
        }
    }
    dump_serial_write_str("\r\n===== END DUMP =====\r\n");
}

/* Write crash dump header to CRASHDMP.DMP on the FAT16 boot volume.
 * The dump buffer lives at PA 0x9F000 (identity-mapped in low 2 MiB).
 * Returns number of bytes written, or -1 on error.
 */
int crash_dump_write_disk(void)
{
    if (dump_hdr->magic != CRASH_DUMP_MAGIC) {
        dump_serial_write_str("CRASH_DUMP: no valid dump on disk write\r\n");
        return -1;  /* no valid dump to write */
    }

    /* Write the crash dump header as CRASHDMP.DMP in root directory.
     * fat16_write_file creates or overwrites the file atomically
     * from FAT's perspective; the cluster chain is allocated and
     * written in one go.  We write the full 64 KiB dump buffer.
     */
    dump_serial_write_str("CRASH_DUMP: writing CRASHDMP.DMP...\r\n");
    int32_t written = fat16_write_file("/CRASHDMP.DMP",
                                        (const void *)(uintptr_t)DUMP_BUF_PA,
                                        (uint32_t)dump_hdr->dump_size);
    if (written < 0) {
        dump_serial_write_str("CRASH_DUMP: disk write failed (FAT not ready?)\r\n");
        return -1;
    }
    dump_serial_write_str("CRASH_DUMP: disk write ok (");
    dump_hex32((uint32_t)written);
    dump_serial_write_str(" bytes)\r\n");
    return written;
}
