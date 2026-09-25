#include "common.h"
#include "vm.h"
#include "mmu.h"
#include "frame.h"
#include "pcb.h"
#include "string.h"

/*
 * Per-process virtual address space isolation.
 *
 * Page-table layout (4-level, 2 MiB large pages for the user window):
 *
 *   Process PML4[0]   -> process PDPT
 *   Process PDPT[0]   -> process PD0   (0..1 GiB virtual, user window here)
 *   Process PDPT[1..] -> kernel PDs    (shared identity mappings)
 *   Process PD0[0..31]   -> identity 0..64 MiB   (kernel, no USER flag)
 *   Process PD0[32..33]  -> process private 4 MiB user region (USER flag)
 *   Process PD0[34..511] -> identity 68 MiB..1 GiB (kernel)
 *
 * The kernel identity-maps all physical memory, so a process's user
 * pages are always reachable from kernel mode via their physical address.
 */

#define PAGE_PRESENT   0x001ULL
#define PAGE_WRITABLE  0x002ULL
#define PAGE_USER      0x004ULL
#define PAGE_LARGE_2M  0x080ULL

/* Physical region reserved for per-process user pages:
 *   128 MiB .. 384 MiB  (256 MiB total = 64 * 4 MiB). */
#define VM_USER_PHYS_BASE   0x08000000ULL
#define VM_USER_PHYS_SIZE   0x10000000ULL   /* 256 MiB */
#define VM_USER_CHUNK_SIZE  VM_USER_SIZE    /* 4 MiB per process */
#define VM_USER_CHUNK_COUNT (VM_USER_PHYS_SIZE / VM_USER_CHUNK_SIZE)

static uint8_t  g_user_chunk_bitmap[VM_USER_CHUNK_COUNT];
static bool     g_vm_initialised;
static uint64_t g_kernel_pml4_phys;

static int vm_alloc_user_chunk(void)
{
    for (uint32_t i = 0; i < VM_USER_CHUNK_COUNT; i++) {
        if (g_user_chunk_bitmap[i] == 0) {
            g_user_chunk_bitmap[i] = 1;
            return (int) i;
        }
    }
    return -1;
}

static void vm_free_user_chunk(int chunk)
{
    if (chunk >= 0 && (uint32_t) chunk < VM_USER_CHUNK_COUNT) {
        g_user_chunk_bitmap[chunk] = 0;
    }
}

void vm_init(void)
{
    memset(g_user_chunk_bitmap, 0, sizeof(g_user_chunk_bitmap));
    g_kernel_pml4_phys = mmu_get_pml4_phys();
    g_vm_initialised = true;
}

