#ifndef _DMA_H_
#define _DMA_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    void *virtual_address;
    uint64_t physical_address;
    uint64_t size;
} dma_buffer_t;

/* ── Existing bump-allocator API (preserved) ─────────────────────── */
void dma_init(void);
bool dma_alloc(uint64_t size, uint64_t align, uint64_t max_physical_address, dma_buffer_t *out_buffer);
uint64_t dma_checkpoint(void);
void dma_rewind(uint64_t checkpoint);
void dma_log_state(void);

/* ── New unified DMA subsystem interface ──────────────────────────── */
/* Initialize the DMA memory pool. Always succeeds (static pool); prints
 * "dma: not found" only if the pool region is invalid. */
bool dma_probe(void);
void dma_shutdown(void);

/* Release a previously allocated buffer. */
bool dma_free(uint64_t phys, uint64_t size);

/* Physical <-> virtual address translation (identity-mapped pool). */
uint64_t dma_virt_to_phys(const void *virt);
void    *dma_phys_to_virt(uint64_t phys);

/* Copy to / from a DMA buffer by physical address. */
int32_t dma_read(void *dst, uint64_t phys, uint32_t len);
int32_t dma_write(uint64_t phys, const void *src, uint32_t len);

/* Descriptor pool (for AHCI / e1000 / virtio rings). Returns physical
 * address; 0 on exhaustion. */
uint64_t dma_desc_alloc(uint32_t count);
void     dma_desc_free(uint64_t phys);

/* Cache-coherence framework. */
void dma_cache_flush(uint64_t phys, uint32_t len);
void dma_cache_invalidate(uint64_t phys, uint32_t len);

/* Statistics. */
uint64_t dma_total_bytes(void);
uint64_t dma_used_bytes(void);
uint32_t dma_alloc_count(void);

/* Status. */
const char *dma_status(void);

#endif
