#include "crash_dump.h"
#include "common.h"
#include "interrupt.h"
#include "fat16.h"
#include "kernel.h"
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

static __attribute__((unused)) void dump_hex32(uint32_t v)
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
    extern uint64_t _kconfig_start;
    volatile uint64_t *kcfg = &_kconfig_start;

    dump_serial_init();
    g_kernel_phys = kcfg[0] != 0 ? kcfg[0] : KERNEL_PHYS_BASE;
    g_kernel_size = 0x03000000ULL;
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
    uint64_t gpr[16];

    asm volatile ("mov %%cr0, %0; mov %%cr2, %1; mov %%cr3, %2; mov %%cr4, %3\n"
                  : "=r"(cr0), "=r"(cr2), "=r"(cr3), "=r"(cr4));

    /* Snapshot every general-purpose register as the kernel sits in the
     * panic path.  Inline asm stores each register into the gpr[] slot. */
    asm volatile (
        "movq %%rax, %0\n\t"
        "movq %%rbx, %1\n\t"
        "movq %%rcx, %2\n\t"
        "movq %%rdx, %3\n\t"
        "movq %%rsi, %4\n\t"
        "movq %%rdi, %5\n\t"
        "movq %%rbp, %6\n\t"
        "movq %%rsp, %7\n\t"
        "movq %%r8,  %8\n\t"
        "movq %%r9,  %9\n\t"
        "movq %%r10, %10\n\t"
        "movq %%r11, %11\n\t"
        "movq %%r12, %12\n\t"
        "movq %%r13, %13\n\t"
        "movq %%r14, %14\n\t"
        "movq %%r15, %15\n\t"
        : "=m"(gpr[0]),  "=m"(gpr[1]),  "=m"(gpr[2]),  "=m"(gpr[3]),
          "=m"(gpr[4]),  "=m"(gpr[5]),  "=m"(gpr[6]),  "=m"(gpr[7]),
          "=m"(gpr[8]),  "=m"(gpr[9]),  "=m"(gpr[10]), "=m"(gpr[11]),
          "=m"(gpr[12]), "=m"(gpr[13]), "=m"(gpr[14]), "=m"(gpr[15])
        : : "memory");

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
    dump_hdr->kernel_rip  = g_kernel_phys;
    dump_hdr->uptime_ticks = uptime;
    for (uint32_t i = 0; i < 16; i++) {
        dump_hdr->gpr[i] = gpr[i];
    }

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
    dump_serial_write_str("RIP=0x");
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
    dump_serial_write_str("\r\nuptime=0x");
    dump_hex64(dump_hdr->uptime_ticks);
    dump_serial_write_str("\r\n===== END DUMP =====\r\n");
}

/* ── Text-log builder (runs entirely out of the static dump buffer) ── */
static char *log_cursor;
static uint32_t log_left;

static void tb_putc(char c)
{
    if (log_left == 0) {
        return;
    }
    *log_cursor++ = c;
    log_left--;
}

static void tb_str(const char *s)
{
    while (s != NULL && *s) {
        tb_putc(*s++);
    }
}

static void tb_hex(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    for (int8_t i = 15; i >= 0; i--) {
        tb_putc(hex[(v >> (i * 4)) & 0xFU]);
    }
}

static void tb_dec(uint64_t v)
{
    char tmp[21];
    uint32_t n = 0;

    if (v == 0) {
        tb_putc('0');
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0) {
        tb_putc(tmp[--n]);
    }
}

static void tb_reg(const char *name, uint64_t value)
{
    tb_str("  ");
    tb_str(name);
    tb_str(" = 0x");
    tb_hex(value);
    tb_str("\r\n");
}

/* Walk the frame-pointer chain.  Frame layout on x86_64:
 *   [rbp]    = saved rbp (parent frame)
 *   [rbp+8]  = return address
 */
static void build_backtrace(void)
{
    uint64_t fp;
    uint32_t frames = 0;

    asm volatile ("mov %%rbp, %0" : "=r"(fp));

    tb_str("\r\n-- Stack backtrace (frame-pointer walk) --\r\n");
    while (fp >= 0x100000ULL && fp < 0x00007FFFFFFFFFFFULL && frames < 16U) {
        uint64_t *frame = (uint64_t *)(uintptr_t)fp;
        uint64_t next_fp;
        uint64_t ret;

        next_fp = frame[0];
        ret = frame[1];
        if (ret < 0x2000000ULL || ret > 0x00007FFFFFFFFFFFULL) {
            break;
        }
        tb_str("  #");
        tb_dec(frames);
        tb_str("  [0x");
        tb_hex(fp);
        tb_str("]  ret = 0x");
        tb_hex(ret);
        tb_str("\r\n");

        dump_hdr->backtrace[frames] = ret;
        frames++;

        if (next_fp <= fp) {
            break;
        }
        fp = next_fp;
    }
    dump_hdr->backtrace_frames = frames;
    if (frames == 0) {
        tb_str("  (no valid frame pointers found)\r\n");
    }
}