bool vm_create_address_space(vm_address_space_t *out)
{
    uint64_t pml4_phys;
    uint64_t pdpt_phys;
    uint64_t pd0_phys;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd0;
    int      chunk;
    uint64_t user_phys;

    if (out == NULL || !g_vm_initialised) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    /* Allocate three 4 KiB pages for PML4, PDPT, PD0. */
    pml4_phys = frame_alloc(1);
    pdpt_phys = frame_alloc(1);
    pd0_phys  = frame_alloc(1);
    if (pml4_phys == 0 || pdpt_phys == 0 || pd0_phys == 0) {
        if (pml4_phys) frame_free(pml4_phys, 1);
        if (pdpt_phys) frame_free(pdpt_phys, 1);
        if (pd0_phys)  frame_free(pd0_phys, 1);
        return false;
    }

    /* Allocate a 4 MiB physical chunk for user memory. */
    chunk = vm_alloc_user_chunk();
    if (chunk < 0) {
        frame_free(pml4_phys, 1);
        frame_free(pdpt_phys, 1);
        frame_free(pd0_phys, 1);
        return false;
    }
    user_phys = VM_USER_PHYS_BASE + (uint64_t) chunk * VM_USER_CHUNK_SIZE;

    /* Identity-mapped: phys == virt for these low addresses. */
    pml4 = (uint64_t *) (uintptr_t) pml4_phys;
    pdpt = (uint64_t *) (uintptr_t) pdpt_phys;
    pd0  = (uint64_t *) (uintptr_t) pd0_phys;

    memset(pml4, 0, 4096);
    memset(pdpt, 0, 4096);
    memset(pd0, 0, 4096);

    /* Clone shared kernel PDPT entries (PDPT[1..] point at kernel PDs). */
    mmu_clone_pdpt(pdpt);
    /* Clone shared kernel PD0 entries (everything except the user window). */
    mmu_clone_pd0(pd0);

    /* Wire up the process-private pieces. */
    pdpt[0] = pd0_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
    pml4[0] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    /* Map the 4 MiB user window at virtual 0x04000000 using two 2 MiB
     * large pages backed by the process's private physical chunk. */
    pd0[32] = user_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_LARGE_2M;
    pd0[33] = (user_phys + 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_LARGE_2M;

    /* Zero the user region so the new process starts clean. */
    memset((void *) (uintptr_t) user_phys, 0, VM_USER_SIZE);

    out->pml4_phys      = pml4_phys;
    out->user_phys_base = user_phys;
    out->active         = true;
    return true;
}

void vm_destroy_address_space(vm_address_space_t *as)
{
    int chunk;
    uint32_t i;

    if (as == NULL || !as->active) {
        return;
    }

    /* Free every demand-allocated 4 KiB user page.  Pages still marked
     * COW are shared with another process and must not be freed here. */
    for (i = 0; i < as->demand_count && i < VM_DEMAND_MAX_PAGES; i++) {
        uint64_t vaddr = as->demand_pages[i];
        uint64_t *pml4, *pdpt, *pd, *pt;
        uint64_t pml4e, pdpte, pde, pte;
        uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;

        pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;
        pml4_idx = (vaddr >> 39) & 0x1FFULL;
        pml4e = pml4[pml4_idx];
        if ((pml4e & PAGE_PRESENT) == 0) continue;
        pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);
        pdpt_idx = (vaddr >> 30) & 0x1FFULL;
        pdpte = pdpt[pdpt_idx];
        if ((pdpte & PAGE_PRESENT) == 0) continue;
        pd = (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);
        pd_idx = (vaddr >> 21) & 0x1FFULL;
        pde = pd[pd_idx];
        if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_LARGE_2M)) continue;
        pt = (uint64_t *) (uintptr_t) (pde & ~0xFFFULL);
        pt_idx = (vaddr >> 12) & 0x1FFULL;
        pte = pt[pt_idx];
        if ((pte & PAGE_PRESENT) == 0) continue;
        if ((pte & PAGE_COW_BIT) == 0) {
            frame_free(pte & ~0xFFFULL, 1);
        }
    }
    as->demand_count = 0;

    /* Free the PD/PT pages we allocated on demand. */
    for (i = 0; i < as->pt_count && i < VM_DEMAND_MAX_TABLES; i++) {
        if (as->pt_pages[i] != 0) {
            frame_free(as->pt_pages[i], 1);
        }
    }
    as->pt_count = 0;

    if (as->cow_shared) {
        /* The 4 MiB user chunk is shared with the fork parent.  Do not free
         * it.  But any 2 MiB large page split into a PT by a COW fault holds
         * private frames that we must release. */
        uint64_t *pd0 = NULL;
        uint64_t *pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;
        uint64_t pml4e = pml4[0];
        if (pml4e & PAGE_PRESENT) {
            uint64_t *pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);
            uint64_t pdpte = pdpt[0];
            if (pdpte & PAGE_PRESENT) {
                pd0 = (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);
            }
        }
        if (pd0 != NULL) {
            for (i = 32; i <= 33; i++) {
                uint64_t pde = pd0[i];
                if ((pde & PAGE_PRESENT) && !(pde & PAGE_LARGE_2M)) {
                    uint64_t *pt = (uint64_t *) (uintptr_t) (pde & ~0xFFFULL);
                    for (uint32_t j = 0; j < 512; j++) {
                        if (pt[j] & PAGE_PRESENT) {
                            frame_free(pt[j] & ~0xFFFULL, 1);
                        }
                    }
                    frame_free(pde & ~0xFFFULL, 1);
                }
            }
        }
    } else {
        /* Free the privately-owned 4 MiB user chunk. */
        if (as->user_phys_base >= VM_USER_PHYS_BASE &&
            as->user_phys_base < VM_USER_PHYS_BASE + VM_USER_PHYS_SIZE) {
            chunk = (int) ((as->user_phys_base - VM_USER_PHYS_BASE) / VM_USER_CHUNK_SIZE);
            vm_free_user_chunk(chunk);
        }
    }

    /* We cannot easily free the PML4/PDPT/PD0 pages because CR3 might still
     * point at them.  The caller must have switched away first.  Leak them
     * deliberately — with at most 64 processes the loss is bounded. */
    as->active = false;
    as->cow_shared = false;
    as->pml4_phys = 0;
    as->user_phys_base = 0;
}

