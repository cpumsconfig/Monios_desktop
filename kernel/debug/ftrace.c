#include "ftrace.h"
#include "common.h"
#include "interrupt.h"

#define FTRACE_MAX FTRACE_EVENT_MAX

/* ── Serial helpers (COM1 115200 8N1) ─────────────────────────── */
static void ftrace_serial_init(void)
{
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void ftrace_serial_putc(char ch)
{
    while ((inb(0x3FD) & 0x20) == 0) { }
    outb(0x3F8, (uint8_t)ch);
}

static void ftrace_serial_write(const char *s)
{
    while (*s) ftrace_serial_putc(*s++);
}

static void ftrace_serial_hex64(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[17];
    for (int8_t i = 15; i >= 0; i--) buf[15 - i] = hex[(v >> (i * 4)) & 0xF];
    buf[16] = '\0';
    ftrace_serial_write(buf);
}

static void ftrace_serial_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for (int8_t i = 7; i >= 0; i--) buf[7 - i] = hex[(v >> (i * 4)) & 0xF];
    buf[8] = '\0';
    ftrace_serial_write(buf);
}

/* ── Ring buffer memory barrier ────────────────────────────────── */
#define FTRACE_MFENCE() __asm__ __volatile__("mfence" : : : "memory")
#define FTRACE_SFENCE() __asm__ __volatile__("sfence" : : : "memory")

/* ── Symbol table ─────────────────────────────────────────────── */
typedef struct {
    uint64_t addr;
    char     name[48];
} ftrace_symbol_t;

static ftrace_symbol_t g_symbols[FTRACE_MAX_FUNCTIONS];
static uint32_t        g_symbol_count;
static uint64_t        g_base_offset;   /* Physical base of kernel image. */
static volatile int    g_enabled;
static volatile uint32_t g_lost_count;

/* ── Ring buffer ──────────────────────────────────────────────── */
static ftrace_event_t  g_event_buf[FTRACE_MAX];
static volatile uint32_t g_head;   /* producer index */
static uint32_t         g_tail;   /* consumer index (dump) */
static volatile uint32_t g_total_lost;

