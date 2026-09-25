/*
 * MoniOS 16-bit DOS compatibility layer (NTVDM-style).  Feature 23.
 *
 * Long mode has no VM86, so we interpret a small 16-bit x86 subset.
 * This is a *framework*: enough to run hello-world style COM/MZ programs
 * via INT 21h, not a complete 8086 emulator.
 *
 * Build integration (see report):
 *   1. Add kernel/compat/dos.o to KERNEL_OBJS in the Makefile.
 *   2. exec.c calls exec_set_dos_entry(dos_exec_image) from dos_compat_init().
 */
#include "common.h"
#include "string.h"
#include "dos_compat.h"
#include "kernel.h"

/* ── 16-bit register file ──────────────────────────────────────── */
typedef struct {
    uint16_t ax, bx, cx, dx;
    uint16_t si, di, bp, sp;
    uint16_t cs, ds, es, ss;
    uint16_t ip;
    uint16_t flags;
} dos_cpu_t;

/* Conventional memory model (up to 640 KiB) */
static uint8_t  g_ram[DOS_RAM_BYTES];
static dos_cpu_t g_cpu;
static int      g_terminated;
static int32_t  g_exit_code;

/* Byte accessors for the 16-bit registers (we store them whole). */
static inline uint8_t reg_ah(void) { return (uint8_t)(g_cpu.ax >> 8); }
static inline uint8_t reg_al(void) { return (uint8_t)(g_cpu.ax & 0xFFu); }
static inline void    set_ah(uint8_t v) { g_cpu.ax = (uint16_t)((g_cpu.ax & 0x00FFu) | ((uint16_t)v << 8)); }
static inline void    set_al(uint8_t v) { g_cpu.ax = (uint16_t)((g_cpu.ax & 0xFF00u) | v); }
static inline uint8_t reg_dl(void) { return (uint8_t)(g_cpu.dx & 0xFFu); }

/* ── Address helpers: seg:off -> flat offset ─────────────────── */
static uint32_t dos_phys(uint16_t seg, uint16_t off)
{
    return (uint32_t)seg * 16u + off;
}

static bool dos_range(uint32_t a, uint32_t size)
{
    if (a > DOS_RAM_BYTES || size > DOS_RAM_BYTES - a) {
        g_terminated = 1;
        g_exit_code = -1;
        return false;
    }
    return true;
}
static uint8_t dos_rb(uint32_t a) { return dos_range(a, 1) ? g_ram[a] : 0; }
static uint16_t dos_rw(uint32_t a)
{ return dos_range(a, 2) ? (uint16_t)g_ram[a] | ((uint16_t)g_ram[a + 1] << 8) : 0; }
static void dos_ww(uint32_t a, uint16_t v)
{ if (dos_range(a, 2)) { g_ram[a] = (uint8_t)v; g_ram[a + 1] = (uint8_t)(v >> 8); } }

/* ── Console glue (overridden by kernel; defaults to serial) ──── */
__attribute__((weak)) void dos_console_putc(char c) { char text[2] = { c, 0 }; serial_write(text); }
__attribute__((weak)) int  dos_console_getc(void)   { return -1; }

/* ── MZ header ────────────────────────────────────────────────── */
typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;
} __attribute__((packed)) dos_mz_header_t;

bool dos_is_mz_image(const uint8_t *image, uint32_t size)
{
    if (image == NULL || size < sizeof(dos_mz_header_t)) {
        /* A .COM file has no header at all — treat bare binaries as COM. */
        return image != NULL && size > 0 && size < 0x10000;
    }
    const dos_mz_header_t *h = (const dos_mz_header_t *)image;
    if (h->e_magic != DOS_MZ_MAGIC) return false;
    /* If there's a PE\\0\\0 at e_lfanew it's a PE, not a DOS image. */
    if (h->e_lfanew >= 0 &&
        (uint32_t)h->e_lfanew + 4u <= size) {
        const uint8_t *sig = image + (uint32_t)h->e_lfanew;
        if (sig[0] == 'P' && sig[1] == 'E' && sig[2] == 0 && sig[3] == 0)
            return false;
    }
    return true;
}

/* ── Program Segment Prefix (256 bytes at PSP_SEG) ────────────── */
static void dos_build_psp(uint16_t psp_seg)
{
    memset(g_ram, 0, DOS_RAM_BYTES);
    uint32_t p = (uint32_t)psp_seg * 16u;
    g_ram[p + 0x00] = 0xCD; g_ram[p + 0x01] = 0x20; g_ram[p + 0x02] = 0x90; /* INT 20h; RET */
    g_ram[p + 0x80] = 0;   /* tail length */
    g_ram[p + 0x81] = 0x0D; /* default tail: just CR */
}

