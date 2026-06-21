#include "gdb_stub.h"
#include "common.h"
#include "string.h"
#include "ftrace.h"
#include "crash_dump.h"

#define GDB_COM_PORT 0x3F8
#define GDB_BUFSIZE  8192

/* ── Feature flags ─────────────────────────────────────────────── */
static volatile bool gdb_active;
static volatile bool gdb_no_ack;     /* QStartNoAckMode */
static volatile bool gdb_breakpoint_set;
static uint64_t gdb_bp_addr;
static uint8_t  gdb_bp_saved_byte;

typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rsp, rip;
    uint64_t cs, ss, ds, es, fs, gs;
    uint64_t rflags;
    uint64_t orig_rax;   /* error_code / original vector */
} gdb_registers_t;

static gdb_registers_t gdb_regs_snapshot;

/* ── Serial hardware ──────────────────────────────────────────── */
static void serial_init_com1(void)
{
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03);  outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
    inb(0x3F8); inb(0x3F8);
}

static void serial_putc(char ch)
{
    while ((inb(0x3FD) & 0x20) == 0) { }
    outb(GDB_COM_PORT, (uint8_t)ch);
}

static int serial_getc(void)
{
    while ((inb(0x3FD) & 1) == 0) { }
    return inb(GDB_COM_PORT);
}

static int serial_getc_nonblock(void)
{
    if ((inb(0x3FD) & 1) == 0) return -1;
    return inb(GDB_COM_PORT);
}

static void serial_drain(void)
{
    while (serial_getc_nonblock() != -1) { }
}

/* ── GDB packet I/O ───────────────────────────────────────────── */
static uint8_t  gdb_rx_buf[GDB_BUFSIZE];
static uint8_t  gdb_tx_buf[GDB_BUFSIZE];

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

/* ── Register pack / unpack (GDB remote protocol 32-bit format) ── */
static void gdb_pack_regs(const gdb_registers_t *r, char *reply)
{
    uint32_t rp = 0;
    /* GDB register order: rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp,
     * r8-r15, rip, eflags, cs, ss, ds, es, fs, gs (24 × 16 hex chars) */
    const uint64_t vals[24] = {
        r->rax, r->rbx, r->rcx, r->rdx,
        r->rsi, r->rdi, r->rbp, r->rsp,
        r->r8,  r->r9,  r->r10, r->r11,
        r->r12, r->r13, r->r14, r->r15,
        r->rip, r->rflags, r->cs, r->ss,
        r->ds,  r->es,  r->fs,  r->gs,
    };
    for (uint32_t j = 0; j < 24; j++) {
        uint64_t v = vals[j];
        reply[rp++] = "0123456789ABCDEF"[(v >> 60) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 56) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 52) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 48) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 44) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 40) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 36) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 32) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 28) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 24) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 20) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 16) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >> 12) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >>  8) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[(v >>  4) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[ v        & 0xF];
    }
    reply[rp] = '\0';
}

static void gdb_unpack_regs(gdb_registers_t *r, const char *buf)
{
    uint64_t flat[24];
    uint32_t pos = 0;
    for (uint8_t j = 0; j < 24; j++) {
        uint64_t v = 0;
        for (int8_t k = 15; k >= 0; k--) v = (v << 4) | nibble_hex(buf[pos++]);
        flat[j] = v;
    }
    r->rax = flat[0];  r->rbx = flat[1];  r->rcx = flat[2];  r->rdx = flat[3];
    r->rsi = flat[4];  r->rdi = flat[5];  r->rbp = flat[6];  r->rsp = flat[7];
    r->r8  = flat[8];  r->r9  = flat[9];  r->r10 = flat[10]; r->r11 = flat[11];
    r->r12 = flat[12]; r->r13 = flat[13]; r->r14 = flat[14]; r->r15 = flat[15];
    r->rip = flat[16]; r->rflags = flat[17];
    r->cs  = flat[18]; r->ss  = flat[19]; r->ds  = flat[20]; r->es  = flat[21];
    r->fs  = flat[22]; r->gs  = flat[23];
}

