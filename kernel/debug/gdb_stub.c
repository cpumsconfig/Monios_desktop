#include "gdb_stub.h"
#include "common.h"
#include "string.h"
#include "ftrace.h"
#include "crash_dump.h"

#define GDB_COM_PORT 0x2F8
#define GDB_BUFSIZE  8192

/* ── Feature flags ─────────────────────────────────────────────── */
/* gdb_attached is set true the first time a host GDB sends us a packet.
 * While false, exception entries return immediately so the normal BSOD
 * path runs – a kernel without a debugger must never hang inside the stub. */
static volatile bool gdb_attached;
static volatile bool gdb_no_ack;     /* QStartNoAckMode */

/* Stop reason remembered across the GDB protocol loop */
#define STOP_REASON_NONE   0
#define STOP_REASON_SWBKPT 1   /* INT3 software breakpoint hit */
#define STOP_REASON_HWBKPT 2   /* DR0..DR3 hardware breakpoint */
#define STOP_REASON_STEP   3   /* RFLAGS.TF single-step trap */
#define STOP_REASON_SIGTRAP 4  /* other exception reported to GDB */
static volatile uint32_t gdb_stop_reason;
static volatile uint64_t gdb_stop_addr;

/* Single-step state: when GDB asks to step, we arm TF on resume; on the
 * next #DB we report a single-stop and disarm it. */
static volatile bool gdb_step_pending;

/* ── Software breakpoint table (INT3 = 0xCC) ───────────────────── */
typedef struct {
    bool     in_use;
    uint64_t addr;
    uint8_t  saved_byte;
} sw_bp_t;
static sw_bp_t g_sw_bp[GDB_MAX_SW_BP];

/* ── Hardware breakpoint slots (DR0..DR3 / DR7) ────────────────── */
typedef struct {
    bool     in_use;
    uint64_t addr;
    uint32_t type;   /* DBG_HW_* */
} hw_bp_t;
static hw_bp_t g_hw_bp[GDB_MAX_HW_BP];

/* x86_64 GDB register file, in the canonical 'g'-packet order. */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rsp, rip;
    uint64_t cs, ss, ds, es, fs, gs;
    uint64_t rflags;
} gdb_registers_t;

static gdb_registers_t gdb_regs_snapshot;

/* ── Serial hardware ──────────────────────────────────────────── */
static void serial_init_com2(void)
{
    outb(0x2FB, 0x80);
    outb(0x2F8, 0x03);  outb(0x2F9, 0x00);   /* divisor 3 = 115200 */
    outb(0x2FB, 0x03);
    outb(0x2FA, 0xC7);
    outb(0x2FC, 0x0B);
    inb(0x2F8); inb(0x2F8);
}

static void serial_putc(char ch)
{
    while ((inb(0x2FD) & 0x20) == 0) { }
    outb(GDB_COM_PORT, (uint8_t)ch);
}

static int serial_getc(void)
{
    while ((inb(0x2FD) & 1) == 0) { }
    return inb(GDB_COM_PORT);
}

static int serial_getc_nonblock(void)
{
    if ((inb(0x2FD) & 1) == 0) return -1;
    return inb(GDB_COM_PORT);
}

static void serial_drain(void)
{
    while (serial_getc_nonblock() != -1) { }
}

/* Busy-loop wait for an inbound byte with a coarse timeout.  Used at boot
 * before the PIT/interrupts are up, so we cannot use timer_ticks().
 * Returns the byte, or -1 on timeout.  `poll_rounds` is the inner loop
 * count (roughly tuned to ~1s on a modern QEMU CPU). */
static int serial_getc_timeout(uint32_t poll_rounds)
{
    for (uint32_t i = 0; i < poll_rounds; i++) {
        int c = serial_getc_nonblock();
        if (c >= 0) return c;
        /* small spin to throttle the outer loop */
        for (volatile uint32_t d = 0; d < 20; d++) { }
    }
    return -1;
}

/* ── Debug-register access (DR0..DR3, DR7) ────────────────────── */
static __attribute__((unused)) uint64_t gdb_read_dr(uint32_t n)
{
    uint64_t v = 0;
    switch (n) {
    case 0: __asm__ __volatile__("mov %%dr0, %0" : "=r"(v)); break;
    case 1: __asm__ __volatile__("mov %%dr1, %0" : "=r"(v)); break;
    case 2: __asm__ __volatile__("mov %%dr2, %0" : "=r"(v)); break;
    case 3: __asm__ __volatile__("mov %%dr3, %0" : "=r"(v)); break;
    case 7: __asm__ __volatile__("mov %%dr7, %0" : "=r"(v)); break;
    }
    return v;
}

static void gdb_write_dr(uint32_t n, uint64_t v)
{
    switch (n) {
    case 0: __asm__ __volatile__("mov %0, %%dr0" :: "r"(v) : "memory"); break;
    case 1: __asm__ __volatile__("mov %0, %%dr1" :: "r"(v) : "memory"); break;
    case 2: __asm__ __volatile__("mov %0, %%dr2" :: "r"(v) : "memory"); break;
    case 3: __asm__ __volatile__("mov %0, %%dr3" :: "r"(v) : "memory"); break;
    case 7: __asm__ __volatile__("mov %0, %%dr7" :: "r"(v) : "memory"); break;
    }
}