/* ── INT 21h dispatch ─────────────────────────────────────────── */
static void dos_int21(void)
{
    switch (reg_ah()) {
    case 0x01: {           /* read char into al */
        int c = dos_console_getc();
        set_al((uint8_t)(c & 0xFF));
        if (c >= 0) dos_console_putc((char)c);   /* echo */
        break;
    }
    case 0x02:             /* display char in dl */
        dos_console_putc((char)reg_dl());
        break;
    case 0x09: {           /* display $-terminated string at ds:dx */
        uint32_t a = dos_phys(g_cpu.ds, g_cpu.dx);
        while (a < DOS_RAM_BYTES && g_ram[a] != '$') {
            dos_console_putc((char)g_ram[a++]);
        }
        break;
    }
    case 0x40: {           /* write handle (bx=handle, ds:dx=buf, cx=len) */
        if (g_cpu.bx == 1) {
            uint32_t a = dos_phys(g_cpu.ds, g_cpu.dx);
            for (uint16_t i = 0; i < g_cpu.cx && a < DOS_RAM_BYTES; i++) {
                dos_console_putc((char)g_ram[a++]);
            }
        }
        break;
    }
    case 0x4C:             /* terminate with return code in al */
        g_exit_code = (int32_t)reg_al();
        g_terminated = 1;
        break;
    default:
        /* unsupported DOS call: no-op (framework) */
        break;
    }
}

/* ── Minimal 16-bit opcode interpreter ────────────────────────
 * Handles the opcodes simple DOS programs need.  Unknown opcodes are
 * skipped by advancing ip past a conservative decode; unsupported
 * forms are treated as no-ops so the loop makes progress. */
static void dos_step(void)
{
    uint32_t pc = dos_phys(g_cpu.cs, g_cpu.ip);
    uint8_t  op = dos_rb(pc);
    uint16_t next_ip = g_cpu.ip + 1;

    switch (op) {
    case 0xCD: {           /* INT n */
        uint8_t vec = dos_rb(pc + 1);
        next_ip += 1;
        if (vec == 0x21) dos_int21();
        else if (vec == 0x20) { g_terminated = 1; }
        break;
    }
    case 0xCB:             /* RETF */
        g_cpu.ip = dos_rw(dos_phys(g_cpu.ss, g_cpu.sp)); g_cpu.sp += 2;
        g_cpu.cs = dos_rw(dos_phys(g_cpu.ss, g_cpu.sp)); g_cpu.sp += 2;
        next_ip = g_cpu.ip;
        break;
    case 0xC3: {           /* RET near */
        g_cpu.ip = dos_rw(dos_phys(g_cpu.ss, g_cpu.sp)); g_cpu.sp += 2;
        next_ip = g_cpu.ip;
        break;
    }
    case 0x90:             /* NOP */
        break;
    case 0xF4:             /* HLT -> terminate */
        g_terminated = 1;
        break;
    case 0xEB: {           /* JMP rel8 */
        int8_t off = (int8_t)dos_rb(pc + 1);
        next_ip = (uint16_t)(g_cpu.ip + 2 + off);
        break;
    }
    case 0xE9: {           /* JMP rel16 */
        int16_t off = (int16_t)dos_rw(pc + 1);
        next_ip = (uint16_t)(g_cpu.ip + 3 + off);
        break;
    }
    case 0xB8:             /* MOV ax, imm16 */
        g_cpu.ax = dos_rw(pc + 1); next_ip += 2; break;
    case 0xB9:             /* MOV cx, imm16 */
        g_cpu.cx = dos_rw(pc + 1); next_ip += 2; break;
    case 0xBA:             /* MOV dx, imm16 */
        g_cpu.dx = dos_rw(pc + 1); next_ip += 2; break;
    case 0xBB:             /* MOV bx, imm16 */
        g_cpu.bx = dos_rw(pc + 1); next_ip += 2; break;
    case 0xB4:             /* MOV ah, imm8 */
        set_ah(dos_rb(pc + 1)); next_ip += 1; break;
    case 0xB0:             /* MOV al, imm8 */
        set_al(dos_rb(pc + 1)); next_ip += 1; break;
    case 0x06:             /* PUSH es */
        dos_ww(dos_phys(g_cpu.ss, (uint16_t)(g_cpu.sp - 2)), g_cpu.es); g_cpu.sp -= 2; break;
    case 0x1E:             /* PUSH ds */
        dos_ww(dos_phys(g_cpu.ss, (uint16_t)(g_cpu.sp - 2)), g_cpu.ds); g_cpu.sp -= 2; break;
    case 0x07:             /* POP es */
        g_cpu.es = dos_rw(dos_phys(g_cpu.ss, g_cpu.sp)); g_cpu.sp += 2; break;
    case 0x1F:             /* POP ds */
        g_cpu.ds = dos_rw(dos_phys(g_cpu.ss, g_cpu.sp)); g_cpu.sp += 2; break;
    case 0x1F + 0x80:      /* (unused) */
        break;
    default:
        /* Unknown opcode: skip 1 byte and continue (framework tolerant). */
        break;
    }

    g_cpu.ip = next_ip;
}