/*
 * Enhanced on-disk panic log.  Everything is rendered as readable text into
 * the static 64 KiB dump buffer (no heap allocation, interrupts already off
 * in the panic path) and then atomically overwritten to /PANIC.LOG on the
 * boot FAT16 volume (presented to the user as C:\PANIC.LOG).
 */
int crash_dump_write_disk(void)
{
    const char *logbuf;
    uint32_t loglen;
    uint32_t header_size;
    const char *reg_names[16] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
    };

    if (dump_hdr->magic != CRASH_DUMP_MAGIC) {
        dump_serial_write_str("CRASH_DUMP: no valid dump on disk write\r\n");
        return -1;
    }

    header_size = (uint32_t)sizeof(crash_dump_header_t);
    log_cursor = (char *)(uintptr_t)(DUMP_BUF_PA + header_size);
    log_left = DUMP_BUF_SIZE - header_size;

    tb_str("MONIOS KERNEL PANIC LOG\r\n");
    tb_str("=================================\r\n");
    tb_str("uptime_ticks = ");  tb_dec(dump_hdr->uptime_ticks); tb_str("\r\n");
    tb_str("process      = \"");
    for (uint32_t i = 0; i < sizeof(dump_hdr->reserved) && dump_hdr->reserved[i]; i++) {
        tb_putc((char)dump_hdr->reserved[i]);
    }
    tb_str("\"\r\n");
    tb_str("vector       = 0x");  tb_hex(dump_hdr->vector);      tb_str("\r\n");
    tb_str("error_code   = 0x");  tb_hex(dump_hdr->error_code);  tb_str("\r\n");

    tb_str("\r\n-- Instruction / control registers --\r\n");
    tb_reg("rip",   dump_hdr->rip);
    tb_reg("rsp",   dump_hdr->rsp);
    tb_reg("rbp",   dump_hdr->rbp);
    tb_reg("rflags", dump_hdr->rflags);
    tb_reg("cr0",   dump_hdr->cr0);
    tb_reg("cr2",   dump_hdr->cr2);
    tb_reg("cr3",   dump_hdr->cr3);
    tb_reg("cr4",   dump_hdr->cr4);

    tb_str("\r\n-- General purpose registers --\r\n");
    for (uint32_t i = 0; i < 16; i++) {
        tb_reg(reg_names[i], dump_hdr->gpr[i]);
    }

    build_backtrace();

    tb_str("\r\n-- Last 4 KiB of kernel log buffer --\r\n");
    logbuf = kernel_log_tail(&loglen);
    if (logbuf != NULL && loglen > 0) {
        uint32_t start = (loglen > 4096U) ? (loglen - 4096U) : 0U;
        for (uint32_t i = start; i < loglen; i++) {
            char c = logbuf[i];
            if (c == '\0') {
                c = ' ';
            }
            tb_putc(c);
        }
    } else {
        tb_str("(kernel log buffer empty)\r\n");
    }

    uint32_t text_len = (uint32_t)(log_cursor - ((char *)(uintptr_t)(DUMP_BUF_PA + header_size)));
    dump_hdr->log_len = text_len;
    dump_hdr->dump_size = header_size + text_len;

    dump_serial_write_str("CRASH_DUMP: writing /PANIC.LOG (");
    {
        /* serial echo of size */
        uint32_t v = (uint32_t)dump_hdr->dump_size;
        char tmp[12]; uint32_t n = 0;
        if (v == 0) { dump_serial_putc('0'); }
        while (v > 0 && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
        while (n > 0) dump_serial_putc(tmp[--n]);
    }
    dump_serial_write_str(" bytes)...\r\n");

    int32_t written = fat16_write_file("/PANIC.LOG",
                                        (const void *)(uintptr_t)DUMP_BUF_PA,
                                        (uint32_t)dump_hdr->dump_size);
    if (written < 0) {
        dump_serial_write_str("CRASH_DUMP: disk write failed (FAT not ready?)\r\n");
        return -1;
    }
    dump_serial_write_str("CRASH_DUMP: panic log written OK\r\n");
    return written;
}