void vm_switch_to(const vm_address_space_t *as)
{
    uint64_t target;

    if (as != NULL && as->active) {
        target = as->pml4_phys;
    } else {
        target = g_kernel_pml4_phys;
    }
    asm volatile (
        "mov %0, %%cr3\n\t"
        "lfence\n\t"
        :
        : "r" (target)
        : "memory"
    );
}

uint64_t vm_current_pml4(void)
{
    uint64_t cur;
    asm volatile ("mov %%cr3, %0" : "=r" (cur));
    return cur;
}

void vm_zero_user_region(const vm_address_space_t *as)
{
    if (as == NULL || !as->active) {
        return;
    }
    memset((void *) (uintptr_t) as->user_phys_base, 0, VM_USER_SIZE);
}

bool vm_copy_user_region(vm_address_space_t *dst, const vm_address_space_t *src)
{
    uint32_t i;

    if (dst == NULL || src == NULL || !dst->active || !src->active) {
        return false;
    }
    memcpy((void *) (uintptr_t) dst->user_phys_base,
           (const void *) (uintptr_t) src->user_phys_base,
           VM_USER_SIZE);

    /* Replicate every demand-allocated page: allocate a fresh frame in the
     * child and copy the parent's physical contents into it. */
    for (i = 0; i < src->demand_count && i < VM_DEMAND_MAX_PAGES; i++) {
        uint64_t vpage = src->demand_pages[i];
        uint64_t src_pa = vm_get_mapping(src, vpage);
        uint64_t dst_pa;

        if (src_pa == 0) {
            continue;
        }
        dst_pa = frame_alloc(1);
        if (dst_pa == 0) {
            break;
        }
        memcpy((void *) (uintptr_t) dst_pa,
               (const void *) (uintptr_t) src_pa,
               4096U);
        if (!vm_map_page(dst, vpage, dst_pa,
                         PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
            frame_free(dst_pa, 1);
            break;
        }
    }
    return true;
}

/* ====================================================================== */
/* Demand paging                                                          */
/* All page-table memory handed out by frame_alloc lives in the low       */
/* physical window (0x01000000..) which is identity-mapped in every       */
/* address space, so we can dereference physical page-table pointers.     */
/* ====================================================================== */

static int vm_track_page_table(vm_address_space_t *as, uint64_t pt_phys)
{
    if (as->pt_count >= VM_DEMAND_MAX_TABLES) {
        return -1;
    }
    as->pt_pages[as->pt_count++] = pt_phys;
    return 0;
}

bool vm_map_page(vm_address_space_t *as, uint64_t vaddr, uint64_t paddr, uint64_t flags)
{
    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t pml4e, pdpte, pde;
    uint64_t pd_phys, pt_phys;
    uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;

    if (as == NULL || !as->active) {
        return false;
    }
    vaddr &= ~0xFFFULL;
    paddr &= ~0xFFFULL;

    pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;

    pml4_idx = (vaddr >> 39) & 0x1FFULL;
    pml4e = pml4[pml4_idx];
    if ((pml4e & PAGE_PRESENT) == 0) {
        return false; /* process PML4 must already exist (PML4[0]) */
    }
    pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);

    pdpt_idx = (vaddr >> 30) & 0x1FFULL;
    pdpte = pdpt[pdpt_idx];
    if ((pdpte & PAGE_PRESENT) == 0) {
        uint64_t new_pd = frame_alloc(1);
        if (new_pd == 0) {
            return false;
        }
        memset((void *) (uintptr_t) new_pd, 0, 4096U);
        pdpt[pdpt_idx] = new_pd | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        if (vm_track_page_table(as, new_pd) != 0) {
            /* leave the table mapped but untracked; bounded by table cap */
        }
        pdpte = pdpt[pdpt_idx];
    }
    pd_phys = pdpte & ~0xFFFULL;
    pd = (uint64_t *) (uintptr_t) pd_phys;

    pd_idx = (vaddr >> 21) & 0x1FFULL;
    pde = pd[pd_idx];
    if ((pde & PAGE_PRESENT) == 0) {
        uint64_t new_pt = frame_alloc(1);
        if (new_pt == 0) {
            return false;
        }
        memset((void *) (uintptr_t) new_pt, 0, 4096U);
        pd[pd_idx] = new_pt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        (void) vm_track_page_table(as, new_pt);
        pde = pd[pd_idx];
    } else if (pde & 0x80ULL) {
        /* Already a 2 MiB large mapping (e.g. the pre-mapped window); do not
         * clobber it. */
        return false;
    }
    pt_phys = pde & ~0xFFFULL;
    pt = (uint64_t *) (uintptr_t) pt_phys;

    pt_idx = (vaddr >> 12) & 0x1FFULL;
    pt[pt_idx] = paddr | flags;

    if (as->demand_count < VM_DEMAND_MAX_PAGES) {
        as->demand_pages[as->demand_count++] = vaddr;
    }

    asm volatile ("invlpg (%0)" : : "r" ((void *) (uintptr_t) vaddr) : "memory");
    return true;
}