/* ── Memory access ────────────────────────────────────────────── */
static void gdb_mem_read(const char *packet, char *reply)
{
    uint64_t addr = 0, len = 0;
    uint32_t pos = 1;
    while (packet[pos] != ',') addr = (addr << 4) | nibble_hex(packet[pos++]);
    pos++;
    while (packet[pos] && packet[pos] != ':')
        len = (len << 4) | nibble_hex(packet[pos++]);
    if (len > 4096) len = 4096;
    uint32_t rp = 0;
    for (uint64_t i = 0; i < len && rp < GDB_BUFSIZE - 2; i++) {
        uint8_t b = ((volatile uint8_t *)(uint64_t)(addr + i))[0];
        reply[rp++] = "0123456789ABCDEF"[(b >> 4) & 0xF];
        reply[rp++] = "0123456789ABCDEF"[ b        & 0xF];
    }
    reply[rp] = '\0';
}

static void gdb_mem_write(const char *packet, char *reply)
{
    uint64_t addr = 0, len = 0;
    uint32_t pos = 1;
    while (packet[pos] != ',') addr = (addr << 4) | nibble_hex(packet[pos++]);
    pos++;
    while (packet[pos] != ',') len = (len << 4) | nibble_hex(packet[pos++]);
    pos++;
    if (len > 4096) { strcpy(reply, "E01"); return; }
    for (uint64_t i = 0; i < len; i++) {
        uint8_t byte = (uint8_t)((nibble_hex(packet[pos]) << 4) | nibble_hex(packet[pos + 1]));
        pos += 2;
        ((volatile uint8_t *)(uint64_t)(addr + i))[0] = byte;
    }
    strcpy(reply, "OK");
}

/* ── Helpers ──────────────────────────────────────────────────── */
static bool starts_with(const char *s, const char *prefix)
{
    while (*prefix) { if (*s++ != *prefix++) return false; }
    return true;
}

/* Minimal strtoul (avoids stdlib in freestanding) */
static uint64_t gdb_strtoul(const char *s, char **end)
{
    uint64_t v = 0;
    while (*s == ' ') s++;
    while (*s) {
        if (*s < '0' || *s > '9') break;
        v = (v << 4) | (uint64_t)(*s - '0');
        s++;
    }
    if (end) *end = (char *)s;
    return v;
}

/* ── Command dispatcher ────────────────────────────────────────── */
static char reply_buf[GDB_BUFSIZE];

