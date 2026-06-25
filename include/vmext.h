#ifndef _VMEXT_H_
#define _VMEXT_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool paging_active;
    bool vma_ready;
    bool lazy_ready;
    uint32_t vma_regions;
    uint32_t lazy_regions;
    uint64_t pml4_phys;
    char status[64];
} vmext_info_t;

void vmext_init(void);
void vmext_refresh(void);
const vmext_info_t *vmext_info(void);
const char *vmext_status(void);

#endif