uint64_t vm_get_mapping(const vm_address_space_t *as, uint64_t vaddr)
{
    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t pml4e, pdpte, pde, pte;
    uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;

    if (as == NULL || !as->active) {
        return 0;
    }
    vaddr &= ~0xFFFULL;

    pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;
    pml4_idx = (vaddr >> 39) & 0x1FFULL;
    pml4e = pml4[pml4_idx];
    if ((pml4e & PAGE_PRESENT) == 0) return 0;
    pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);

    pdpt_idx = (vaddr >> 30) & 0x1FFULL;
    pdpte = pdpt[pdpt_idx];
    if ((pdpte & PAGE_PRESENT) == 0) return 0;
    pd = (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);

    pd_idx = (vaddr >> 21) & 0x1FFULL;
    pde = pd[pd_idx];
    if ((pde & PAGE_PRESENT) == 0) return 0;
    if (pde & 0x80ULL) {
        return (pde & ~0x1FFFFFULL) | (vaddr & 0x1FFFFFULL);
    }
    pt = (uint64_t *) (uintptr_t) (pde & ~0xFFFULL);

    pt_idx = (vaddr >> 12) & 0x1FFULL;
    pte = pt[pt_idx];
    if ((pte & PAGE_PRESENT) == 0) return 0;
    return pte & ~0xFFFULL;
}

