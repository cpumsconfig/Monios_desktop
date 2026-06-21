#ifndef _MEMORY_H_
#define _MEMORY_H_

#include "stdint.h"

void memory_init(void);
void *kmalloc(uint64_t size);
void kfree(void *ptr);
uint64_t memory_total_free(void);
uint64_t memory_total_used(void);
uint64_t memory_heap_size(void);

#endif
