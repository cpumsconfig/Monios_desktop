#ifndef _LPT_H_
#define _LPT_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * Standard parallel port (SPP) driver for LPT1 at I/O 0x378.
 *
 *   base+0 : data port  (write)
 *   base+1 : status port (read)
 *   base+2 : control port (read/write)
 *
 * If no physical parallel port is present (as under stock QEMU), the
 * written bytes are captured in an in-kernel spool ring so output remains
 * observable (and can be flushed to C:\Monios\spool\printout.txt).
 */

#define LPT1_BASE 0x378u

/* Status register bits (base+1). */
#define LPT_STATUS_BUSY    0x80u   /* BUSY# (high = not busy) */
#define LPT_STATUS_ACK     0x40u
#define LPT_STATUS_PAPER   0x20u   /* Paper-end */
#define LPT_STATUS_SELECT  0x10u   /* Select / online */
#define LPT_STATUS_ERROR   0x08u   /* ERROR# */

/* Control register bits (base+2). */
#define LPT_CTRL_STROBE    0x01u
#define LPT_CTRL_AUTOLF    0x02u
#define LPT_CTRL_INIT      0x04u   /* nInit, low pulse resets */
#define LPT_CTRL_SELECT    0x08u
#define LPT_CTRL_IRQEN     0x10u

void    lpt_init(void);
bool    lpt_here(void);
int32_t lpt_write_byte(uint8_t data);
int32_t lpt_write(const void *buf, uint32_t len);
uint8_t lpt_status(void);
void    lpt_reset(void);
int32_t lpt_print_text(const char *text, uint32_t len);

/* Spool inspection (no hardware present path). */
uint32_t lpt_spool_bytes(void);
uint32_t lpt_spool_read(uint8_t *out, uint32_t max);

/* syscall 100 (SYS_LPT_CTL) command codes. */
#define LPT_CTL_INIT    0u
#define LPT_CTL_WRITE   1u   /* rcx = user text buffer, rdx = length */
#define LPT_CTL_STATUS  2u   /* returns status byte bits */
#define LPT_CTL_RESET   3u
#define LPT_CTL_SPOOL   4u   /* rcx = user out buffer, rdx = capacity */

int32_t lpt_ctl(uint64_t rbx, uint64_t rcx, uint64_t rdx);

#endif