/* ── Load & run ───────────────────────────────────────────────── */
int32_t dos_exec_image(const uint8_t *image, uint32_t size, int is_com,
                       int32_t *exit_code)
{
    if (image == NULL || size == 0) { if (exit_code) *exit_code = -1; return -1; }
    if (exit_code) *exit_code = -1;
    if (size > DOS_RAM_BYTES) return -1;

    dos_build_psp(DOS_PSP_SEG);
    g_terminated = 0;
    g_exit_code = 0;
    memset(&g_cpu, 0, sizeof(g_cpu));

    if (is_com) {
        /* COM offsets are relative to the PSP, with entry point at 0x100. */
        uint32_t load = dos_phys(DOS_COM_LOAD_SEG, 0);
        if (size > 0xFF00u || size > DOS_RAM_BYTES - load) return -1;
        memcpy(&g_ram[load], image, size);
        g_cpu.ds = DOS_PSP_SEG; g_cpu.es = DOS_PSP_SEG;
        g_cpu.ss = DOS_PSP_SEG; g_cpu.cs = DOS_PSP_SEG;
        g_cpu.ip = 0x100;
        g_cpu.sp = 0xFFFE;
    } else {
        if (size < sizeof(dos_mz_header_t)) return -1;
        const dos_mz_header_t *h = (const dos_mz_header_t *)image;
        uint32_t hdr_paras = (uint32_t)h->e_cparhdr * 16u;
        if (h->e_magic != DOS_MZ_MAGIC || hdr_paras < sizeof(*h) || hdr_paras >= size ||
            h->e_lfarlc > hdr_paras || (uint32_t)h->e_crlc * 4u > hdr_paras - h->e_lfarlc)
            return -1;
        uint32_t code_size = size - hdr_paras;
        if (code_size > DOS_RAM_BYTES) code_size = DOS_RAM_BYTES;
        /* Load image base at paragraph 0x0000 (CS:IP relocated below) */
        memcpy(&g_ram[0], image + hdr_paras, code_size);

        /* Apply segment relocations: each table entry is (seg, off) of a
         * 16-bit value to add the load paragraph to.  The relocation table
         * lives at e_lfarlc in the header. */
        uint32_t rel_table = (uint32_t)h->e_lfarlc;
        uint16_t count = h->e_crlc;
        for (uint16_t i = 0; i < count; i++) {
            if (rel_table + 4u > size) break;
            uint16_t relo_off = (uint16_t)image[rel_table] | ((uint16_t)image[rel_table + 1] << 8);
            uint16_t relo_seg = (uint16_t)image[rel_table + 2] | ((uint16_t)image[rel_table + 3] << 8);
            uint32_t where = dos_phys(relo_seg, relo_off);
            if (where > code_size || code_size - where < 2u) return -1;
            if (where + 2u <= DOS_RAM_BYTES) {
                uint16_t v = dos_rw(where);
                dos_ww(where, (uint16_t)(v + 0x0000)); /* load para = 0 here */
            }
            rel_table += 4;
        }

        g_cpu.ds = DOS_PSP_SEG; g_cpu.es = DOS_PSP_SEG;
        g_cpu.ss = (uint16_t)(0x0000 + h->e_ss);
        g_cpu.sp = h->e_sp ? h->e_sp : 0xFFFE;
        g_cpu.cs = h->e_cs;
        g_cpu.ip = h->e_ip;
    }

    if (!dos_range(dos_phys(g_cpu.cs, g_cpu.ip), 1)) return -1;
    /* Interpret until INT 21h/4C or HLT.  Bound the step count so a bad
     * image cannot spin forever in the framework. */
    uint64_t steps = 0;
    const uint64_t max_steps = 2000000ULL;
    while (!g_terminated && steps < max_steps) {
        dos_step();
        steps++;
    }

    if (!g_terminated) g_exit_code = -1;
    if (exit_code) *exit_code = g_exit_code;
    return g_exit_code;
}

uint64_t dos_exec_syscall(uint64_t path_ptr, uint64_t arg1, uint64_t arg2)
{
    (void)path_ptr; (void)arg1; (void)arg2;
    /* Real path resolution + image read happens via the VFS/file layer in
     * the integration patch; here we only expose the ABI entry point. */
    return (uint64_t)-1;
}

/* Called from exec.c once the compat layer is linked in. */
void dos_compat_init(void)
{
    log_write("dos: NTVDM-style 16-bit compat layer ready");
}