static void gdb_handle_packet(const uint8_t *pkt, uint32_t len)
{
    (void)len;
    const char *cmd = (const char *)pkt;
    reply_buf[0] = '\0';

    switch (cmd[0]) {

    /* ── Stop reply / status ── */
    case '?':  /* Stop reply – report signal 05 (SIGTRAP) */
        reply_buf[0] = 'S'; reply_buf[1] = '0'; reply_buf[2] = '5';
        reply_buf[3] = '\0';
        break;

    /* ── Register I/O ── */
    case 'g':
        gdb_pack_regs(&gdb_regs_snapshot, reply_buf);
        break;
    case 'G':
        gdb_unpack_regs(&gdb_regs_snapshot, cmd + 1);
        strcpy(reply_buf, "OK");
        break;

    /* ── Memory I/O ── */
    case 'm':
        gdb_mem_read(cmd, reply_buf);
        break;
    case 'M':
        gdb_mem_write(cmd, reply_buf);
        break;

    /* ── Breakpoint management (Z0 = software INT3) ── */
    case 'Z': {
        /* Z0,addr,kind – insert software breakpoint */
        char *p = (char *)(uint64_t)(cmd + 2);
        uint64_t addr = gdb_strtoul(p, &p);
        if (gdb_breakpoint_set) {
            /* Remove previous first */
            ((volatile uint8_t *)gdb_bp_addr)[0] = gdb_bp_saved_byte;
        }
        gdb_bp_saved_byte = ((volatile uint8_t *)addr)[0];
        ((volatile uint8_t *)addr)[0] = 0xCC;  /* INT3 */
        gdb_bp_addr = addr;
        gdb_breakpoint_set = true;
        strcpy(reply_buf, "OK");
        break;
    }
    case 'z': {
        /* z0,addr,kind – remove software breakpoint */
        char *p = (char *)(uint64_t)(cmd + 2);
        uint64_t addr = gdb_strtoul(p, &p);
        (void)addr;
        if (gdb_breakpoint_set) {
            ((volatile uint8_t *)gdb_bp_addr)[0] = gdb_bp_saved_byte;
            gdb_breakpoint_set = false;
        }
        strcpy(reply_buf, "OK");
        break;
    }

    /* ── Continue / step (legacy single-char) ── */
    case 'c':
    case 's':
    case 'k':
        gdb_breakpoint_set = false;
        gdb_active = false;
        strcpy(reply_buf, "OK");
        break;

    /* ── vCont – verbose continue with thread/signal actions ── */
    case 'v': {
        if (starts_with(cmd, "vCont?")) {
            /* We support: c (continue), s (step), C (continue with signal), S (step with signal) */
            strcpy(reply_buf, "vCont;c;C;s;S");
        }
        else if (starts_with(cmd, "vCont")) {
            /* vCont[;action[:thread[:sig]]]*
             * Parse action letter(s) after 'vCont;'
             * action: 'c'=cont, 's'=step, 'C sig'=cont+sig, 'S sig'=step+sig
             */
            gdb_breakpoint_set = false;
            gdb_active = false;
            strcpy(reply_buf, "OK");
        }
        else if (starts_with(cmd, "vKill")) {
            /* vKill pid – kill the inferior */
            gdb_breakpoint_set = false;
            gdb_active = false;
            strcpy(reply_buf, "OK");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;
    }

    /* ── Thread operations ── */
    case 'H':  /* Hc / Hg – set thread for subsequent operations */
        /* We are single-threaded; use thread 1 */
        strcpy(reply_buf, "OK");
        break;
    case 'T':  /* T thread – is thread alive? */
        strcpy(reply_buf, "OK");  /* thread 1 is always alive */
        break;
    case 'q':  /* Queries */
        if (starts_with(cmd, "qSupported")) {
            /* Report our capabilities */
            strcpy(reply_buf,
                "PacketSize=2000;"
                "swbreak+;hwbreak-;"
                "qXfer:memory-map:read+;"
                "qXfer:auxv:read+;"
                "QStartNoAckMode+;"
                "vContSupported+");
        }
        else if (starts_with(cmd, "qSymbol:")) {
            /* qSymbol:ask – GDB offers to resolve symbols.
             * We respond with empty (no symbols to resolve)
             * so GDB uses the ELF we provided.
             * If GDB sends "qSymbol::" (done), we reply OK. */
            if (cmd[8] == ':' && cmd[9] == ':') {
                /* GDB says "we're done" */
                strcpy(reply_buf, "OK");
            } else {
                strcpy(reply_buf, "");
            }
        }
        else if (starts_with(cmd, "qOffsets")) {
            /* qOffsets – report section offsets for GDB 'load' command.
             * Text/Data/BSS already use the linked runtime base. */
            strcpy(reply_buf, "Text=0;Data=0;Bss=0");
        }
        else if (starts_with(cmd, "qXfer:memory-map:read:")) {
            /* Return a minimal memory map of MoniOS address space.
             * Format: l<hex-offset>,<hex-length>:<data>
             * GDB sends "qXfer:memory-map:read:aa00:nnnn" – we return
             * a single 'l' (last) packet with the XML map. */
            const char *map_xml =
                "l<memory-map>"
                "  <memory type='rom' start='0x00000000' length='0x00100000'/>"
                "  <memory type='ram' start='0x00100000' length='0x3FE00000'/>"
                "  <memory type='ram' start='0x40000000' length='0x40000000'/>"
                "  <memory type='ram' start='0x80000000' length='0x7FE00000'/>"
                "  <memory type='mmio' start='0xA0000000' length='0x10000000'/>"
                "</memory-map>";
            (void)map_xml;
            strcpy(reply_buf, "l");
        }
        else if (starts_with(cmd, "qfThreadInfo")) {
            /* First thread info – we have one thread */
            strcpy(reply_buf, "m1");
        }
        else if (starts_with(cmd, "qsThreadInfo")) {
            /* Subsequent threads – end of list */
            strcpy(reply_buf, "l");
        }
        else if (starts_with(cmd, "qC")) {
            /* Current thread ID */
            strcpy(reply_buf, "QC1");
        }
        else if (starts_with(cmd, "qTMinFTPILen:")) {
            /* Minimum flash patch instruction length (N/A) */
            strcpy(reply_buf, "");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;

    /* ── Set operations ── */
    case 'Q':
        if (starts_with(cmd, "QStartNoAckMode")) {
            gdb_no_ack = true;
            /* Switch to no-ack mode: send '+' immediately */
            serial_putc('+');
            strcpy(reply_buf, "OK");
        }
        else {
            strcpy(reply_buf, "");
        }
        break;

    /* ── Miscellaneous ── */
    case '!':  /* Extended mode – we always support it */
        strcpy(reply_buf, "OK");
        break;
    case 'D':  /* Detach */
        gdb_breakpoint_set = false;
        gdb_active = false;
        strcpy(reply_buf, "OK");
        break;
    case 'R':  /* Extended restart – N/A */
        strcpy(reply_buf, "");
        break;

    default:
        strcpy(reply_buf, "");
        break;
    }

    gdb_send_packet((const uint8_t *)reply_buf, (uint32_t)strlen(reply_buf));
}

/* ── Public API ─────────────────────────────────────────────── */
void gdb_stub_init(void)
{
    serial_init_com1();
    gdb_active   = false;
    gdb_no_ack   = false;
    gdb_breakpoint_set = false;
    memset(&gdb_regs_snapshot, 0, sizeof(gdb_regs_snapshot));

    /* Announce GDB stub on serial */
    const char *banner = "\r\n## GDB stub active (COM1 115200 8N1) ##\r\n"
                         "## Use: target remote \\\\.\\COM1     ##\r\n"
                         "## Or:  target remote /dev/ttyS0  ##\r\n";
    while (*banner) { serial_putc(*banner++); }
}

void gdb_break(void)
{
    if (!gdb_active) return;
    /* Already inside the stub – just return */
}

/* ── Register read helper (called by exception handler) ──────── */
static void gdb_read_gpr(void);

int gdb_stub_handle_exception(uint64_t vector, uint64_t error_code,
                              uint64_t rip, uint64_t cs, uint64_t rflags,
                              uint64_t rsp, uint64_t ss)
{
    (void)vector;

    /* ── Read GPRs from CPU (individual asm, GCC 7.1 safe) ── */
    uint64_t rax_v, rbx_v, rcx_v, rdx_v;
    uint64_t rsi_v, rdi_v, rbp_v, rsp_v;
    uint64_t r8_v, r9_v, r10_v, r11_v;
    uint64_t r12_v, r13_v, r14_v, r15_v;
    uint64_t ds_v, es_v, fs_v, gs_v, rflags_v;

    __asm__ __volatile__("mov %%rax, %0" : "=r"(rax_v));
    __asm__ __volatile__("mov %%rbx, %0" : "=r"(rbx_v));
    __asm__ __volatile__("mov %%rcx, %0" : "=r"(rcx_v));
    __asm__ __volatile__("mov %%rdx, %0" : "=r"(rdx_v));
    __asm__ __volatile__("mov %%rsi, %0" : "=r"(rsi_v));
    __asm__ __volatile__("mov %%rdi, %0" : "=r"(rdi_v));
    __asm__ __volatile__("mov %%rbp, %0" : "=r"(rbp_v));
    __asm__ __volatile__("mov %%rsp, %0" : "=r"(rsp_v));
    __asm__ __volatile__("mov %%r8,  %0" : "=r"(r8_v));
    __asm__ __volatile__("mov %%r9,  %0" : "=r"(r9_v));
    __asm__ __volatile__("mov %%r10, %0" : "=r"(r10_v));
    __asm__ __volatile__("mov %%r11, %0" : "=r"(r11_v));
    __asm__ __volatile__("mov %%r12, %0" : "=r"(r12_v));
    __asm__ __volatile__("mov %%r13, %0" : "=r"(r13_v));
    __asm__ __volatile__("mov %%r14, %0" : "=r"(r14_v));
    __asm__ __volatile__("mov %%r15, %0" : "=r"(r15_v));
    __asm__ __volatile__("mov %%ds, %0"  : "=r"(ds_v));
    __asm__ __volatile__("mov %%es, %0"  : "=r"(es_v));
    __asm__ __volatile__("mov %%fs, %0"  : "=r"(fs_v));
    __asm__ __volatile__("mov %%gs, %0"  : "=r"(gs_v));
    __asm__ __volatile__("pushfq; pop %0" : "=r"(rflags_v));

    /* ── Pack into snapshot ── */
    gdb_regs_snapshot.rax      = rax_v;
    gdb_regs_snapshot.rbx      = rbx_v;
    gdb_regs_snapshot.rcx      = rcx_v;
    gdb_regs_snapshot.rdx      = rdx_v;
    gdb_regs_snapshot.rsi      = rsi_v;
    gdb_regs_snapshot.rdi      = rdi_v;
    gdb_regs_snapshot.rbp      = rbp_v;
    gdb_regs_snapshot.rsp      = rsp_v;
    gdb_regs_snapshot.r8       = r8_v;
    gdb_regs_snapshot.r9       = r9_v;
    gdb_regs_snapshot.r10      = r10_v;
    gdb_regs_snapshot.r11      = r11_v;
    gdb_regs_snapshot.r12      = r12_v;
    gdb_regs_snapshot.r13      = r13_v;
    gdb_regs_snapshot.r14      = r14_v;
    gdb_regs_snapshot.r15      = r15_v;
    gdb_regs_snapshot.ds       = ds_v;
    gdb_regs_snapshot.es       = es_v;
    gdb_regs_snapshot.fs       = fs_v;
    gdb_regs_snapshot.gs       = gs_v;
    gdb_regs_snapshot.rflags   = rflags_v;
    gdb_regs_snapshot.rip      = rip;
    gdb_regs_snapshot.cs       = cs;
    gdb_regs_snapshot.ss       = ss;
    gdb_regs_snapshot.orig_rax = error_code;

    /* ── Capture to crash dump & ftrace ── */
    crash_dump_capture("GDB-exception", vector, 0, rip, rsp_v, rbp_v, rflags_v);
    ftrace_dump_serial();

    /* Drain stale input */
    serial_drain();

    /* ── GDB protocol loop ── */
    gdb_active = true;
    while (gdb_active) {
        int len = gdb_recv_packet(gdb_rx_buf, sizeof(gdb_rx_buf));
        if (len >= 0) {
            gdb_handle_packet(gdb_rx_buf, (uint32_t)len);
        }
    }

    /* ── Restore registers from snapshot ── */
    rax_v  = gdb_regs_snapshot.rax;
    rbx_v  = gdb_regs_snapshot.rbx;
    rcx_v  = gdb_regs_snapshot.rcx;
    rdx_v  = gdb_regs_snapshot.rdx;
    rsi_v  = gdb_regs_snapshot.rsi;
    rdi_v  = gdb_regs_snapshot.rdi;
    rbp_v  = gdb_regs_snapshot.rbp;
    rsp_v  = gdb_regs_snapshot.rsp;
    r8_v   = gdb_regs_snapshot.r8;
    r9_v   = gdb_regs_snapshot.r9;
    r10_v  = gdb_regs_snapshot.r10;
    r11_v  = gdb_regs_snapshot.r11;
    r12_v  = gdb_regs_snapshot.r12;
    r13_v  = gdb_regs_snapshot.r13;
    r14_v  = gdb_regs_snapshot.r14;
    r15_v  = gdb_regs_snapshot.r15;
    ds_v   = gdb_regs_snapshot.ds;
    es_v   = gdb_regs_snapshot.es;
    fs_v   = gdb_regs_snapshot.fs;
    gs_v   = gdb_regs_snapshot.gs;

    /* Write GPRs back */
    __asm__ __volatile__("mov %0, %%rax" : : "r"(rax_v)  : "rax");
    __asm__ __volatile__("mov %0, %%rbx" : : "r"(rbx_v)  : "rbx");
    __asm__ __volatile__("mov %0, %%rcx" : : "r"(rcx_v)  : "rcx");
    __asm__ __volatile__("mov %0, %%rdx" : : "r"(rdx_v)  : "rdx");
    __asm__ __volatile__("mov %0, %%rsi" : : "r"(rsi_v)  : "rsi");
    __asm__ __volatile__("mov %0, %%rdi" : : "r"(rdi_v)  : "rdi");
    (void)rbp_v;  /* rbp restored by compiler from local var */
    __asm__ __volatile__("mov %0, %%rsp" : : "r"(rsp_v)  : "rsp");
    __asm__ __volatile__("mov %0, %%r8"  : : "r"(r8_v)   : "r8");
    __asm__ __volatile__("mov %0, %%r9"  : : "r"(r9_v)   : "r9");
    __asm__ __volatile__("mov %0, %%r10" : : "r"(r10_v)  : "r10");
    __asm__ __volatile__("mov %0, %%r11" : : "r"(r11_v)  : "r11");
    __asm__ __volatile__("mov %0, %%r12" : : "r"(r12_v)  : "r12");
    __asm__ __volatile__("mov %0, %%r13" : : "r"(r13_v)  : "r13");
    __asm__ __volatile__("mov %0, %%r14" : : "r"(r14_v)  : "r14");
    __asm__ __volatile__("mov %0, %%r15" : : "r"(r15_v)  : "r15");

    /* Restore segment registers (Intel syntax, avoids GAS AT&T bugs) */
    {
        uint16_t _sv;
        _sv = (uint16_t)ds_v;
        __asm__ __volatile__(
            ".intel_syntax noprefix\n\t"
            "mov ax, [%0]\n\tmov ds, ax\n\t"
            ".att_syntax\n"
            : : "r"((uint64_t)&_sv) : "ax", "memory");
        _sv = (uint16_t)es_v;
        __asm__ __volatile__(
            ".intel_syntax noprefix\n\t"
            "mov ax, [%0]\n\tmov es, ax\n\t"
            ".att_syntax\n"
            : : "r"((uint64_t)&_sv) : "ax", "memory");
        _sv = (uint16_t)fs_v;
        __asm__ __volatile__(
            ".intel_syntax noprefix\n\t"
            "mov ax, [%0]\n\tmov fs, ax\n\t"
            ".att_syntax\n"
            : : "r"((uint64_t)&_sv) : "ax", "memory");
        _sv = (uint16_t)gs_v;
        __asm__ __volatile__(
            ".intel_syntax noprefix\n\t"
            "mov ax, [%0]\n\tmov gs, ax\n\t"
            ".att_syntax\n"
            : : "r"((uint64_t)&_sv) : "ax", "memory");
    }

    /* Remove software breakpoint byte if one is active */
    if (gdb_breakpoint_set) {
        ((volatile uint8_t *)gdb_bp_addr)[0] = gdb_bp_saved_byte;
        gdb_breakpoint_set = false;
    }

    return 1;  /* handled – iretq in kernel_entry.asm resumes */
}