bool vm_handle_page_fault(uint64_t fault_addr, uint64_t error_code)
{
    pcb_t *cur;
    uint64_t frame;
    uint64_t vpage;

    /* bit2 = 1: fault originated in user mode. */
    if ((error_code & 0x4ULL) == 0) {
        return false;
    }

    cur = pcb_get_current();
    if (cur == NULL || !cur->addr_space.active) {
        return false;
    }

    /* bit0 = 1: page is present but permission violation.  This could be a
     * write to a COW page.  Try the COW handler first. */
    if (error_code & 0x1ULL) {
        if (vm_handle_cow_fault(&cur->addr_space, fault_addr, error_code)) {
            return true;
        }
        return false;
    }

    /* bit0 = 0: non-present page (demand allocation). */
    /* Only service faults inside the demand region; the fixed 4 MiB window is
     * pre-mapped and kernel addresses are not ours to map. */
    if (fault_addr < VM_DEMAND_BEGIN || fault_addr >= VM_DEMAND_END) {
        return false;
    }

    vpage = fault_addr & ~0xFFFULL;
    frame = frame_alloc(1);
    if (frame == 0) {
        return false;
    }
    memset((void *) (uintptr_t) frame, 0, 4096U);

    if (!vm_map_page(&cur->addr_space, vpage, frame,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER)) {
        frame_free(frame, 1);
        return false;
    }
    return true;
}

/* ====================================================================== */
/* Copy-on-Write (COW) fork support                                      */
/* ====================================================================== */

/* Get a pointer to the PD0 for an address space (identity-mapped). */
static uint64_t *vm_get_pd0(const vm_address_space_t *as)
{
    uint64_t *pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;
    uint64_t pml4e = pml4[0];
    if ((pml4e & PAGE_PRESENT) == 0) return NULL;
    uint64_t *pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);
    uint64_t pdpte = pdpt[0];
    if ((pdpte & PAGE_PRESENT) == 0) return NULL;
    return (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);
}

