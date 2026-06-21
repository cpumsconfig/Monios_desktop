#include "vma.h"
#include "common.h"
#include "memory.h"

#define VMA_MAX_ENTRIES 16U

static vma_entry_t g_vmas[VMA_MAX_ENTRIES];
static uint32_t g_vma_count;
static char g_vma_status[64];

static void vma_copy_name(char *dst, uint32_t size, const char *src)
{
    uint32_t index = 0;

    if (size == 0) {
        return;
    }
    while (src != NULL && src[index] != '\0' && index + 1 < size) {
        dst[index] = src[index];
        index++;
    }
    dst[index] = '\0';
}

void vma_init(void)
{
    memset(g_vmas, 0, sizeof(g_vmas));
    g_vma_count = 0;
    strcpy(g_vma_status, "vma: ready");
    vma_add(0x00000000ULL, 0x00200000ULL, VMA_FLAG_READ | VMA_FLAG_WRITE, "lowmem");
    vma_add(0x04000000ULL, 0x003C0000ULL, VMA_FLAG_READ | VMA_FLAG_EXEC | VMA_FLAG_USER, "exec-image");
    vma_add(0x043C0000ULL, 0x00030000ULL, VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_USER | VMA_FLAG_LAZY, "exec-data");
    vma_add(0x043F0000ULL, 0x00010000ULL, VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_USER, "exec-stack");
    vma_add((uint64_t) memory_heap_base(), memory_heap_size(), VMA_FLAG_READ | VMA_FLAG_WRITE, "kernel-heap");
    vma_add(0xE8000000ULL, 0x18000000ULL, VMA_FLAG_READ | VMA_FLAG_WRITE, "mmio");
}

int32_t vma_add(uint64_t base, uint64_t size, uint32_t flags, const char *name)
{
    vma_entry_t *entry;

    if (size == 0 || g_vma_count >= VMA_MAX_ENTRIES) {
        strcpy(g_vma_status, "vma: table full");
        return -1;
    }
    entry = &g_vmas[g_vma_count];
    memset(entry, 0, sizeof(*entry));
    entry->used = true;
    entry->id = g_vma_count;
    entry->base = base;
    entry->size = size;
    entry->flags = flags;
    vma_copy_name(entry->name, sizeof(entry->name), name);
    g_vma_count++;
    strcpy(g_vma_status, "vma: region added");
    return (int32_t) entry->id;
}

const vma_entry_t *vma_find(uint64_t address)
{
    for (uint32_t index = 0; index < g_vma_count; index++) {
        const vma_entry_t *entry = &g_vmas[index];

        if (entry->used && address >= entry->base && address < entry->base + entry->size) {
            return entry;
        }
    }
    return NULL;
}

uint32_t vma_count(void)
{
    return g_vma_count;
}

bool vma_snapshot(uint32_t index, vma_entry_t *out)
{
    if (index >= g_vma_count || out == NULL) {
        return false;
    }
    *out = g_vmas[index];
    return true;
}

const char *vma_status(void)
{
    return g_vma_status;
}
