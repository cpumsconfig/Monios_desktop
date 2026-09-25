#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "mmu.h"
#include "memory.h"

/* ── DMA memory pool ───────────────────────────────────────────────
 * A static, physically contiguous, page-aligned region reserved for
 * device DMA.  The kernel runs on identity mappings, so the virtual
 * address of each byte equals its physical address, which is what
 * PCI/USB/NIC controllers expect.  The pool lives in low memory (<4GB)
 * so 32-bit DMA masters can reach it.
 * ─────────────────────────────────────────────────────────────────── */
#define DMA_POOL_SIZE       (2u * 1024u * 1024u)   /* 2 MiB lowmem pool */
#define DMA_MAX_ALLOCS      32u
#define DMA_DESC_POOL_SIZE  (16u * 1024u)          /* 16 KiB descriptor ring pool */

static uint8_t g_dma_pool[DMA_POOL_SIZE] __attribute__((aligned(4096)));
static uint8_t g_dma_desc_pool[DMA_DESC_POOL_SIZE] __attribute__((aligned(4096)));
static uint64_t g_dma_offset;
static uint32_t g_dma_alloc_n;
static uint32_t g_dma_desc_offset;
static char g_dma_status[64];

/* Allocation record table (for dma_free tracking). */
typedef struct {
    uint64_t phys;
    uint64_t size;
    bool     in_use;
} dma_alloc_rec_t;
static dma_alloc_rec_t g_allocs[DMA_MAX_ALLOCS];

static uint64_t dma_align_up(uint64_t value, uint64_t align)
{
    if (align == 0) {
        return value;
    }
    return (value + align - 1) & ~(align - 1);
}

void dma_init(void)
{
    g_dma_offset = 0;
    g_dma_alloc_n = 0;
    g_dma_desc_offset = 0;
    memset(g_allocs, 0, sizeof(g_allocs));
    strcpy(g_dma_status, "dma: pool ready");
}

bool dma_alloc(uint64_t size, uint64_t align, uint64_t max_physical_address, dma_buffer_t *out_buffer)
{
    uint64_t pool_base;
    uint64_t aligned_address;
    uint64_t start;
    uint64_t end;
    uint64_t phys;

    if (out_buffer == NULL || size == 0) {
        return false;
    }

    pool_base = (uint64_t) (uintptr_t) g_dma_pool;
    aligned_address = dma_align_up(pool_base + g_dma_offset, align == 0 ? 16 : align);
    start = aligned_address - pool_base;
    end = start + size;
    if (end > DMA_POOL_SIZE) {
        return false;
    }

    phys = (uint64_t) (uintptr_t) &g_dma_pool[start];
    if (max_physical_address != 0 && phys + size - 1 > max_physical_address) {
        return false;
    }

    out_buffer->virtual_address = &g_dma_pool[start];
    out_buffer->physical_address = phys;
    out_buffer->size = size;
    g_dma_offset = end;

    /* Record the allocation for dma_free() bookkeeping. */
    for (uint32_t i = 0; i < DMA_MAX_ALLOCS; i++) {
        if (!g_allocs[i].in_use) {
            g_allocs[i].phys = phys;
            g_allocs[i].size = size;
            g_allocs[i].in_use = true;
            g_dma_alloc_n++;
            break;
        }
    }

    memset(out_buffer->virtual_address, 0, (uint64_t) size);
    return true;
}

bool dma_free(uint64_t phys, uint64_t size)
{
    bool freed = false;

    for (uint32_t i = 0; i < DMA_MAX_ALLOCS; i++) {
        if (g_allocs[i].in_use && g_allocs[i].phys == phys &&
            g_allocs[i].size == size) {
            g_allocs[i].in_use = false;
            g_dma_alloc_n--;
            freed = true;
            /* If this was the most recent allocation, rewind the bump
             * pointer so the memory can be reused. */
            if (phys + size == (uint64_t) (uintptr_t) g_dma_pool + g_dma_offset) {
                g_dma_offset = phys - (uint64_t) (uintptr_t) g_dma_pool;
            }
            break;
        }
    }
    return freed;
}

uint64_t dma_checkpoint(void)
{
    return g_dma_offset;
}

