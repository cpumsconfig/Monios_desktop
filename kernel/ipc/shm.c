#include "shm.h"
#include "common.h"
#include "frame.h"
#include "memory.h"
#include "pcb.h"
#include "string.h"
#include "vm.h"

/*
 * Shared-memory segment storage.  Physical frames are allocated contiguously
 * once and mapped into every attaching process at SHM_VBASE + slot*2MiB.
 * The PTEs carry the COW bit (0x200) in addition to PRESENT|WRITABLE|USER so
 * that vm_destroy_address_space() leaves the shared frames alone.
 */

#define SHM_MAP_FLAGS  (0x1ULL | 0x2ULL | 0x4ULL | 0x200ULL)

typedef struct {
    bool used;
    bool doomed;
    uint32_t key;
    uint32_t size;        /* rounded up to page multiple */
    uint32_t pages;
    uint64_t phys_base;   /* physical base of the contiguous run */
    uint32_t attach_count;
} shm_entry_t;

static shm_entry_t g_shm[SHM_MAX_SEGS];

void shm_init(void)
{
    uint32_t i;

    for (i = 0; i < SHM_MAX_SEGS; i++) {
        g_shm[i].used = false;
        g_shm[i].doomed = false;
        g_shm[i].key = 0;
        g_shm[i].size = 0;
        g_shm[i].pages = 0;
        g_shm[i].phys_base = 0;
        g_shm[i].attach_count = 0;
    }
}

static shm_entry_t *shm_find_by_key(uint32_t key)
{
    uint32_t i;

    for (i = 0; i < SHM_MAX_SEGS; i++) {
        if (g_shm[i].used && g_shm[i].key == key && !g_shm[i].doomed) {
            return &g_shm[i];
        }
    }
    return NULL;
}

static shm_entry_t *shm_by_id(int32_t shmid)
{
    int32_t slot = shmid - 1;

    if (slot < 0 || slot >= (int32_t) SHM_MAX_SEGS) {
        return NULL;
    }
    if (!g_shm[slot].used) {
        return NULL;
    }
    return &g_shm[slot];
}

int32_t shm_create(uint32_t key, uint32_t size)
{
    shm_entry_t *existing;
    shm_entry_t *seg = NULL;
    uint32_t i;
    uint32_t pages;

    existing = shm_find_by_key(key);
    if (existing != NULL) {
        for (i = 0; i < SHM_MAX_SEGS; i++) {
            if (&g_shm[i] == existing) {
                return (int32_t)(i + 1U);
            }
        }
    }
    if (size == 0 || size > SHM_MAX_SIZE) {
        return -1;
    }
    pages = (size + SHM_PAGE_SIZE - 1U) / SHM_PAGE_SIZE;

    for (i = 0; i < SHM_MAX_SEGS; i++) {
        if (!g_shm[i].used) {
            seg = &g_shm[i];
            break;
        }
    }
    if (seg == NULL) {
        return -1;
    }

    seg->phys_base = frame_alloc_aligned(pages, 1);
    if (seg->phys_base == 0) {
        return -1;
    }
    memset((void *)(uintptr_t) seg->phys_base, 0, pages * SHM_PAGE_SIZE);

    seg->used = true;
    seg->doomed = false;
    seg->key = key;
    seg->size = pages * SHM_PAGE_SIZE;
    seg->pages = pages;
    seg->attach_count = 0;
    return (int32_t)(i + 1U);
}

void *shm_attach(int32_t shmid)
{
    shm_entry_t *seg = shm_by_id(shmid);
    pcb_t *cur = pcb_get_current();
    uint64_t vbase;
    uint32_t i;

    if (seg == NULL || cur == NULL || !cur->addr_space.active) {
        return NULL;
    }
    vbase = SHM_VBASE + (uint64_t)(shmid - 1) * SHM_MAX_SIZE;
    for (i = 0; i < seg->pages; i++) {
        uint64_t vaddr = vbase + (uint64_t)i * SHM_PAGE_SIZE;
        uint64_t paddr = seg->phys_base + (uint64_t)i * SHM_PAGE_SIZE;

        if (!vm_map_page(&cur->addr_space, vaddr, paddr, SHM_MAP_FLAGS)) {
            return NULL;
        }
    }
    seg->attach_count++;
    return (void *)vbase;
}

int32_t shm_detach(const void *addr)
{
    uint64_t vaddr = (uint64_t)(uintptr_t)addr;
    uint32_t slot;
    shm_entry_t *seg;

    if (vaddr < SHM_VBASE) {
        return -1;
    }
    slot = (uint32_t)((vaddr - SHM_VBASE) / SHM_MAX_SIZE);
    if (slot >= SHM_MAX_SEGS) {
        return -1;
    }
    seg = &g_shm[slot];
    if (!seg->used) {
        return -1;
    }
    if (seg->attach_count > 0) {
        seg->attach_count--;
    }
    if (seg->doomed && seg->attach_count == 0) {
        frame_free(seg->phys_base, seg->pages);
        seg->phys_base = 0;
        seg->used = false;
    }
    return 0;
}

int32_t shm_destroy(int32_t shmid)
{
    shm_entry_t *seg = shm_by_id(shmid);

    if (seg == NULL) {
        return -1;
    }
    seg->doomed = true;
    if (seg->attach_count == 0) {
        frame_free(seg->phys_base, seg->pages);
        seg->phys_base = 0;
        seg->used = false;
    }
    return 0;
}
