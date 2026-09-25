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
/* Identity-map a physical range. Both return true when the whole range was
 * covered; false when part of it lies outside the page tables this kernel
 * maintains (which currently extend to 9 GiB). Drivers mapping a device BAR
 * must check the result - touching an unmapped BAR faults. */
bool mmu_map_identity(uint64_t phys_base, uint64_t length);
bool mmu_map_device_identity(uint64_t phys_base, uint64_t length);
uint64_t mmu_get_pml4_phys(void);
int mmu_is_active(void);

/* Clone the kernel's shared (non-user) page-table pieces into a
 * caller-supplied buffer.  Used by the per-process VM layer to build
 * address spaces that share all kernel identity mappings.
 *   mmu_clone_pdpt(dst)  copies PDPT[1..511] (shared PD references);
 *                        dst[0] is left for the caller to set.
 *   mmu_clone_pd0(dst)   copies PD0[0..31] and [34..511]; the caller
 *                        overrides entries 32..33 for the user window. */
void mmu_clone_pdpt(uint64_t *dst_pdpt);
void mmu_clone_pd0(uint64_t *dst_pd0);

#endif