void dma_rewind(uint64_t checkpoint)
{
    if (checkpoint <= g_dma_offset) {
        g_dma_offset = checkpoint;
        /* Invalidate allocation records beyond the checkpoint. */
        uint64_t pool_base = (uint64_t) (uintptr_t) g_dma_pool;
        for (uint32_t i = 0; i < DMA_MAX_ALLOCS; i++) {
            if (g_allocs[i].in_use &&
                g_allocs[i].phys >= pool_base + checkpoint) {
                g_allocs[i].in_use = false;
                g_dma_alloc_n--;
            }
        }
    }
}

void dma_log_state(void)
{
    char line[48] = "dma: pool used=";
    char digits[21];
    uint32_t i = 0;
    uint64_t value = g_dma_offset;

    if (value == 0) {
        digits[i++] = '0';
    } else {
        char temp[21];
        while (value > 0) {
            temp[i++] = (char) ('0' + (value % 10));
            value /= 10;
        }
        for (uint32_t j = 0; j < i / 2; j++) {
            char swap = temp[j];
            temp[j] = temp[i - 1 - j];
            temp[i - 1 - j] = swap;
        }
        memcpy(digits, temp, i);
    }
    digits[i] = '\0';
    strcpy(line + 15, digits);
    log_write(line);
}

/* =========================================================================
 * New unified DMA subsystem interface.
 * ========================================================================= */

bool dma_probe(void)
{
    uint64_t pool_base = (uint64_t) (uintptr_t) g_dma_pool;

    dma_init();
    if (pool_base == 0u || pool_base >= 0x100000000ULL) {
        strcpy(g_dma_status, "dma: not found");
        log_write("dma: not found");
        return false;
    }
    /* Ensure the pool is identity-mapped for devices. */
    mmu_map_identity(pool_base, DMA_POOL_SIZE);
    mmu_map_identity((uint64_t) (uintptr_t) g_dma_desc_pool, DMA_DESC_POOL_SIZE);
    strcpy(g_dma_status, "dma: pool ready (<4GB lowmem)");
    log_write(g_dma_status);
    return true;
}

void dma_shutdown(void)
{
    g_dma_offset = 0;
    g_dma_desc_offset = 0;
    g_dma_alloc_n = 0;
    memset(g_allocs, 0, sizeof(g_allocs));
    strcpy(g_dma_status, "dma: shutdown");
}

/* Identity-mapped pool: virtual == physical. */
uint64_t dma_virt_to_phys(const void *virt)
{
    return (uint64_t) (uintptr_t) virt;
}

void *dma_phys_to_virt(uint64_t phys)
{
    return (void *) (uintptr_t) phys;
}

int32_t dma_read(void *dst, uint64_t phys, uint32_t len)
{
    if (dst == NULL || len == 0u) {
        return -1;
    }
    memcpy(dst, (const void *) (uintptr_t) phys, (uint64_t) len);
    return (int32_t) len;
}

int32_t dma_write(uint64_t phys, const void *src, uint32_t len)
{
    if (src == NULL || len == 0u) {
        return -1;
    }
    memcpy((void *) (uintptr_t) phys, src, (uint64_t) len);
    return (int32_t) len;
}

/* Descriptor pool: small bump allocator for ring descriptors. */
uint64_t dma_desc_alloc(uint32_t count)
{
    uint64_t bytes = (uint64_t) count * 16u;   /* 16-byte descriptors */
    uint64_t aligned = dma_align_up(g_dma_desc_offset, 16u);
    uint64_t phys;

    if (aligned + bytes > DMA_DESC_POOL_SIZE) {
        return 0u;
    }
    phys = (uint64_t) (uintptr_t) &g_dma_desc_pool[aligned];
    g_dma_desc_offset = aligned + bytes;
    memset((void *) (uintptr_t) phys, 0, bytes);
    return phys;
}

void dma_desc_free(uint64_t phys)
{
    (void) phys;
    /* Descriptor rings are typically lifetime-managed with the device;
     * individual descriptor frees are coalesced at shutdown. */
}

/* Cache-coherence framework (no explicit cache management on this
 * identity-mapped, write-through kernel; hooks for future use). */
void dma_cache_flush(uint64_t phys, uint32_t len)
{
    (void) phys;
    (void) len;
}

void dma_cache_invalidate(uint64_t phys, uint32_t len)
{
    (void) phys;
    (void) len;
}

uint64_t dma_total_bytes(void)
{
    return DMA_POOL_SIZE;
}

uint64_t dma_used_bytes(void)
{
    return g_dma_offset;
}

uint32_t dma_alloc_count(void)
{
    return g_dma_alloc_n;
}

const char *dma_status(void)
{
    return g_dma_status;
}
