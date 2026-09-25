#ifndef _PIPE_H_
#define _PIPE_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * Anonymous byte-stream pipes.
 *
 * Each pipe owns a single 4 KiB kernel ring buffer.  A pipe is referenced by
 * two file descriptors in the caller's per-process fd_table: one read end and
 * one write end.  The fd_table entries store a global "pipe handle" that
 * encodes both the pipe id and the direction:
 *
 *   read handle  = PIPE_HANDLE_BASE + pipe_id * 2
 *   write handle = PIPE_HANDLE_BASE + pipe_id * 2 + 1
 *
 * The syscall layer hands the user-facing fd to pipe_read()/pipe_write(); the
 * helpers resolve it through pcb_fd_get_handle() and verify the direction.
 */

#define PIPE_BUF_SIZE        4096U
#define PIPE_MAX_PIPES       16U
#define PIPE_HANDLE_BASE     10000

/* Signal delivered to a writer when all read ends are closed. */
#define PIPE_SIGPIPE         13U

void pipe_init(void);

/* Create an anonymous pipe.  On success allocates two fds in the current
 * process's fd_table and writes them into read_fd / write_fd.  Returns 0 on
 * success, -1 on error. */
int32_t pipe_create(int32_t *read_fd, int32_t *write_fd);

/* Read up to count bytes from the read end of a pipe identified by fd.
 * Returns the number of bytes copied, 0 on EOF (write side fully closed and
 * no data left), or -1 if the fd is not a readable pipe. */
int32_t pipe_read(int32_t fd, void *buf, uint32_t count);

/* Write up to count bytes to the write end.  Returns bytes written, or -1 on
 * error (including SIGPIPE when the read side is fully closed). */
int32_t pipe_write(int32_t fd, const void *buf, uint32_t count);

/* Drop one reference to the pipe side associated with fd (called when the
 * fd is closed).  Frees the pipe buffer once both ends have zero references. */
void pipe_close_fd(int32_t fd);

#endif /* _PIPE_H_ */