/* Rebuild DR7 from the active hw_bp table and reload DR0..DR3. */
static void gdb_hw_bp_apply(void)
{
    uint64_t dr7 = 0;
    for (int i = 0; i < GDB_MAX_HW_BP; i++) {
        if (!g_hw_bp[i].in_use) continue;
        gdb_write_dr((uint32_t)i, g_hw_bp[i].addr);
        uint64_t rw = (g_hw_bp[i].type == DBG_HW_WRITE) ? 1ULL :
                      (g_hw_bp[i].type == DBG_HW_RW)     ? 2ULL : 0ULL;
        dr7 |= (1ULL << i);              /* Ln local enable */
        dr7 |= (rw << (16 + i * 2));     /* RWn field */
    }
    gdb_write_dr(7, dr7);
}

/* ── Software breakpoint table management ─────────────────────── */
static int gdb_sw_bp_insert(uint64_t addr)
{
    for (int i = 0; i < GDB_MAX_SW_BP; i++) {
        if (g_sw_bp[i].in_use && g_sw_bp[i].addr == addr) return i;
    }
    for (int i = 0; i < GDB_MAX_SW_BP; i++) {
        if (!g_sw_bp[i].in_use) {
            g_sw_bp[i].addr = addr;
            g_sw_bp[i].saved_byte = ((volatile uint8_t *)addr)[0];
            ((volatile uint8_t *)addr)[0] = 0xCC;   /* INT3 */
            g_sw_bp[i].in_use = true;
            return i;
        }
    }
    return -1;
}

static bool gdb_sw_bp_remove(uint64_t addr)
{
    for (int i = 0; i < GDB_MAX_HW_BP; i++) {
        if (g_sw_bp[i].in_use && g_sw_bp[i].addr == addr) {
            ((volatile uint8_t *)addr)[0] = g_sw_bp[i].saved_byte;
            g_sw_bp[i].in_use = false;
            return true;
        }
    }
    return false;
}

static void gdb_sw_bp_unplug_all(void)
{
    for (int i = 0; i < GDB_MAX_SW_BP; i++) {
        if (g_sw_bp[i].in_use) {
            ((volatile uint8_t *)g_sw_bp[i].addr)[0] = g_sw_bp[i].saved_byte;
        }
    }
}

static void gdb_sw_bp_replug_all(void)
{
    for (int i = 0; i < GDB_MAX_SW_BP; i++) {
        if (g_sw_bp[i].in_use) {
            ((volatile uint8_t *)g_sw_bp[i].addr)[0] = 0xCC;
        }
    }
}

static int gdb_hw_bp_insert(uint64_t addr, uint32_t type)
{
    for (int i = 0; i < GDB_MAX_HW_BP; i++) {
        if (g_hw_bp[i].in_use && g_hw_bp[i].addr == addr) {
            g_hw_bp[i].type = type;
            gdb_hw_bp_apply();
            return i;
        }
    }
    for (int i = 0; i < GDB_MAX_HW_BP; i++) {
        if (!g_hw_bp[i].in_use) {
            g_hw_bp[i].in_use = true;
            g_hw_bp[i].addr = addr;
            g_hw_bp[i].type = type;
            gdb_hw_bp_apply();
            return i;
        }
    }
    return -1;
}

static bool gdb_hw_bp_remove(uint64_t addr)
{
    for (int i = 0; i < GDB_MAX_HW_BP; i++) {
        if (g_hw_bp[i].in_use && g_hw_bp[i].addr == addr) {
            g_hw_bp[i].in_use = false;
            gdb_hw_bp_apply();
            return true;
        }
    }
    return false;
}

uint32_t gdb_sw_bp_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < GDB_MAX_SW_BP; i++) if (g_sw_bp[i].in_use) n++;
    return n;
}

uint32_t gdb_hw_bp_count(void)
{
    uint32_t n = 0;
    for (int i = 0; i < GDB_MAX_HW_BP; i++) if (g_hw_bp[i].in_use) n++;
    return n;
}

/* ── GDB packet I/O ───────────────────────────────────────────── */
static uint8_t  gdb_rx_buf[GDB_BUFSIZE];
static __attribute__((unused)) uint8_t  gdb_tx_buf[GDB_BUFSIZE];

