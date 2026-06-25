#include "common.h"
#include "dma.h"
#include "kernel.h"
#include "memory.h"

#define DMA_POOL_SIZE (2 * 1024 * 1024)

static uint8_t g_dma_pool[DMA_POOL_SIZE] __attribute__((aligned(4096)));
static uint64_t g_dma_offset;

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
}

bool dma_alloc(uint64_t size, uint64_t align, uint64_t max_physical_address, dma_buffer_t *out_buffer)
{
    uint64_t start;
    uint64_t end;
    uint64_t phys;

    if (out_buffer == NULL || size == 0) {
        return false;
    }

    start = dma_align_up(g_dma_offset, align == 0 ? 16 : align);
    end = start + size;
    if (end > DMA_POOL_SIZE) {
        return false;
    }

    phys = (uint64_t) &g_dma_pool[start];
    if (max_physical_address != 0 && phys + size - 1 > max_physical_address) {
        return false;
    }

    out_buffer->virtual_address = &g_dma_pool[start];
    out_buffer->physical_address = phys;
    out_buffer->size = size;
    g_dma_offset = end;
    memset(out_buffer->virtual_address, 0, (uint32_t) size);
    return true;
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
