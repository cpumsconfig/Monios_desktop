#ifndef _FTRACE_H_
#define _FTRACE_H_

#include "stdint.h"
#include "stdbool.h"

#define FTRACE_MAX_FUNCTIONS  512
#define FTRACE_EVENT_MAX     16384
#define FTRACE_MASK (FTRACE_EVENT_MAX - 1)

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

#endif
