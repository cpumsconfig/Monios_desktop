#ifndef _GDB_STUB_H_
#define _GDB_STUB_H_

#include "stdint.h"

/* GDB stub over COM1 (0x3F8) at 115200 8N1.
 * Enable with gdb_stub_init() then connect via:
 *   target remote \\.\COM1
 * or on Linux:  target remote /dev/ttyS0
 */

void gdb_stub_init(void);
void gdb_stub_break(void);

/* Called from INT3 handler in kernel_entry.asm.
 * Receives the full CPU frame and returns:
 *   0 = resume execution normally
 *   1 = stay in GDB remote protocol loop
 */
int gdb_stub_handle_exception(uint64_t vector, uint64_t error_code,
                              uint64_t rip, uint64_t cs, uint64_t rflags,
                              uint64_t rsp, uint64_t ss);

#endif