/* ── Name lookup ─────────────────────────────────────────────── */
static const char *ftrace_lookup_name(uint32_t addr)
{
    uint32_t abs = addr + (uint32_t)g_base_offset;
    for (uint32_t i = 0; i < g_symbol_count; i++) {
        if (g_symbols[i].addr == abs)
            return g_symbols[i].name;
    }
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────── */
void ftrace_init(void)
{
    ftrace_serial_init();
    g_head = 0;
    g_tail = 0;
    g_enabled = 0;
    g_total_lost = 0;
    g_symbol_count = 0;
    g_base_offset = 0;
    g_lost_count  = 0;
}

void ftrace_set_symbols(const uint64_t *addrs, const char * const *names,
                        uint32_t count, uint64_t base_offset)
{
    g_base_offset = base_offset;
    if (count > FTRACE_MAX_FUNCTIONS) count = FTRACE_MAX_FUNCTIONS;
    g_symbol_count = count;
    for (uint32_t i = 0; i < count; i++) {
        g_symbols[i].addr = addrs[i];
        uint32_t j = 0;
        while (names[i][j] && j < sizeof(g_symbols[i].name) - 1) {
            g_symbols[i].name[j] = names[i][j];
            j++;
        }
        g_symbols[i].name[j] = '\0';
    }
}

void ftrace_enable(void)
{
    g_enabled = 1;
    FTRACE_SFENCE();
}

void ftrace_disable(void)
{
    FTRACE_MFENCE();
    g_enabled = 0;
}

int ftrace_is_enabled(void)
{
    return g_enabled;
}

/* ── Ring-buffer producers ─────────────────────────────────────
 * Memory barrier sequence:
 *   1. Read g_head (consumer view)   → SFENCE afterwards
 *   2. Write event fields             → MFENCE afterwards
 *   3. Increment g_head              → SFENCE afterwards
 * This ensures the event data is globally visible before
 * the index update, preventing a consumer from reading
 * stale data after observing a new head value.
 */
void ftrace_record_entry(uint32_t func, uint32_t parent)
{
    if (!g_enabled) return;

    /* Read consumer head with SFENCE to order subsequent data reads */
    volatile uint32_t head_before = g_head;
    (void)head_before;
    FTRACE_SFENCE();

    uint32_t idx = g_head & (FTRACE_MASK);

    /* Write event data */
    g_event_buf[idx].timestamp      = timer_ticks();
    g_event_buf[idx].function_addr  = func;
    g_event_buf[idx].parent_addr    = parent;
    g_event_buf[idx].type           = FTRACE_ENTRY;
    g_event_buf[idx].cpu_id         = 0;
    g_event_buf[idx].padding[0] = 0;
    g_event_buf[idx].padding[1] = 0;
    g_event_buf[idx].padding[2] = 0;

    /* Publish event: MFENCE before head update prevents a producer
     * from publishing a partial event that a consumer could observe
     * after seeing the new head value. */
    FTRACE_MFENCE();

    /* Update producer index (SFENCE orders this after the event data) */
    g_head++;
    FTRACE_SFENCE();

    /* Check for overflow: if head advanced more than buffer size,
     * signal lost events (simple approximation) */
    if ((g_head - g_tail) > FTRACE_MAX) {
        g_total_lost++;
    }
}

void ftrace_record_exit(uint32_t func)
{
    if (!g_enabled) return;

    FTRACE_SFENCE();
    uint32_t idx = g_head & (FTRACE_MASK);

    g_event_buf[idx].timestamp     = timer_ticks();
    g_event_buf[idx].function_addr = func;
    g_event_buf[idx].parent_addr   = 0;
    g_event_buf[idx].type          = FTRACE_EXIT;
    g_event_buf[idx].cpu_id        = 0;
    g_event_buf[idx].padding[0] = 0;
    g_event_buf[idx].padding[1] = 0;
    g_event_buf[idx].padding[2] = 0;

    FTRACE_MFENCE();
    g_head++;
    FTRACE_SFENCE();

    if ((g_head - g_tail) > FTRACE_MAX) {
        g_total_lost++;
    }
}

void ftrace_record_lost(uint32_t count)
{
    /* Count discarded events – not currently storable but tracked */
    g_lost_count += count;
}

uint64_t ftrace_event_count(void)
{
    uint32_t h = g_head;
    uint32_t t = g_tail;
    return (uint64_t)((int32_t)h - (int32_t)t);
}

uint32_t ftrace_buffer_size(void)
{
    return FTRACE_MAX;
}

uint32_t ftrace_lost_count(void)
{
    return g_total_lost + g_lost_count;
}

/* ── Serial dump: verbose ──────────────────────────────────────── */
void ftrace_dump_serial(void)
{
    /* Snapshot the ring buffer end pointers under a memory barrier */
    FTRACE_MFENCE();
    uint32_t start = g_tail;
    uint32_t end   = g_head;
    FTRACE_MFENCE();

    ftrace_serial_write("\r\n====== FTRACE DUMP ======\r\n");
    ftrace_serial_write("MONIOS function graph tracer\r\n");
    ftrace_serial_write("enabled=");
    ftrace_serial_putc(g_enabled ? '1' : '0');
    ftrace_serial_write(" events=");
    ftrace_serial_hex64(ftrace_event_count());
    ftrace_serial_write(" lost=");
    ftrace_serial_hex32(g_total_lost);
    ftrace_serial_write(" symbols=");
    ftrace_serial_hex32(g_symbol_count);
    ftrace_serial_write("\r\n\r\n");

    ftrace_serial_write("  TICKS       FUNC          CALLER         T  NAME\r\n");
    ftrace_serial_write("--------  ----------  --------------  --  --------------\r\n");

    uint32_t count = end - start;
    if (count > FTRACE_MAX) count = FTRACE_MAX;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i) & FTRACE_MASK;
        const ftrace_event_t *e = &g_event_buf[idx];

        ftrace_serial_hex32((uint32_t)e->timestamp);
        ftrace_serial_putc(' ');

        ftrace_serial_hex32(e->function_addr);
        ftrace_serial_putc(' ');

        ftrace_serial_hex32(e->parent_addr);
        ftrace_serial_putc(' ');

        const char *type_str;
        char type_ch;
        if (e->type == FTRACE_ENTRY) {
            type_str = "ENTRY>"; type_ch = '>';
        } else if (e->type == FTRACE_EXIT) {
            type_str = "EXIT <"; type_ch = '<';
        } else {
            type_str = "LOST !"; type_ch = '!';
        }
        ftrace_serial_write(type_str);
        ftrace_serial_putc(' ');

        const char *name = ftrace_lookup_name(e->function_addr);
        if (name != NULL) {
            uint32_t n = 0;
            while (name[n] && n < 20) {
                ftrace_serial_putc(name[n++]);
            }
        } else {
            ftrace_serial_write("(unknown)");
        }
        ftrace_serial_write("\r\n");
    }

    ftrace_serial_write("\r\n====== END FTRACE =======\r\n");
}

/* ── Serial dump: compact crash-dump style ───────────────────────
 * Output format (one line per event):
 *   TTTTTTTT ENTRY  PPPP  FUNC_NAME
 *   TTTTTTTT EXIT   ----  FUNC_NAME
 * No ASCII table borders – minimal for serial output under panic.
 */
