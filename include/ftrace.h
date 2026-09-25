#ifndef _FTRACE_H_
#define _FTRACE_H_

#include "stdint.h"
#include "stdbool.h"

#define FTRACE_MAX_FUNCTIONS  512
#define FTRACE_EVENT_MAX     16384
#define FTRACE_MASK (FTRACE_EVENT_MAX - 1)

/* Shadow call-stack depth for timing aggregation */
#define FSTACK_DEPTH 64

typedef enum {
    FTRACE_ENTRY = 1,
    FTRACE_EXIT  = 2,
} ftrace_event_type_t;

typedef struct {
    uint32_t        timestamp;     /* CPU ticks (TSC >> 10) */
    ftrace_event_type_t type;
    uint32_t        function_addr; /* low-32 PA */
    uint32_t        parent_addr;
    uint8_t         cpu_id;
    uint8_t         padding[3];
} ftrace_event_t;

/* ── Feature 30: per-function performance profile record ────────── */
typedef struct {
    uint64_t call_count;     /* invocations */
    uint64_t total_cycles;  /* cumulative on-CPU cycles (RDTSC) */
    uint64_t max_cycles;     /* single-instance peak */
    uint64_t sample_count;   /* timer-sampling hits */
} ftrace_profile_t;

void ftrace_init(void);

/* Register a symbol table for name resolution.
 * addrs       – array of uint64_t entry point addresses (physical)
 * names       – matching array of const char* names
 * count       – number of entries
 * base_offset - physical base of kernel image
 */
void ftrace_set_symbols(const uint64_t *addrs, const char * const *names,
                        uint32_t count, uint64_t base_offset);

/* Enable / disable recording */
void ftrace_enable(void);
void ftrace_disable(void);
int  ftrace_is_enabled(void);

/* Manual instrumentation – call from prologue / epilogue hooks.
 * func    – low-32 address of called function
 * parent  – low-32 address of caller (0 for EXIT)
 */
void ftrace_record_entry(uint32_t func, uint32_t parent);
void ftrace_record_exit(uint32_t func);
void ftrace_record_lost(uint32_t count);

/* Serial dump – full verbose listing with names */
void ftrace_dump_serial(void);

/* Serial dump – compact crash-dump style (hex only) */
void ftrace_dump_serial_compact(void);

/* Ring buffer stats */
uint64_t ftrace_event_count(void);
uint32_t ftrace_buffer_size(void);
uint32_t ftrace_lost_count(void);

/* Shell sub-command: "ftrace on|off|dump|clear|count|syms"
 * args may be NULL.
 */
void ftrace_shell_cmd(const char *args);

/* ── Feature 30: profiler control (SYS_PROFILE_CTL 68) ──────────
 * rbx=op, rcx=arg1, rdx=arg2.
 *   0 PROFILE_START     reset & begin aggregation
 *   1 PROFILE_STOP
 *   2 PROFILE_RESET
 *   3 PROFILE_REPORT     print sorted report to serial
 *   4 PROFILE_GET_COUNT  arg1=func addr -> call count
 *   5 PROFILE_GET_CYCLES arg1=func addr -> total cycles
 *   6 PROFILE_SAMPLE_ON  enable timer-PC sampling
 *   7 PROFILE_SAMPLE_OFF
 *   8 PROFILE_STATUS     -> bitmap (bit0=on, bit1=sampling)
 */
#define PROFILE_START      0
#define PROFILE_STOP       1
#define PROFILE_RESET      2
#define PROFILE_REPORT     3
#define PROFILE_GET_COUNT  4
#define PROFILE_GET_CYCLES 5
#define PROFILE_SAMPLE_ON  6
#define PROFILE_SAMPLE_OFF 7
#define PROFILE_STATUS     8

uint64_t profile_ctl(uint64_t op, uint64_t arg1, uint64_t arg2);

/* Called from the timer interrupt when sampling is on: record the
 * interrupted PC so we can attribute sample hits to symbols. */
void ftrace_sample_pc(uint64_t pc);

#endif
