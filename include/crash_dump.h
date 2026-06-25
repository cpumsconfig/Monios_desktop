#ifndef _CRASH_DUMP_H_
#define _CRASH_DUMP_H_

#include "stdbool.h"
#include "stdint.h"

/* Crash dump magic at the start of the dump buffer */
#define CRASH_DUMP_MAGIC   0x44554D504F4E4944UL   /* "MONIPDUMP" reversed = DUMP... */
#define CRASH_DUMP_VERSION 1

typedef struct {
    uint64_t magic;          /* CRASH_DUMP_MAGIC */
    uint64_t version;        /* CRASH_DUMP_VERSION */
    uint64_t dump_size;     /* total bytes used in dump buffer */
    uint64_t kernel_phys;   /* kernel load physical address */
    uint64_t kernel_size;   /* kernel image size in bytes */
    uint64_t rip;            /* instruction pointer at crash */
    uint64_t rsp;            /* stack pointer at crash */
    uint64_t rbp;            /* frame pointer at crash */
    uint64_t vector;         /* exception/interrupt vector */
    uint64_t error_code;
    uint64_t rflags;
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t gs_base;
    uint64_t kernel_rip;    /* kernel entry / _start */
    uint64_t uptime_ticks;  /* timer ticks at crash */
    uint8_t  reserved[96];  /* padding / future use */
    uint8_t  data_start;    /* variable dump data begins here */
} __attribute__((packed)) crash_dump_header_t;



/* ── Public API ─────────────────────────────────────────────── */
/* Initialise crash dump subsystem. Must be called before any crash can happen. */
void crash_dump_init(void);

/* Called from bsod_panic() / bsod_exception_panic().
 * Fills the dump header and as much context as possible,
 * then returns (does NOT reboot – bsod.c controls reboot timing).
 */
void crash_dump_capture(const char *process_name,
                        uint64_t vector, uint64_t error_code,
                        uint64_t rip, uint64_t rsp, uint64_t rbp,
                        uint64_t rflags);

/* Serialise the current dump buffer to the serial port (COM1).
 * Called by bsod_collect_and_reboot().
 */
void crash_dump_flush_serial(void);

/* Write crash dump to the first free FAT cluster on the boot disk.
 * Returns true if dump was written successfully.
 */
int crash_dump_write_disk(void);

/* Pointer to the crash dump buffer (fixed PA = 0x9F000, 64 KiB). */
extern volatile crash_dump_header_t * const crash_dump_buffer;

#endif
