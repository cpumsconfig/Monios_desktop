#include "bsod.h"
#include "common.h"
#include "memory.h"

#define KERNEL_HEAP_SIZE (1024 * 1024)
#define KMALLOC_ALIGN 16ULL

typedef struct memory_block {
    uint64_t size;
    bool free;
    struct memory_block *next;
} memory_block_t;

static uint8_t kernel_heap[KERNEL_HEAP_SIZE] __attribute__((aligned(KMALLOC_ALIGN)));
static memory_block_t *memory_head;

static uint64_t align_up(uint64_t value)
{
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
    memory_head = (memory_block_t *) kernel_heap;
    memory_head->size = KERNEL_HEAP_SIZE - sizeof(memory_block_t);
    memory_head->free = true;
    memory_head->next = NULL;
}

void *kmalloc(uint64_t size)
{
    memory_block_t *block;
    memory_block_t *next;

    size = align_up(size == 0 ? 1 : size);
    block = memory_head;

    while (block != NULL) {
        if (block->free && block->size >= size) {
            if (block->size >= size + sizeof(memory_block_t) + KMALLOC_ALIGN) {
                next = (memory_block_t *) ((uint8_t *) block + sizeof(memory_block_t) + size);
                next->size = block->size - size - sizeof(memory_block_t);
                next->free = true;
                next->next = block->next;
                block->next = next;
                block->size = size;
            }
            block->free = false;
            return (uint8_t *) block + sizeof(memory_block_t);
        }
        block = block->next;
    }

    bsod_out_of_memory(size);
    return NULL;
}

void kfree(void *ptr)
{
    memory_block_t *block;

    if (ptr == NULL) {
        return;
    }

    block = (memory_block_t *) ((uint8_t *) ptr - sizeof(memory_block_t));
    block->free = true;
    coalesce_blocks();
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
    uint64_t total = 0;
    memory_block_t *block = memory_head;

    while (block != NULL) {
        if (!block->free) {
            total += block->size;
        }
        block = block->next;
    }
    return total;
}

uint64_t memory_heap_size(void)
{
    return KERNEL_HEAP_SIZE;
}
