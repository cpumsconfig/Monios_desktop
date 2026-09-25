#include "memdump.h"
#include "common.h"
#include "string.h"
#include "fat16.h"
#include "frame.h"
#include "kernel.h"
#include "kernel_layout.h"

/* Static staging area (BSS, no heap).  Holds the header + up to ~511 pages.
 * Sized to fit comfortably under the loader's image-size budget. */
#define DUMP_STAGING_SIZE (2U * 1024U * 1024U)
static uint8_t g_staging[DUMP_STAGING_SIZE];

/* Hard cap on physical address range we scan (256 MB per spec). */
#define DUMP_MAX_PHYS     (256ULL * 1024ULL * 1024ULL)

/* Low-memory region always dumped (real-mode IVT/BDA/BIOS data). */
#define DUMP_LOW_END      (1ULL * 1024ULL * 1024ULL)

static uint64_t g_kernel_phys;
static uint64_t g_kernel_size;

static void dputc(char c)
{
    while ((inb(0x3FD) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)c);
}
static void dstr(const char *s) { while (*s) dputc(*s++); }
static void dhex(uint64_t v)
{
    static const char h[] = "0123456789ABCDEF";
    for (int8_t i = 15; i >= 0; i--) dputc(h[(v >> (i * 4)) & 0xF]);
}

void memdump_init(void)
{
    extern uint64_t _kconfig_start;
    volatile uint64_t *kcfg = &_kconfig_start;
    g_kernel_phys = kcfg[0] != 0 ? kcfg[0] : KERNEL_PHYS_BASE;
    g_kernel_size = 0x03000000ULL;
}

/* Is this 4K page within the kernel image (always worth keeping)? */
static bool page_in_kernel(uint64_t phys)
{
    return phys >= g_kernel_phys && phys < (g_kernel_phys + g_kernel_size);
}

void memdump_write(const char *reason,
                   uint64_t vector, uint64_t error_code,
                   uint64_t rip, uint64_t rsp, uint64_t rbp,
                   uint64_t rflags)
{
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t gpr[16];

    dstr("\r\nMEMDUMP: writing C:\\panic.dmp ...\r\n");

    asm volatile ("mov %%cr0, %0; mov %%cr2, %1; mov %%cr3, %2; mov %%cr4, %3\n"
                  : "=r"(cr0), "=r"(cr2), "=r"(cr3), "=r"(cr4));

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

    memdump_header_t *hdr = (memdump_header_t *)(uint64_t)g_staging;
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic       = MEMDUMP_MAGIC;
    hdr->version     = MEMDUMP_VERSION;
    hdr->header_size = sizeof(memdump_header_t);
    hdr->uptime_ticks= timer_ticks();
    hdr->vector      = vector;
    hdr->error_code  = error_code;
    hdr->rip         = rip;
    hdr->rsp         = rsp;
    hdr->rbp         = rbp;
    hdr->rflags      = rflags;
    hdr->cr2         = cr2;
    hdr->cr3         = cr3;
    hdr->kernel_phys = g_kernel_phys;
    hdr->kernel_size = g_kernel_size;
    hdr->max_phys_scanned = DUMP_MAX_PHYS;
    for (uint32_t i = 0; i < 16; i++) hdr->gpr[i] = gpr[i];
    (void)reason;
    (void)cr0; (void)cr4;

    uint64_t off = sizeof(memdump_header_t);
    uint64_t num_pages = 0;
    bool truncated = false;

    uint64_t frame_base = frame_base_phys();
    uint32_t frame_total = frame_total_frames();

    for (uint64_t phys = 0; phys < DUMP_MAX_PHYS; phys += MEMDUMP_PAGE_SIZE) {
        bool used = false;
        if (phys < DUMP_LOW_END) {
            used = true;                       /* low 1 MB always kept */
        } else if (phys >= frame_base &&
                   phys < frame_base + (uint64_t)frame_total * MEMDUMP_PAGE_SIZE) {
            used = frame_is_used(phys);       /* frame bitmap */
        }
        if (!used && page_in_kernel(phys)) used = true;

        if (!used) continue;

        if (off + sizeof(memdump_page_record_t) > DUMP_STAGING_SIZE) {
            truncated = true;
            break;
        }
        memdump_page_record_t *rec =
            (memdump_page_record_t *)(uint64_t)(g_staging + off);
        rec->phys = phys;
        /* Physical low memory is identity-mapped, so a direct pointer read
         * is safe here (matches crash_dump / gdb stub access). */
        for (uint32_t i = 0; i < MEMDUMP_PAGE_SIZE; i++)
            rec->data[i] = ((volatile uint8_t *)(uint64_t)(phys + i))[0];

        off += sizeof(memdump_page_record_t);
        num_pages++;
    }

    hdr->num_pages = num_pages;
    hdr->dump_size = off;
    if (truncated) hdr->flags |= MEMDUMP_FLAG_TRUNCATED;

    dstr("MEMDUMP: pages=");
    {
        char tmp[16]; uint32_t n = 0; uint64_t v = num_pages;
        if (v == 0) { dputc('0'); }
        while (v > 0 && n < 16) { tmp[n++] = (char)('0' + (v % 10U)); v /= 10U; }
        while (n > 0) dputc(tmp[--n]);
    }
    dstr(" size=0x"); dhex(off);
    if (truncated) dstr(" (TRUNCATED)");
    dstr("\r\n");

    int32_t written = fat16_write_file("/panic.dmp", g_staging, (uint32_t)off);
    if (written < 0) {
        dstr("MEMDUMP: fat16_write_file(/panic.dmp) FAILED\r\n");
    } else {
        dstr("MEMDUMP: wrote C:\\panic.dmp\r\n");
    }
}