void ftrace_dump_serial_compact(void)
{
    FTRACE_MFENCE();
    uint32_t start = g_tail;
    uint32_t end   = g_head;
    FTRACE_MFENCE();

    ftrace_serial_write("\r\n## FT ##\r\n");
    uint32_t count = end - start;
    if (count > FTRACE_MAX) count = FTRACE_MAX;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (start + i) & FTRACE_MASK;
        const ftrace_event_t *e = &g_event_buf[idx];

        ftrace_serial_hex32((uint32_t)e->timestamp);
        ftrace_serial_putc(' ');

        if (e->type == FTRACE_ENTRY) {
            ftrace_serial_write("ENTRY ");
            ftrace_serial_hex32(e->parent_addr);
            ftrace_serial_putc(' ');
            const char *name = ftrace_lookup_name(e->function_addr);
            if (name) {
                uint32_t n = 0;
                while (name[n]) ftrace_serial_putc(name[n++]);
            } else {
                ftrace_serial_hex32(e->function_addr);
            }
        } else {
            ftrace_serial_write("EXIT  ---- ");
            const char *name = ftrace_lookup_name(e->function_addr);
            if (name) {
                uint32_t n = 0;
                while (name[n]) ftrace_serial_putc(name[n++]);
            } else {
                ftrace_serial_hex32(e->function_addr);
            }
        }
        ftrace_serial_write("\r\n");
    }
    ftrace_serial_write("## FT END ##\r\n");
}

/* ── Symbol table dump (serial) ───────────────────────────────── */
static void ftrace_dump_symbols_serial(void)
{
    ftrace_serial_write("\r\n## FT SYMBOLS ##\r\n");
    for (uint32_t i = 0; i < g_symbol_count; i++) {
        ftrace_serial_hex64(g_symbols[i].addr);
        ftrace_serial_putc(' ');
        uint32_t n = 0;
        while (g_symbols[i].name[n]) {
            ftrace_serial_putc(g_symbols[i].name[n++]);
        }
        ftrace_serial_write("\r\n");
    }
    ftrace_serial_write("## END SYMBOLS ##\r\n");
}

/* ── Shell command ───────────────────────────────────────────── */
void ftrace_shell_cmd(const char *args)
{
    if (args == NULL) {
        ftrace_serial_write("ftrace: on|off|dump|clear|count|syms\r\n");
        return;
    }

    /* Match first token (up to space / tab / null) */
    const char *p = args;
    while (*p == ' ' || *p == '\t') p++;

    if (p[0] == 'o' && p[1] == 'n' && (p[2] == '\0' || p[2] == ' ' || p[2] == '\t')) {
        ftrace_enable();
        ftrace_serial_write("ftrace: enabled\r\n");
    } else if (p[0] == 'o' && p[1] == 'f' && p[2] == 'f' && (p[3] == '\0' || p[3] == ' ' || p[3] == '\t')) {
        ftrace_disable();
        ftrace_serial_write("ftrace: disabled\r\n");
    } else if (p[0] == 'd' && p[1] == 'u' && p[2] == 'm' && p[3] == 'p' && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
        ftrace_dump_serial();
    } else if (p[0] == 'c' && p[1] == 'l' && p[2] == 'e' && p[3] == 'a' && p[4] == 'r' && (p[5] == '\0' || p[5] == ' ' || p[5] == '\t')) {
        FTRACE_MFENCE();
        g_head = 0;
        g_tail = 0;
        g_total_lost = 0;
        g_lost_count = 0;
        FTRACE_SFENCE();
        ftrace_serial_write("ftrace: buffer cleared\r\n");
    } else if (p[0] == 'c' && p[1] == 'o' && p[2] == 'u' && p[3] == 'n' && p[4] == 't' && (p[5] == '\0' || p[5] == ' ' || p[5] == '\t')) {
        ftrace_serial_write("events=");
        ftrace_serial_hex64(ftrace_event_count());
        ftrace_serial_write(" lost=");
        ftrace_serial_hex32(ftrace_lost_count());
        ftrace_serial_write(" buffer=");
        ftrace_serial_hex32(FTRACE_MAX);
        ftrace_serial_write(" symbols=");
        ftrace_serial_hex32(g_symbol_count);
        ftrace_serial_write("\r\n");
    } else if (p[0] == 's' && p[1] == 'y' && p[2] == 'm' && p[3] == 's' && (p[4] == '\0' || p[4] == ' ' || p[4] == '\t')) {
        ftrace_dump_symbols_serial();
    } else {
        ftrace_serial_write("ftrace: unknown '");
        uint32_t i = 0;
        while (p[i] && p[i] != ' ' && p[i] != '\t' && i < 20) {
            ftrace_serial_putc(p[i++]);
        }
        ftrace_serial_write("'\r\n");
    }
}
