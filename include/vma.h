#ifndef _VMA_H_
#define _VMA_H_

#include "stdbool.h"
#include "stdint.h"

#define VMA_FLAG_READ   0x01U
#define VMA_FLAG_WRITE  0x02U
#define VMA_FLAG_EXEC   0x04U
#define VMA_FLAG_USER   0x08U
#define VMA_FLAG_LAZY   0x10U

typedef struct {
    bool used;
    uint32_t id;
    uint64_t base;
    uint64_t size;
    uint32_t flags;
    char name[24];
} vma_entry_t;

void vma_init(void);
int32_t vma_add(uint64_t base, uint64_t size, uint32_t flags, const char *name);
const vma_entry_t *vma_find(uint64_t address);
uint32_t vma_count(void);
bool vma_snapshot(uint32_t index, vma_entry_t *out);
const char *vma_status(void);

#endif
