#ifndef _SHM_H_
#define _SHM_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * Shared memory segments.
 *
 * A small table of named (key-based) shared regions.  Each segment owns a
 * contiguous run of physical frames that is mapped read/writable into every
 * attaching process's user address space at a fixed, slot-relative virtual
 * address.  Because the same physical pages are mapped in multiple address
 * spaces, the kernel marks the PTEs with the COW bit so that ordinary
 * address-space teardown does not free the physical frames; shm_destroy()
 * releases them explicitly.
 */

#define SHM_MAX_SEGS        16
#define SHM_MAX_SIZE        (2U * 1024U * 1024U)   /* 2 MiB per segment */
#define SHM_PAGE_SIZE       4096U
#define SHM_VBASE           0x08000000ULL            /* 128 MiB user window */

/* Sub-function numbers for the SYS_SHM_CALL multiplexer. */
#define SHM_OP_CREATE      1U
#define SHM_OP_ATTACH      2U
#define SHM_OP_DETACH      3U
#define SHM_OP_DESTROY     4U

void shm_init(void);

/* Create (or look up) a shared segment identified by key.  size is rounded
 * up to a page multiple and capped at SHM_MAX_SIZE.  Returns the shmid
 * (>=1) or -1 on error. */
int32_t shm_create(uint32_t key, uint32_t size);

/* Map segment shmid into the current process's address space.  Returns the
 * user virtual address, or 0 on error. */
void *shm_attach(int32_t shmid);

/* Unmap the segment previously attached at addr.  Returns 0 on success. */
int32_t shm_detach(const void *addr);

/* Mark a segment for destruction; releases its physical frames once all
 * attachments have gone away.  Returns 0 on success. */
int32_t shm_destroy(int32_t shmid);

#endif /* _SHM_H_ */