static void gdb_send_packet(const uint8_t *data, uint32_t len)
{
    if (!gdb_no_ack) {
        serial_putc('+');
    }
    uint8_t csum = 0;
    serial_putc('$');
    for (uint32_t i = 0; i < len; i++) {
        serial_putc((char)data[i]);
        csum += data[i];
    }
    serial_putc('#');
    serial_putc("0123456789ABCDEF"[(csum >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[csum & 0xF]);

    if (!gdb_no_ack) {
        int ack = serial_getc();
        (void)ack;
    }
}

/* Receive one packet.  Returns length, or -1 on timeout/abort. */
static int gdb_recv_packet(uint8_t *pkt, uint32_t max_len)
{
    while (serial_getc() != '$') { }
    uint8_t csum_received[2] = { (uint8_t)serial_getc(), (uint8_t)serial_getc() };
    uint32_t pos = 0;
    uint8_t csum = 0;
    int ch;
    while ((ch = serial_getc()) != '#' && pos < max_len - 1) {
        if (ch == '$') { pos = 0; csum = 0; continue; }
        pkt[pos++] = (uint8_t)ch;
        csum += (uint8_t)ch;
    }
    pkt[pos] = '\0';

    if (!gdb_no_ack) {
        uint8_t c1 = "0123456789ABCDEF"[(csum >> 4) & 0xF];
        uint8_t c2 = "0123456789ABCDEF"[csum & 0xF];
        if (c1 == (char)csum_received[0] && c2 == (char)csum_received[1])
            serial_putc('+');
        else
            serial_putc('-');
    }
    return (int)pos;
}

/* ── Hex helpers ──────────────────────────────────────────────── */
static uint8_t nibble_hex(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

static char hex_nibble(uint8_t n)
{
    return "0123456789abcdef"[n & 0xF];
}

/* Parse a hex integer from *s, advancing *end. */
static uint64_t parse_hex(const char *s, uint32_t *pos)
{
    uint64_t v = 0;
    while (s[*pos] && s[*pos] != ',' && s[*pos] != ':' && s[*pos] != ';') {
        v = (v << 4) | nibble_hex(s[*pos]);
        (*pos)++;
    }
    return v;
}

/* ── Register pack / unpack (GDB remote protocol x86_64) ────────
 * Order: rax,rbx,rcx,rdx,rsi,rdi,rbp,rsp,r8..r15,rip,eflags,cs,ss,
 *        ds,es,fs,gs  (24 registers, each 16 hex chars = 384 chars)  */
enum {
    REG_RAX = 0, REG_RBX, REG_RCX, REG_RDX,
    REG_RSI, REG_RDI, REG_RBP, REG_RSP,
    REG_R8,  REG_R9,  REG_R10, REG_R11,
    REG_R12, REG_R13, REG_R14, REG_R15,
    REG_RIP, REG_RFLAGS, REG_CS, REG_SS,
    REG_DS, REG_ES, REG_FS, REG_GS,
    REG_COUNT
};

static uint64_t gdb_reg_get(uint32_t n, const gdb_registers_t *r)
{
    switch (n) {
    case REG_RAX: return r->rax;  case REG_RBX: return r->rbx;
    case REG_RCX: return r->rcx;  case REG_RDX: return r->rdx;
    case REG_RSI: return r->rsi;  case REG_RDI: return r->rdi;
    case REG_RBP: return r->rbp;  case REG_RSP: return r->rsp;
    case REG_R8:  return r->r8;   case REG_R9:  return r->r9;
    case REG_R10: return r->r10;  case REG_R11: return r->r11;
    case REG_R12: return r->r12;  case REG_R13: return r->r13;
    case REG_R14: return r->r14;  case REG_R15: return r->r15;
    case REG_RIP: return r->rip;  case REG_RFLAGS: return r->rflags;
    case REG_CS:  return r->cs;   case REG_SS:  return r->ss;
    case REG_DS:  return r->ds;   case REG_ES:  return r->es;
    case REG_FS:  return r->fs;   case REG_GS:  return r->gs;
    }
    return 0;
}

static void gdb_reg_set(uint32_t n, uint64_t v, gdb_registers_t *r)
{
    switch (n) {
    case REG_RAX: r->rax = v; break;  case REG_RBX: r->rbx = v; break;
    case REG_RCX: r->rcx = v; break;  case REG_RDX: r->rdx = v; break;
    case REG_RSI: r->rsi = v; break;  case REG_RDI: r->rdi = v; break;
    case REG_RBP: r->rbp = v; break;  case REG_RSP: r->rsp = v; break;
    case REG_R8:  r->r8 = v;  break;  case REG_R9:  r->r9 = v;  break;
    case REG_R10: r->r10 = v; break;  case REG_R11: r->r11 = v; break;
    case REG_R12: r->r12 = v; break;  case REG_R13: r->r13 = v; break;
    case REG_R14: r->r14 = v; break;  case REG_R15: r->r15 = v; break;
    case REG_RIP: r->rip = v; break; case REG_RFLAGS: r->rflags = v; break;
    case REG_CS:  r->cs = v;  break; case REG_SS:  r->ss = v;  break;
    case REG_DS:  r->ds = v;  break; case REG_ES:  r->es = v;  break;
    case REG_FS:  r->fs = v;  break; case REG_GS:  r->gs = v;  break;
    }
}

static void gdb_pack_regs(const gdb_registers_t *r, char *reply)
{
    uint32_t rp = 0;
    for (uint32_t j = 0; j < REG_COUNT; j++) {
        uint64_t v = gdb_reg_get(j, r);
        for (int8_t k = 15; k >= 0; k--)
            reply[rp++] = hex_nibble((uint8_t)(v >> (k * 4)));
    }
    reply[rp] = '\0';
}

static void gdb_unpack_regs(gdb_registers_t *r, const char *buf)
{
    uint32_t pos = 0;
    for (uint32_t j = 0; j < REG_COUNT; j++) {
        uint64_t v = 0;
        for (uint8_t k = 0; k < 16; k++)
            v = (v << 4) | nibble_hex(buf[pos++]);
        gdb_reg_set(j, v, r);
    }
}

/* ── Memory access (with RLE) ─────────────────────────────────── */
/* Append one byte as two hex chars to reply, honoring outgoing RLE:
 * a run of 3+ identical bytes is emitted as `*<char><2hex>`. */
static void emit_hex_byte(char *reply, uint32_t *rp, uint8_t b,
                          uint8_t *run_byte, uint32_t *run_len)
{
    if (*run_len == 0) {
        reply[(*rp)++] = hex_nibble(b >> 4);
        reply[(*rp)++] = hex_nibble(b & 0xF);
        *run_byte = b;
        *run_len = 1;
    } else if (b == *run_byte && *run_len < 255) {
        (*run_len)++;
    } else {
        /* flush run */
        if (*run_len >= 3) {
            reply[(*rp)++] = '*';
            reply[(*rp)++] = (char)*run_byte;
            reply[(*rp)++] = hex_nibble((uint8_t)(*run_len >> 4));
            reply[(*rp)++] = hex_nibble((uint8_t)(*run_len & 0xF));
        } else {
            for (uint32_t i = 0; i < *run_len; i++) {
                reply[(*rp)++] = hex_nibble(*run_byte >> 4);
                reply[(*rp)++] = hex_nibble(*run_byte & 0xF);
            }
        }
        reply[(*rp)++] = hex_nibble(b >> 4);
        reply[(*rp)++] = hex_nibble(b & 0xF);
        *run_byte = b;
        *run_len = 1;
    }
}

static void flush_run(char *reply, uint32_t *rp, uint8_t run_byte, uint32_t run_len)
{
    if (run_len >= 3) {
        reply[(*rp)++] = '*';
        reply[(*rp)++] = (char)run_byte;
        reply[(*rp)++] = hex_nibble((uint8_t)(run_len >> 4));
        reply[(*rp)++] = hex_nibble((uint8_t)(run_len & 0xF));
    } else {
        for (uint32_t i = 0; i < run_len; i++) {
            reply[(*rp)++] = hex_nibble(run_byte >> 4);
            reply[(*rp)++] = hex_nibble(run_byte & 0xF);
        }
    }
}

static void gdb_mem_read(const char *packet, char *reply)
{
    uint32_t pos = 1;
    uint64_t addr = parse_hex(packet, &pos);
    if (packet[pos] == ',') pos++;
    uint64_t len = parse_hex(packet, &pos);
    if (len > 4096) len = 4096;
    uint32_t rp = 0;
    uint8_t run_byte = 0;
    uint32_t run_len = 0;
    for (uint64_t i = 0; i < len && rp < GDB_BUFSIZE - 8; i++) {
        uint8_t b = ((volatile uint8_t *)(uint64_t)(addr + i))[0];
        emit_hex_byte(reply, &rp, b, &run_byte, &run_len);
    }
    flush_run(reply, &rp, run_byte, run_len);
    reply[rp] = '\0';
}

/* Decode hex/RLE data (after the ':' of an M packet) into a caller buffer. */
static uint32_t decode_hex_rle(const char *src, uint8_t *out, uint32_t out_max)
{
    uint32_t si = 0, oi = 0;
    uint8_t prev = 0;
    while (src[si] && oi < out_max) {
        if (src[si] == '*') {
            /* *<char><2hex> : repeat prev N times (N = count) */
            si++;
            si++;                       /* the literal repeated char */
            uint8_t cnt = (uint8_t)((nibble_hex(src[si]) << 4) | nibble_hex(src[si + 1]));
            si += 2;
            for (uint8_t k = 0; k < cnt && oi < out_max; k++) out[oi++] = prev;
        } else {
            uint8_t b = (uint8_t)((nibble_hex(src[si]) << 4) | nibble_hex(src[si + 1]));
            si += 2;
            out[oi++] = b;
            prev = b;
        }
    }
    return oi;
}

static void gdb_mem_write(const char *packet, char *reply)
{
    uint32_t pos = 1;
    uint64_t addr = parse_hex(packet, &pos);
    if (packet[pos] == ',') pos++;
    uint64_t len = parse_hex(packet, &pos);
    if (len > 4096) { strcpy(reply, "E01"); return; }
    if (packet[pos] == ':') pos++;
    static uint8_t wrbuf[4096];
    uint32_t got = decode_hex_rle(packet + pos, wrbuf, (uint32_t)len);
    for (uint32_t i = 0; i < got; i++)
        ((volatile uint8_t *)(uint64_t)(addr + i))[0] = wrbuf[i];
    strcpy(reply, "OK");
}

/* ── Helpers ──────────────────────────────────────────────────── */
static bool starts_with(const char *s, const char *prefix)
{
    while (*prefix) { if (*s++ != *prefix++) return false; }
    return true;
}

/* ─-- Continue/step resume address (set by c <addr> / s <addr>) ── */
static volatile uint64_t gdb_resume_addr;
static volatile bool gdb_resume_addr_valid;

/* ── Command dispatcher ────────────────────────────────────────── */
static char reply_buf[GDB_BUFSIZE];

/* Set when GDB issues c/s/vCont so the loop exits after replying OK. */
static volatile bool gdb_run_resume;

static void gdb_handle_packet(const uint8_t *pkt, uint32_t len)
{
    (void)len;
    const char *cmd = (const char *)pkt;
    reply_buf[0] = '\0';
    gdb_run_resume = false;

    switch (cmd[0]) {

    /* ── Stop reply / status ── */
    case '?': {
        const char *reason_tag = "";
        if (gdb_stop_reason == STOP_REASON_SWBKPT) reason_tag = "swbreak:;";
        else if (gdb_stop_reason == STOP_REASON_HWBKPT) reason_tag = "hwbreak:;";
        else if (gdb_stop_reason == STOP_REASON_STEP)   reason_tag = "syscall_entry:;";
        uint32_t rp = 0;
        reply_buf[rp++] = 'T';
        reply_buf[rp++] = '0'; reply_buf[rp++] = '5';
        reply_buf[rp++] = 't'; reply_buf[rp++] = 'h'; reply_buf[rp++] = 'r';
        reply_buf[rp++] = 'e'; reply_buf[rp++] = 'a'; reply_buf[rp++] = 'd';
        reply_buf[rp++] = ':'; reply_buf[rp++] = '1'; reply_buf[rp++] = ';';
        while (*reason_tag) reply_buf[rp++] = *reason_tag++;
        reply_buf[rp] = '\0';
        break;
    }

    /* ── Register I/O ── */
    case 'g':
        gdb_pack_regs(&gdb_regs_snapshot, reply_buf);
        break;
    case 'G':
        gdb_unpack_regs(&gdb_regs_snapshot, cmd + 1);
        strcpy(reply_buf, "OK");
        break;

    /* p n : read single register n */
    case 'p': {
        uint32_t pos = 1;
        uint64_t n = parse_hex(cmd, &pos);
        if (n >= REG_COUNT) { strcpy(reply_buf, "E01"); break; }
        uint64_t v = gdb_reg_get((uint32_t)n, &gdb_regs_snapshot);
        uint32_t rp = 0;
        for (int8_t k = 15; k >= 0; k--)
            reply_buf[rp++] = hex_nibble((uint8_t)(v >> (k * 4)));
        reply_buf[rp] = '\0';
        break;
    }
    /* P n=v : write single register n */
    case 'P': {
        uint32_t pos = 1;
        uint64_t n = parse_hex(cmd, &pos);
        if (cmd[pos] == '=') pos++;
        uint64_t v = 0;
        while (cmd[pos] && cmd[pos] != ';') v = (v << 4) | nibble_hex(cmd[pos++]);
        if (n >= REG_COUNT) { strcpy(reply_buf, "E01"); break; }
        gdb_reg_set((uint32_t)n, v, &gdb_regs_snapshot);
        strcpy(reply_buf, "OK");
        break;
    }

    /* ── Memory I/O ── */
    case 'm':
        gdb_mem_read(cmd, reply_buf);
        break;
    case 'M':
        gdb_mem_write(cmd, reply_buf);
        break;

    /* ── Breakpoint management ── */
    case 'Z':
    case 'z': {
        char kind_ch = cmd[1];
        uint32_t pos = 2;
        while (cmd[pos] == ',') pos++;
        uint64_t addr = parse_hex(cmd, &pos);
        bool insert = (cmd[0] == 'Z');
        bool ok = false;
        switch (kind_ch) {
        case '0': ok = insert ? (gdb_sw_bp_insert(addr) >= 0)
                              : gdb_sw_bp_remove(addr); break;
        case '1': ok = insert ? (gdb_hw_bp_insert(addr, DBG_HW_EXEC) >= 0)
                              : gdb_hw_bp_remove(addr); break;
        case '2': ok = insert ? (gdb_hw_bp_insert(addr, DBG_HW_WRITE) >= 0)
                              : gdb_hw_bp_remove(addr); break;
        case '3': ok = insert ? (gdb_hw_bp_insert(addr, DBG_HW_RW) >= 0)
                              : gdb_hw_bp_remove(addr); break;
        case '4': ok = insert ? (gdb_hw_bp_insert(addr, DBG_HW_EXEC) >= 0)
                              : gdb_hw_bp_remove(addr); break;
        default: ok = false; break;
        }
        strcpy(reply_buf, ok ? "OK" : "E01");
        break;
    }

    /* ── Continue / step (optional resume address) ── */
    case 'c': {
        uint32_t pos = 1;
        if (cmd[pos]) {
            gdb_resume_addr = parse_hex(cmd, &pos);
            gdb_resume_addr_valid = true;
        }
        gdb_step_pending = false;
        gdb_run_resume = true;
        strcpy(reply_buf, "OK");
        break;
    }
    case 's': {
        uint32_t pos = 1;
        if (cmd[pos]) {
            gdb_resume_addr = parse_hex(cmd, &pos);
            gdb_resume_addr_valid = true;
        }
        gdb_step_pending = true;
        gdb_run_resume = true;
        strcpy(reply_buf, "OK");
        break;
    }
    case 'k':
        gdb_step_pending = false;
        gdb_attached = false;
        gdb_run_resume = true;
        strcpy(reply_buf, "OK");
        break;

    /* ── vCont ── */
    case 'v': {
        if (starts_with(cmd, "vCont?")) {
            strcpy(reply_buf, "vCont;c;C;s;S");
        }
        else if (starts_with(cmd, "vCont")) {
            bool want_step = false;
            const char *q = cmd;
            while (*q) {
                if (*q == 's' || *q == 'S') want_step = true;
                q++;
            }
            gdb_step_pending = want_step;
            gdb_run_resume = true;
            strcpy(reply_buf, "OK");
        }
        else if (starts_with(cmd, "vKill")) {
            gdb_step_pending = false;
            gdb_attached = false;
            gdb_run_resume = true;
            strcpy(reply_buf, "OK");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;
    }

    /* ── Thread operations ── */
    case 'H':
        strcpy(reply_buf, "OK");
        break;
    case 'T':
        strcpy(reply_buf, "OK");
        break;
    case 'q':
        if (starts_with(cmd, "qSupported")) {
            strcpy(reply_buf,
                "PacketSize=4000;"
                "swbreak+;hwbreak+;"
                "QStartNoAckMode+;"
                "vContSupported+;"
                "multiprocess+");
        }
        else if (starts_with(cmd, "qAttached")) {
            /* We are attached to a live process (reply "1"). */
            strcpy(reply_buf, "1");
        }
        else if (starts_with(cmd, "qSymbol::")) {
            strcpy(reply_buf, "OK");
        }
        else if (starts_with(cmd, "qSymbol:")) {
            strcpy(reply_buf, "");
        }
        else if (starts_with(cmd, "qOffsets")) {
            strcpy(reply_buf, "Text=0;Data=0;Bss=0");
        }
        else if (starts_with(cmd, "qfThreadInfo")) {
            strcpy(reply_buf, "m1");
        }
        else if (starts_with(cmd, "qsThreadInfo")) {
            strcpy(reply_buf, "l");
        }
        else if (starts_with(cmd, "qC")) {
            strcpy(reply_buf, "QC1");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;

    /* ── Set operations ── */
    case 'Q':
        if (starts_with(cmd, "QStartNoAckMode")) {
            gdb_no_ack = true;
            serial_putc('+');
            strcpy(reply_buf, "OK");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;

    case '!':
        strcpy(reply_buf, "OK");
        break;
    case 'D':
        gdb_step_pending = false;
        gdb_attached = false;
        gdb_run_resume = true;
        strcpy(reply_buf, "OK");
        break;
    case 'R':
        strcpy(reply_buf, "");
        break;

    default:
        strcpy(reply_buf, "");
        break;
    }

    gdb_send_packet((const uint8_t *)reply_buf, (uint32_t)strlen(reply_buf));
}

/* Run the GDB command loop until the host asks us to continue/step/kill. */
static void gdb_protocol_loop(void)
{
    gdb_attached = true;
    gdb_run_resume = false;
    while (!gdb_run_resume) {
        int len = gdb_recv_packet(gdb_rx_buf, sizeof(gdb_rx_buf));
        if (len >= 0) {
            gdb_handle_packet(gdb_rx_buf, (uint32_t)len);
        }
    }
}

/* ── Public API ─────────────────────────────────────────────── */
bool gdb_stub_is_attached(void)
{
    return gdb_attached;
}

void gdb_stub_init(void)
{
    serial_init_com2();
    gdb_attached   = false;
    gdb_no_ack     = false;
    gdb_step_pending = false;
    gdb_stop_reason  = STOP_REASON_NONE;
    gdb_resume_addr_valid = false;
    memset(g_sw_bp, 0, sizeof(g_sw_bp));
    memset(g_hw_bp, 0, sizeof(g_hw_bp));
    memset(&gdb_regs_snapshot, 0, sizeof(gdb_registers_t));

    /* Announce stub on serial. */
    const char *banner = "\r\n## MoniOS GDB stub (COM2 115200 8N1) ##\r\n"
                         "## Connect: target remote localhost:1234  ##\r\n";
    while (*banner) { serial_putc(*banner++); }

    /* Wait briefly for a host GDB to connect.  If a $ arrives within the
     * timeout we drop into the protocol loop (initial breakpoint); otherwise
     * boot continues normally and exceptions stay on the BSOD path. */
    int c = serial_getc_timeout(4000000U);
    if (c == '$') {
        serial_drain();
        /* Build a fake snapshot so GDB has something to read at the initial
         * stop.  RIP points back into gdb_stub_init (approximate). */
        gdb_stop_reason = STOP_REASON_SWBKPT;
        gdb_regs_snapshot.rip = (uint64_t)(void *)&gdb_stub_init;
        gdb_protocol_loop();
    }
}

void gdb_break(void)
{
    /* Manually requested breakpoint: only act if a debugger is attached. */
    if (!gdb_attached) return;
}

/* ── Exception entry (called from exception{1,3}_handler) ──────
 * frame points to the saved GPR block:
 *   frame[0]=rax ... frame[14]=r15
 *   frame[15]=rip, frame[16]=cs, frame[17]=rflags  (hardware frame, vec 1/3) */
int gdb_stub_handle_exception(uint64_t *frame, uint64_t vector,
                              uint64_t error_code)
{
    (void)error_code; /* reserved for future GDB vector/error reporting */
    /* No debugger attached?  Chain to normal handling (BSOD / process abort). */
    if (!gdb_attached) {
        return 0;
    }

    /* Snapshot GPRs from the saved block. */
    gdb_regs_snapshot.rax = frame[0];
    gdb_regs_snapshot.rbx = frame[1];
    gdb_regs_snapshot.rcx = frame[2];
    gdb_regs_snapshot.rdx = frame[3];
    gdb_regs_snapshot.rsi = frame[4];
    gdb_regs_snapshot.rdi = frame[5];
    gdb_regs_snapshot.rbp = frame[6];
    gdb_regs_snapshot.r8  = frame[7];
    gdb_regs_snapshot.r9  = frame[8];
    gdb_regs_snapshot.r10 = frame[9];
    gdb_regs_snapshot.r11 = frame[10];
    gdb_regs_snapshot.r12 = frame[11];
    gdb_regs_snapshot.r13 = frame[12];
    gdb_regs_snapshot.r14 = frame[13];
    gdb_regs_snapshot.r15 = frame[14];

    uint64_t hw_rip    = frame[15];
    uint64_t hw_cs     = frame[16];
    uint64_t hw_rflags = frame[17];

    /* Segment registers from the CPU. */
    uint64_t ds_v, es_v, fs_v, gs_v;
    __asm__ __volatile__("mov %%ds, %0" : "=r"(ds_v));
    __asm__ __volatile__("mov %%es, %0" : "=r"(es_v));
    __asm__ __volatile__("mov %%fs, %0" : "=r"(fs_v));
    __asm__ __volatile__("mov %%gs, %0" : "=r"(gs_v));
    gdb_regs_snapshot.ds = ds_v;
    gdb_regs_snapshot.es = es_v;
    gdb_regs_snapshot.fs = fs_v;
    gdb_regs_snapshot.gs = gs_v;
    gdb_regs_snapshot.cs = hw_cs;
    gdb_regs_snapshot.ss = 0x10;
    gdb_regs_snapshot.rflags = hw_rflags;
    gdb_regs_snapshot.rip = hw_rip;
    gdb_regs_snapshot.rsp = (uint64_t)&frame[18];

    /* Classify stop reason & rewind over INT3. */
    gdb_stop_addr = hw_rip;
    if (vector == 1) {
        gdb_stop_reason = gdb_step_pending ? STOP_REASON_STEP : STOP_REASON_HWBKPT;
    } else if (vector == 3) {
        bool ours = false;
        for (int i = 0; i < GDB_MAX_SW_BP; i++) {
            if (g_sw_bp[i].in_use && g_sw_bp[i].addr == hw_rip - 1) { ours = true; break; }
        }
        if (ours) {
            hw_rip = hw_rip - 1;
            gdb_regs_snapshot.rip = hw_rip;
            gdb_stop_addr = hw_rip;
        }
        gdb_stop_reason = STOP_REASON_SWBKPT;
    } else {
        gdb_stop_reason = STOP_REASON_SIGTRAP;
    }

    /* Unplug INT3 bytes while talking to GDB. */
    gdb_sw_bp_unplug_all();

    serial_drain();

    /* Talk to the host. */
    gdb_protocol_loop();

    /* GDB may have changed rip / regs.  Write them back into the frame so
     * iretq resumes at the right place. */
    frame[0]  = gdb_regs_snapshot.rax;
    frame[1]  = gdb_regs_snapshot.rbx;
    frame[2]  = gdb_regs_snapshot.rcx;
    frame[3]  = gdb_regs_snapshot.rdx;
    frame[4]  = gdb_regs_snapshot.rsi;
    frame[5]  = gdb_regs_snapshot.rdi;
    frame[6]  = gdb_regs_snapshot.rbp;
    frame[7]  = gdb_regs_snapshot.r8;
    frame[8]  = gdb_regs_snapshot.r9;
    frame[9]  = gdb_regs_snapshot.r10;
    frame[10] = gdb_regs_snapshot.r11;
    frame[11] = gdb_regs_snapshot.r12;
    frame[12] = gdb_regs_snapshot.r13;
    frame[13] = gdb_regs_snapshot.r14;
    frame[14] = gdb_regs_snapshot.r15;

    /* Optional explicit resume address from c/s <addr>. */
    if (gdb_resume_addr_valid) {
        hw_rip = gdb_resume_addr;
        gdb_resume_addr_valid = false;
    }
    frame[15] = hw_rip;

    /* Arm single-step: set TF in the restored rflags. */
    if (gdb_step_pending) {
        hw_rflags |= 0x100;
    }
    frame[17] = hw_rflags;

    /* Re-plug software breakpoints. */
    gdb_sw_bp_replug_all();

    return 1;
}

/* ── Feature 29: SYS_DEBUG_CTL (67) programmatic control ───────── */
uint64_t debug_ctl(uint64_t op, uint64_t arg1, uint64_t arg2)
{
    switch (op) {
    case DBG_SWBP_SET:
        return (uint64_t)gdb_sw_bp_insert(arg1);
    case DBG_SWBP_CLEAR:
        return gdb_sw_bp_remove(arg1) ? 0 : (uint64_t)-1;
    case DBG_SWBP_CLEAR_ALL: {
        uint32_t n = gdb_sw_bp_count();
        for (int i = 0; i < GDB_MAX_SW_BP; i++) {
            if (g_sw_bp[i].in_use) {
                ((volatile uint8_t *)g_sw_bp[i].addr)[0] = g_sw_bp[i].saved_byte;
                g_sw_bp[i].in_use = false;
            }
        }
        return (uint64_t)n;
    }
    case DBG_HWBP_SET:
        return (uint64_t)gdb_hw_bp_insert(arg1, (uint32_t)arg2);
    case DBG_HWBP_CLEAR: {
        if (arg1 >= GDB_MAX_HW_BP || !g_hw_bp[arg1].in_use) return (uint64_t)-1;
        g_hw_bp[arg1].in_use = false;
        gdb_hw_bp_apply();
        return 0;
    }
    case DBG_STEP_ON:
        gdb_step_pending = true;
        return 0;
    case DBG_CONTINUE:
        gdb_step_pending = false;
        return 0;
    case DBG_READ_REGS: {
        if ((void *)arg1 == 0) return (uint64_t)-1;
        uint64_t *out = (uint64_t *)(uint64_t)arg1;
        out[0]  = gdb_regs_snapshot.rax; out[1]  = gdb_regs_snapshot.rbx;
        out[2]  = gdb_regs_snapshot.rcx; out[3]  = gdb_regs_snapshot.rdx;
        out[4]  = gdb_regs_snapshot.rsi; out[5]  = gdb_regs_snapshot.rdi;
        out[6]  = gdb_regs_snapshot.rbp; out[7]  = gdb_regs_snapshot.rsp;
        out[8]  = gdb_regs_snapshot.r8;  out[9]  = gdb_regs_snapshot.r9;
        out[10] = gdb_regs_snapshot.r10; out[11] = gdb_regs_snapshot.r11;
        out[12] = gdb_regs_snapshot.r12; out[13] = gdb_regs_snapshot.r13;
        out[14] = gdb_regs_snapshot.r14; out[15] = gdb_regs_snapshot.r15;
        out[16] = gdb_regs_snapshot.rip; out[17] = gdb_regs_snapshot.rflags;
        out[18] = gdb_regs_snapshot.cs;  out[19] = gdb_regs_snapshot.ss;
        out[20] = gdb_regs_snapshot.ds;  out[21] = gdb_regs_snapshot.es;
        out[22] = gdb_regs_snapshot.fs;  out[23] = gdb_regs_snapshot.gs;
        return 24;
    }
    case DBG_READ_MEM: {
        const uint64_t *p = (const uint64_t *)(uint64_t)arg2;
        if (p == 0) return (uint64_t)-1;
        uint8_t *buf = (uint8_t *)(uint64_t)p[0];
        uint32_t len = (uint32_t)p[1];
        if (len > 4096) len = 4096;
        for (uint32_t i = 0; i < len; i++)
            buf[i] = ((volatile uint8_t *)(uint64_t)(arg1 + i))[0];
        return (uint64_t)len;
    }
    case DBG_WRITE_MEM: {
        const uint64_t *p = (const uint64_t *)(uint64_t)arg2;
        if (p == 0) return (uint64_t)-1;
        const uint8_t *buf = (const uint8_t *)(uint64_t)p[0];
        uint32_t len = (uint32_t)p[1];
        if (len > 4096) len = 4096;
        for (uint32_t i = 0; i < len; i++)
            ((volatile uint8_t *)(uint64_t)(arg1 + i))[0] = buf[i];
        return (uint64_t)len;
    }
    case DBG_STATUS: {
        uint64_t st = 0;
        st |= ((uint64_t)gdb_sw_bp_count() & 0xFF) << 0;
        st |= ((uint64_t)gdb_hw_bp_count() & 0xFF) << 8;
        st |= ((uint64_t)gdb_step_pending & 1) << 16;
        st |= ((uint64_t)gdb_attached & 1) << 17;
        return st;
    }
    default:
        return (uint64_t)-1;
    }
}
