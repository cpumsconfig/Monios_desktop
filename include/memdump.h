#ifndef _MEMDUMP_H_
#define _MEMDUMP_H_

#include "stdbool.h"
#include "stdint.h"

/* Binary panic memory dump written to C:\panic.dmp.
 *
 * On-disk layout:
 *   [memdump_header_t]                       fixed-size header
 *   [page_record_t] * num_pages              one record per used 4K page
 *
 * Each page_record_t is:
 *   uint64 phys_addr;
 *   uint8  data[4096];
 *
 * Only "used" pages are collected:
 *   - the low 1 MB physical range (IVT / BDA / BIOS data)
 *   - every frame marked allocated/reserved in the physical frame bitmap
 *   - the kernel image range (always, even if not bitmap-tracked)
 *
 * The whole dump is assembled in a static staging buffer (no heap) and
 * written in one shot with fat16_write_file().  If it would overflow the
 * staging buffer the collector stops early and sets the TRUNCATED flag.
 */

#define MEMDUMP_MAGIC     0x4D4F4E49504D4450ULL   /* "MONIPDMP" */
#define MEMDUMP_VERSION   2
#define MEMDUMP_PAGE_SIZE 4096U

/* flags bits */
#define MEMDUMP_FLAG_TRUNCATED  (1ULL << 0)

typedef struct {
    uint64_t magic;
    uint64_t version;
    uint64_t header_size;      /* sizeof(memdump_header_t) */
    uint64_t dump_size;        /* total bytes written to disk */
    uint64_t uptime_ticks;
    uint64_t vector;           /* exception vector (0 for plain panic) */
    uint64_t error_code;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rflags;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t kernel_phys;
    uint64_t kernel_size;
    uint64_t num_pages;        /* page records following the header */
    uint64_t max_phys_scanned; /* physical address scan limit */
    uint64_t flags;
    uint64_t gpr[16];          /* rax..r15 */
    uint64_t reserved[8];
} __attribute__((packed)) memdump_header_t;

typedef struct {
    uint64_t phys;
    uint8_t  data[MEMDUMP_PAGE_SIZE];
} __attribute__((packed)) memdump_page_record_t;

/* Must be called once during boot (after crash_dump_init). */
void memdump_init(void);

/* Capture register/CPU state and write C:\panic.dmp.
 * Safe to call from the BSOD path (interrupts off, no heap). */
void memdump_write(const char *reason,
                   uint64_t vector, uint64_t error_code,
                   uint64_t rip, uint64_t rsp, uint64_t rbp,
                   uint64_t rflags);

#endif
