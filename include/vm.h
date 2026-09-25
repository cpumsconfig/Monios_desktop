#ifndef _VM_H_
#define _VM_H_

#include "stdbool.h"
#include "stdint.h"

/*
 * Per-process virtual address space isolation.
 *
 * Each process gets a private PML4 / PDPT / PD0 triple plus a 4 MiB
 * physical user-region backed at virtual 0x04000000..0x04400000.
 * All kernel identity mappings (everything outside the user window)
 * are shared across every address space, so the kernel can run with
 * any process CR3 loaded and can touch a process's user pages directly
 * through their physical addresses (identity-mapped).
 */

#define VM_USER_BASE       0x04000000ULL
#define VM_USER_SIZE       0x00400000ULL   /* 4 MiB */
#define VM_USER_LIMIT      (VM_USER_BASE + VM_USER_SIZE)
#define VM_MAX_PROCESSES   64

/* Demand-paged user region: everything above the pre-mapped 4 MiB window up
 * to the 4 GiB boundary.  Pages are backed on demand by the #PF handler. */
#define VM_DEMAND_BEGIN    VM_USER_LIMIT   /* 0x04400000 */
#define VM_DEMAND_END      0x100000000ULL /* 4 GiB      */
#define VM_DEMAND_MAX_PAGES 256U
#define VM_DEMAND_MAX_TABLES 64U

/* Copy-on-Write (COW) support.
 *
 * PTE/PDE available bit 9 is used as the COW marker.  When a page is
 * shared between parent and child after fork(), both mappings are marked
 * read-only with the COW bit set.  A subsequent write triggers a #PF,
 * and the fault handler allocates a private copy, maps it writable, and
 * clears the COW bit.
 *
 * The fixed 4 MiB user window uses 2 MiB large pages, so COW there
 * operates at 2 MiB granularity (PDE bit 9).  Demand-paged pages use
 * 4 KiB granularity (PTE bit 9). */
#define PAGE_COW_BIT   0x200ULL   /* available bit 9 in PTE/PDE */

/* One address-space descriptor, embedded in the PCB. */
typedef struct {
    uint64_t pml4_phys;       /* physical address of the top-level page table */
    uint64_t user_phys_base;  /* physical base of the 4 MiB user region       */
    bool     active;          /* true after vm_create_address_space()          */
    bool     cow_shared;      /* true if user pages are currently COW-shared   */
    /* --- demand paging bookkeeping --- */
    uint64_t demand_pages[VM_DEMAND_MAX_PAGES]; /* page-aligned vaddrs backed on demand */
    uint32_t demand_count;
    uint64_t pt_pages[VM_DEMAND_MAX_TABLES];    /* PD/PT physical pages we allocated    */
    uint32_t pt_count;
} vm_address_space_t;

void vm_init(void);

/* Create a fresh address space: private user region + shared kernel mappings.
 * Returns true on success, false on allocation failure. */
bool vm_create_address_space(vm_address_space_t *out);

/* Tear down an address space and free its physical pages. */
void vm_destroy_address_space(vm_address_space_t *as);

/* Switch CPU to the given address space (writes CR3). Pass NULL to switch
 * back to the kernel's master page tables. */
void vm_switch_to(const vm_address_space_t *as);

/* Return the PML4 physical currently loaded in CR3. */
uint64_t vm_current_pml4(void);

/* Zero the user region of an address space (used by exec before loading). */
void vm_zero_user_region(const vm_address_space_t *as);

/* Copy the user region from one address space to another (used by fork). */
bool vm_copy_user_region(vm_address_space_t *dst, const vm_address_space_t *src);

/* --- Copy-on-Write (COW) fork support --- */
/* Set up COW sharing between parent and child address spaces.  Marks all
 * user pages (both the 4 MiB window and demand-paged pages) read-only with
 * the COW bit set, so that the first write triggers a private copy.
 * Returns true on success. */
bool vm_cow_share(vm_address_space_t *parent, vm_address_space_t *child);

/* Handle a write fault on a COW page.  Allocates a private physical frame,
 * copies the shared page contents, and remaps it writable (clearing COW).
 * Returns true if the fault was resolved as a COW copy. */
bool vm_handle_cow_fault(vm_address_space_t *as, uint64_t fault_addr, uint64_t error_code);

/* --- Demand paging / page-table manipulation --- */
/* Map a single 4 KiB page at vaddr -> paddr with the given PAGE_* flags,
 * allocating intermediate PD/PT tables as needed. Records the page for
 * fork-copy and teardown. Returns true on success. */
bool vm_map_page(vm_address_space_t *as, uint64_t vaddr, uint64_t paddr, uint64_t flags);

/* Look up the physical mapping for vaddr in the address space; returns the
 * physical base (page-aligned) or 0 if not mapped. */
uint64_t vm_get_mapping(const vm_address_space_t *as, uint64_t vaddr);

/* Handle a page fault (#PF): if fault_addr is an unmapped user page in the
 * demand region, allocate a frame and map it. Returns true when the fault
 * has been resolved (and the faulting instruction should be retried). */
bool vm_handle_page_fault(uint64_t fault_addr, uint64_t error_code);

#endif /* _VM_H_ */
