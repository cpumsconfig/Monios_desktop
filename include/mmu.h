#ifndef _MMU_H_
#define _MMU_H_

#include "stdint.h"

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) desc_ptr_t;

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

#define GDT_KERNEL_CODE_SELECTOR 0x08
#define GDT_KERNEL_DATA_SELECTOR 0x10
#define GDT_USER_DATA_SELECTOR   0x18
#define GDT_USER_CODE_SELECTOR   0x20
#define GDT_TSS_SELECTOR         0x28

void init_gdt(void);
void init_idt(void);
void init_page_tables(void);
void reload_segments(void);
void idt_set_handler(uint8_t vector, uint64_t handler, uint8_t type_attr);
void tss_set_rsp0(uint64_t rsp0);
void mmu_map_identity(uint64_t phys_base, uint64_t length);
uint64_t mmu_get_pml4_phys(void);
int mmu_is_active(void);

#endif
