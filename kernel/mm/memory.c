#include "common.h"
#include "kernel_layout.h"
#include "memory.h"
#include "leak_track.h"
#include "spinlock.h"

#define KERNEL_HEAP_SIZE (32 * 1024 * 1024)
#define KERNEL_BIOS_HEAP_BASE 0x05000000ULL
#define KMALLOC_ALIGN 16ULL

typedef struct memory_block {
    uint64_t size;
    bool free;
    struct memory_block *next;
} memory_block_t;

static uint8_t *kernel_heap;
static memory_block_t *memory_head;
static uint64_t g_memory_heap_size;
static uint64_t g_memory_used;
static uint64_t g_memory_high_water_used;
static uint32_t g_memory_allocs;
static uint32_t g_memory_frees;

/* Serialises the free-block list against other CPUs and against interrupt
 * context (which may itself call kmalloc/kfree). */
static spinlock_t g_mm_lock = SPINLOCK_INITIALIZER;

static uint64_t align_up(uint64_t value)
{
    if (value > 0xFFFFFFFFFFFFFFFFULL - (KMALLOC_ALIGN - 1U)) {
        return 0;
    }
    return (value + (KMALLOC_ALIGN - 1)) & ~(KMALLOC_ALIGN - 1);
}

static void coalesce_blocks(void)
{
    memory_block_t *block = memory_head;

    while (block != NULL && block->next != NULL) {
        if (block->free && block->next->free) {
            block->size += sizeof(memory_block_t) + block->next->size;
            block->next = block->next->next;
        } else {
            block = block->next;
        }
    }
}

void memory_init(void)
{
    extern uint64_t _kconfig_start;
    volatile uint64_t *kcfg = &_kconfig_start;
    uint64_t configured_base = kcfg[3];
    uint64_t configured_size = kcfg[4];

    /*
     * UEFI supplies a separate heap allocation through .kconfig. BIOS keeps
     * the historical fixed low-memory layout and uses this fallback region.
     */
    if (configured_base == 0 ||
        configured_base > 0xFFFFFFFFFFFFFFFFULL - configured_size ||
        configured_base + configured_size < configured_base ||
        configured_size < sizeof(memory_block_t) + KMALLOC_ALIGN) {
        configured_base = KERNEL_BIOS_HEAP_BASE;
        configured_size = KERNEL_HEAP_SIZE;
    }
    kernel_heap = (uint8_t *) (uintptr_t) configured_base;
    g_memory_heap_size = configured_size;

    memory_head = (memory_block_t *) kernel_heap;
    memory_head->size = g_memory_heap_size - sizeof(memory_block_t);
    memory_head->free = true;
    memory_head->next = NULL;
    g_memory_used = 0;
    g_memory_high_water_used = 0;
    g_memory_allocs = 0;
    g_memory_frees = 0;
}

void *kmalloc(uint64_t size)
{
    memory_block_t *block;
    memory_block_t *next;
    uint64_t split_overhead = sizeof(memory_block_t) + KMALLOC_ALIGN;
    void *ret = NULL;
    uint64_t flags = 0;

    size = align_up(size == 0 ? 1 : size);
    if (size == 0 || size > g_memory_heap_size) {
        return NULL;
    }

    spin_lock_irqsave(&g_mm_lock, &flags);
    block = memory_head;

    while (block != NULL) {
        if (block->free && block->size >= size) {
            if (block->size - size >= split_overhead) {
                next = (memory_block_t *) ((uint8_t *) block + sizeof(memory_block_t) + size);
                next->size = block->size - size - sizeof(memory_block_t);
                next->free = true;
                next->next = block->next;
                block->next = next;
                block->size = size;
            }
            block->free = false;
            g_memory_used += block->size;
            if (g_memory_used > g_memory_high_water_used) {
                g_memory_high_water_used = g_memory_used;
            }
            g_memory_allocs++;
            ret = (uint8_t *) block + sizeof(memory_block_t);
            break;
        }
        block = block->next;
    }
    spin_unlock_irqrestore(&g_mm_lock, flags);

    if (ret != NULL) {
        /* __builtin_return_address(0) here is the caller of kmalloc(). */
        leak_track_record(ret, size, __builtin_return_address(0));
    }
    return ret;
}

void kfree(void *ptr)
{
    memory_block_t *block;
    memory_block_t *scan;
    uint64_t heap_start;
    uint64_t heap_end;
    uint64_t pointer_value;
    uint64_t flags = 0;

    if (ptr == NULL) {
        return;
    }

    heap_start = (uint64_t) kernel_heap;
    heap_end = heap_start + g_memory_heap_size;
    pointer_value = (uint64_t) ptr;
    if (kernel_heap == NULL ||
        heap_end < heap_start ||
        pointer_value < heap_start + sizeof(memory_block_t) ||
        pointer_value >= heap_end) {
        return;
    }

    spin_lock_irqsave(&g_mm_lock, &flags);

    block = (memory_block_t *) ((uint8_t *) ptr - sizeof(memory_block_t));
    scan = memory_head;
    while (scan != NULL && scan != block) {
        scan = scan->next;
    }
    if (scan == NULL || block->size > heap_end - pointer_value) {
        spin_unlock_irqrestore(&g_mm_lock, flags);
        return;
    }
    if (block->free) {
        spin_unlock_irqrestore(&g_mm_lock, flags);
        return;
    }

    /* Drop the leak-tracker record before returning the block to the heap. */
    leak_track_unrecord(ptr);

    if (g_memory_used >= block->size) {
        g_memory_used -= block->size;
    } else {
        g_memory_used = 0;
    }
    block->free = true;
    g_memory_frees++;
    coalesce_blocks();

    spin_unlock_irqrestore(&g_mm_lock, flags);
}

uint64_t memory_total_free(void)
{
    uint64_t total = 0;
    memory_block_t *block = memory_head;

    while (block != NULL) {
        if (block->free) {
            total += block->size;
        }
        block = block->next;
    }
    return total;
}

uint64_t memory_total_used(void)
{
    return g_memory_used;
}

uint64_t memory_heap_size(void)
{
    return g_memory_heap_size;
}

uint64_t memory_heap_base(void)
{
    return (uint64_t) kernel_heap;
}

uint32_t memory_alloc_count(void)
{
    return g_memory_allocs;
}

uint32_t memory_free_count(void)
{
    return g_memory_frees;
}

uint64_t memory_high_water_used(void)
{
    return g_memory_high_water_used;
}