bool vm_cow_share(vm_address_space_t *parent, vm_address_space_t *child)
{
    uint64_t *parent_pd0;
    uint64_t *child_pd0;
    uint32_t i;

    if (parent == NULL || child == NULL ||
        !parent->active || !child->active) {
        return false;
    }

    parent_pd0 = vm_get_pd0(parent);
    child_pd0 = vm_get_pd0(child);
    if (parent_pd0 == NULL || child_pd0 == NULL) {
        return false;
    }

    /* --- 4 MiB user window (two 2 MiB large pages) --- */

    /* Free the child's privately-allocated 4 MiB chunk; we will share the
     * parent's physical pages instead. */
    if (child->user_phys_base >= VM_USER_PHYS_BASE &&
        child->user_phys_base < VM_USER_PHYS_BASE + VM_USER_PHYS_SIZE) {
        int c = (int) ((child->user_phys_base - VM_USER_PHYS_BASE) / VM_USER_CHUNK_SIZE);
        vm_free_user_chunk(c);
    }
    child->user_phys_base = parent->user_phys_base;

    /* Remap child's two 2 MiB large pages to point at the parent's physical
     * pages, marked read-only + COW. */
    child_pd0[32] = parent_pd0[32] & ~(PAGE_WRITABLE | PAGE_COW_BIT);
    child_pd0[32] |= PAGE_COW_BIT;
    child_pd0[33] = parent_pd0[33] & ~(PAGE_WRITABLE | PAGE_COW_BIT);
    child_pd0[33] |= PAGE_COW_BIT;

    /* Mark parent's pages read-only + COW as well. */
    parent_pd0[32] &= ~PAGE_WRITABLE;
    parent_pd0[32] |= PAGE_COW_BIT;
    parent_pd0[33] &= ~PAGE_WRITABLE;
    parent_pd0[33] |= PAGE_COW_BIT;

    /* --- Demand-paged 4 KiB pages --- */
    for (i = 0; i < parent->demand_count && i < VM_DEMAND_MAX_PAGES; i++) {
        uint64_t vpage = parent->demand_pages[i];
        uint64_t src_pa = vm_get_mapping(parent, vpage);
        uint64_t *pml4, *pdpt, *pd, *pt;
        uint64_t pml4e, pdpte, pde;
        uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;

        if (src_pa == 0) {
            continue;
        }

        /* Map the parent's physical frame into the child, read-only + COW.
         * We bypass vm_map_page (which would add to demand_count and might
         * allocate intermediate tables) by walking the child's tables. */
        pml4 = (uint64_t *) (uintptr_t) child->pml4_phys;
        pml4_idx = (vpage >> 39) & 0x1FFULL;
        pml4e = pml4[pml4_idx];
        if ((pml4e & PAGE_PRESENT) == 0) continue;
        pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);
        pdpt_idx = (vpage >> 30) & 0x1FFULL;
        pdpte = pdpt[pdpt_idx];
        if ((pdpte & PAGE_PRESENT) == 0) {
            uint64_t new_pd = frame_alloc(1);
            if (new_pd == 0) continue;
            memset((void *) (uintptr_t) new_pd, 0, 4096U);
            pdpt[pdpt_idx] = new_pd | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            (void) vm_track_page_table(child, new_pd);
            pdpte = pdpt[pdpt_idx];
        }
        pd = (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);
        pd_idx = (vpage >> 21) & 0x1FFULL;
        pde = pd[pd_idx];
        if ((pde & PAGE_PRESENT) == 0) {
            uint64_t new_pt = frame_alloc(1);
            if (new_pt == 0) continue;
            memset((void *) (uintptr_t) new_pt, 0, 4096U);
            pd[pd_idx] = new_pt | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
            (void) vm_track_page_table(child, new_pt);
            pde = pd[pd_idx];
        } else if (pde & PAGE_LARGE_2M) {
            continue;
        }
        pt = (uint64_t *) (uintptr_t) (pde & ~0xFFFULL);
        pt_idx = (vpage >> 12) & 0x1FFULL;
        pt[pt_idx] = src_pa | PAGE_PRESENT | PAGE_USER | PAGE_COW_BIT;

        /* Track this vaddr in the child so destruction can walk it. */
        if (child->demand_count < VM_DEMAND_MAX_PAGES) {
            child->demand_pages[child->demand_count++] = vpage;
        }

        /* Mark parent's PTE read-only + COW. */
        {
            uint64_t *ppml4 = (uint64_t *) (uintptr_t) parent->pml4_phys;
            uint64_t *ppdpt = (uint64_t *) (uintptr_t) (ppml4[pml4_idx] & ~0xFFFULL);
            uint64_t *ppd = (uint64_t *) (uintptr_t) (ppdpt[pdpt_idx] & ~0xFFFULL);
            uint64_t *ppt = (uint64_t *) (uintptr_t) (ppd[pd_idx] & ~0xFFFULL);
            ppt[pt_idx] &= ~PAGE_WRITABLE;
            ppt[pt_idx] |= PAGE_COW_BIT;
        }
    }

    parent->cow_shared = true;
    child->cow_shared = true;

    /* Invalidate TLBs for the user window in both address spaces. */
    asm volatile ("invlpg (%0)" : : "r" ((void *) (uintptr_t) VM_USER_BASE) : "memory");
    asm volatile ("invlpg (%0)" : : "r" ((void *) (uintptr_t) (VM_USER_BASE + 0x200000ULL)) : "memory");

    return true;
}

