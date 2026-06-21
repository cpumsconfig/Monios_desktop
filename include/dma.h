#ifndef _DMA_H_
#define _DMA_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    void *virtual_address;
    uint64_t physical_address;
    uint64_t size;
} dma_buffer_t;

void dma_init(void);
bool dma_alloc(uint64_t size, uint64_t align, uint64_t max_physical_address, dma_buffer_t *out_buffer);
void dma_log_state(void);

#endif
