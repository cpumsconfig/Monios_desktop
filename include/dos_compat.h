#ifndef _DOS_COMPAT_H_
#define _DOS_COMPAT_H_

#include "stdbool.h"
#include "stdint.h"

/* NTVDM-style 16-bit DOS compatibility layer (feature 23).
 *
 * MoniOS runs in long mode, where VM86 is unavailable, so this is a
 * basic 16-bit real-mode *instruction interpreter* rather than hardware
 * VM86.  It supports:
 *   - COM files (loaded at 0x0100, PSP at 0x0000)
 *   - MZ EXE files (header parse + segment relocations, e:cs/ip/ss/sp)
 *   - INT 21h subset: 01h read char, 02h display char, 09h display $-str,
 *                     4Ch terminate, 40h write file handle (stdout)
 *   - INT 20h terminate
 *
 * This is a framework: not every 16-bit opcode is decoded — only the
 * common control-flow / data-movement opcodes needed by simple DOS
 * programs (mov/push/pop/int/jmp/call/ret/hlt/loop).
 */

#define DOS_PSP_SEG        0x0000
#define DOS_COM_LOAD_SEG   (DOS_PSP_SEG + 0x10)
#define DOS_MEM_PARAGRAPHS (640U * 1024U / 16U)
#define DOS_RAM_BYTES      (DOS_MEM_PARAGRAPHS * 16U)

/* MZ/DOS executable header magic */
#define DOS_MZ_MAGIC       0x5A4D

/* Detect a 16-bit DOS MZ/COM image: MZ magic present but NO PE\\0\\0
 * signature at e_lfanew (i.e. not a PE executable). */
bool dos_is_mz_image(const uint8_t *image, uint32_t size);

/* Execute a DOS image.  Returns the INT 21h/4Ch exit code via *exit_code.
 * is_com: treat as a .COM image (no header, load at 0x0100). */
int32_t dos_exec_image(const uint8_t *image, uint32_t size, int is_com,
                       int32_t *exit_code);

/* SYS_DOS_EXEC (66) entry: rbx=path_ptr, rcx=arg1, rdx=arg2. */
uint64_t dos_exec_syscall(uint64_t path_ptr, uint64_t arg1, uint64_t arg2);

/* Console callbacks the interpreter uses to talk to the host.  These are
 * wired to MoniOS terminal/shell services when linked into the kernel. */
void dos_console_putc(char c);
int  dos_console_getc(void);

#endif