bool vm_handle_cow_fault(vm_address_space_t *as, uint64_t fault_addr, uint64_t error_code)
{
    uint64_t *pd0;
    uint64_t pd_idx;

    if (as == NULL || !as->active) {
        return false;
    }
    /* COW faults are write faults (bit 1) on present pages (bit 0). */
    if ((error_code & 0x1ULL) == 0 || (error_code & 0x2ULL) == 0) {
        return false;
    }

    pd0 = vm_get_pd0(as);
    if (pd0 == NULL) {
        return false;
    }

    /* --- Case 1: fault in the 4 MiB user window (2 MiB large pages) --- */
    if (fault_addr >= VM_USER_BASE && fault_addr < VM_USER_LIMIT) {
        uint64_t page_2m_base;
        uint64_t src_phys;
        uint64_t new_pt_phys;
        uint64_t *new_pt;
        uint64_t pde;

        pd_idx = (fault_addr >> 21) & 0x1FFULL; /* 32 or 33 */
        if (pd_idx != 32 && pd_idx != 33) {
            return false;
        }
        pde = pd0[pd_idx];
        /* Must be a large page with COW bit set. */
        if (!(pde & PAGE_PRESENT) || !(pde & PAGE_LARGE_2M) || !(pde & PAGE_COW_BIT)) {
            return false;
        }

        page_2m_base = fault_addr & ~0x1FFFFFULL;
        src_phys = pde & ~0x1FFFFFULL;

        /* Allocate a new page table for the 512 4 KiB PTEs. */
        new_pt_phys = frame_alloc(1);
        if (new_pt_phys == 0) {
            return false;
        }
        new_pt = (uint64_t *) (uintptr_t) new_pt_phys;
        memset(new_pt, 0, 4096U);

        /* Allocate and copy 512 frames (2 MiB), mapping them writable. */
        for (uint32_t j = 0; j < 512; j++) {
            uint64_t new_frame = frame_alloc(1);
            if (new_frame == 0) {
                /* Rollback: free already-allocated frames and PT. */
                for (uint32_t k = 0; k < j; k++) {
                    frame_free(new_pt[k] & ~0xFFFULL, 1);
                }
                frame_free(new_pt_phys, 1);
                return false;
            }
            memcpy((void *) (uintptr_t) new_frame,
                   (const void *) (uintptr_t) (src_phys + (uint64_t) j * 4096U),
                   4096U);
            new_pt[j] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }

        /* Replace the large-page PDE with a pointer to the new PT. */
        pd0[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

        /* Track the PT for later cleanup. */
        (void) vm_track_page_table(as, new_pt_phys);

        /* Invalidate TLB for the entire 2 MiB region. */
        for (uint64_t a = page_2m_base; a < page_2m_base + 0x200000ULL; a += 4096U) {
            asm volatile ("invlpg (%0)" : : "r" ((void *) (uintptr_t) a) : "memory");
        }
        return true;
    }

    /* --- Case 2: fault in the demand-paged region (4 KiB pages) --- */
    if (fault_addr >= VM_DEMAND_BEGIN && fault_addr < VM_DEMAND_END) {
        uint64_t vpage = fault_addr & ~0xFFFULL;
        uint64_t *pml4, *pdpt, *pd, *pt;
        uint64_t pml4e, pdpte, pde, pte;
        uint64_t pml4_idx, pdpt_idx, pd_idx, pt_idx;
        uint64_t src_pa;
        uint64_t new_frame;

        pml4 = (uint64_t *) (uintptr_t) as->pml4_phys;
        pml4_idx = (vpage >> 39) & 0x1FFULL;
        pml4e = pml4[pml4_idx];
        if ((pml4e & PAGE_PRESENT) == 0) return false;
        pdpt = (uint64_t *) (uintptr_t) (pml4e & ~0xFFFULL);
        pdpt_idx = (vpage >> 30) & 0x1FFULL;
        pdpte = pdpt[pdpt_idx];
        if ((pdpte & PAGE_PRESENT) == 0) return false;
        pd = (uint64_t *) (uintptr_t) (pdpte & ~0xFFFULL);
        pd_idx = (vpage >> 21) & 0x1FFULL;
        pde = pd[pd_idx];
        if ((pde & PAGE_PRESENT) == 0 || (pde & PAGE_LARGE_2M)) return false;
        pt = (uint64_t *) (uintptr_t) (pde & ~0xFFFULL);
        pt_idx = (vpage >> 12) & 0x1FFULL;
        pte = pt[pt_idx];
        if ((pte & PAGE_PRESENT) == 0 || !(pte & PAGE_COW_BIT)) {
            return false;
        }

        src_pa = pte & ~0xFFFULL;
        new_frame = frame_alloc(1);
        if (new_frame == 0) {
            return false;
        }
        memcpy((void *) (uintptr_t) new_frame,
               (const void *) (uintptr_t) src_pa,
               4096U);

        /* Remap to the private frame, writable, no COW bit. */
        pt[pt_idx] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

        asm volatile ("invlpg (%0)" : : "r" ((void *) (uintptr_t) vpage) : "memory");
        return true;
    }

    return false;
}
