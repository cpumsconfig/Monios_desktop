#ifndef _GDB_STUB_H_
#define _GDB_STUB_H_

#include "stdbool.h"
#include "stdint.h"

/* GDB stub over COM1 (0x3F8) at 115200 8N1.
 * Enable with gdb_stub_init() then connect via:
 *   target remote \\.\COM1            (Windows, physical serial)
 *   target remote /dev/ttyS0         (Linux)
 *   target remote localhost:1234      (via tools/gdb_connect.py socket bridge)
 *
 * Supported protocol subset (x86_64):
 *   g/G      read/write all registers (16 GPRs + rip + rflags + cs/ss/ds/es/fs/gs)
 *   p/P      read/write a single register by number
 *   m/M      read/write memory (hex + RLE)
 *   c/s      continue / single-step (optional resume address)
 *   Z0/z0    software INT3 breakpoints (up to 16)
 *   Z1..Z4   hardware breakpoints/watchpoints via DR0..DR3 / DR7
 *   ?        query stop reason
 *   k        kill, qAttached, qOffsets, qSymbol::, qSupported, vCont, qC
 *
 * The stub drops into the debug loop only when a host GDB is actually
 * attached (it sets gdb_attached on the first inbound packet).  With no
 * debugger attached, exceptions fall through to the normal BSOD path so the
 * existing crash handling is never disturbed.
 */

void gdb_stub_init(void);
void gdb_stub_break(void);

/* Trap frame pushed by exception{1,3}_handler in kernel_entry.asm before
 * calling us.  The GPR block is on the stack in this exact order (low
 * address = rsp at call time):
 *
 *   rax, rbx, rcx, rdx, rsi, rdi, rbp, r8..r15   (15 registers, 120 bytes)
 *   <hardware frame: rip, cs, rflags>             (no error code for vec 1/3)
 *
 * Returns:
 *   1 = handled by GDB – the asm resumes via iretq with the (possibly
 *       modified) registers in the block.
 *   0 = not handled (no debugger attached) – the asm pops the block and
 *       chains into exception_common() for normal processing.
 */
int gdb_stub_handle_exception(uint64_t *frame, uint64_t vector,
                              uint64_t error_code);

/* True once a host GDB has spoken to us over COM1. */
bool gdb_stub_is_attached(void);

/* ── Feature 29: programmatic debugger control (SYS_DEBUG_CTL 67) ──
 * rbx = op, rcx = arg1, rdx = arg2.
 *   0 DBG_SWBP_SET     arg1=addr                 -> bp id (>=0) or -1
 *   1 DBG_SWBP_CLEAR   arg1=addr                 -> 0/-1
 *   2 DBG_SWBP_CLEAR_ALL                         -> count cleared
 *   3 DBG_HWBP_SET     arg1=addr arg2=type(0 exec,1 write,2 rw) -> slot 0..3 or -1
 *   4 DBG_HWBP_CLEAR   arg1=slot                 -> 0/-1
 *   5 DBG_STEP_ON                                -> arm TF single-step
 *   6 DBG_CONTINUE                               -> resume without trap
 *   7 DBG_READ_REGS    arg1=user_ptr(gdb_regs_layout, 192 bytes) -> count
 *   8 DBG_READ_MEM     arg1=addr arg2=ptr{buf,len} -> bytes
 *   9 DBG_WRITE_MEM    arg1=addr arg2=ptr{buf,len} -> bytes
 *  10 DBG_STATUS                                 -> bitmap of active bps
 */
#define DBG_SWBP_SET        0
#define DBG_SWBP_CLEAR      1
#define DBG_SWBP_CLEAR_ALL  2
#define DBG_HWBP_SET        3
#define DBG_HWBP_CLEAR      4
#define DBG_STEP_ON         5
#define DBG_CONTINUE        6
#define DBG_READ_REGS       7
#define DBG_READ_MEM        8
#define DBG_WRITE_MEM       9
#define DBG_STATUS          10

#define DBG_HW_EXEC   0
#define DBG_HW_WRITE  1
#define DBG_HW_RW     2

#define GDB_MAX_SW_BP 16
#define GDB_MAX_HW_BP 4

uint64_t debug_ctl(uint64_t op, uint64_t arg1, uint64_t arg2);

/* Exposed for crash-dump / shell inspection */
uint32_t gdb_sw_bp_count(void);
uint32_t gdb_hw_bp_count(void);

#endif
