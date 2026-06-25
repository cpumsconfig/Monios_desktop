#include "bitmap.h"
#include "common.h"

static bitmap_stats_t g_bitmap_stats;
static char g_bitmap_status[64];

static uint32_t bitmap_word_index(uint32_t index)
{
    return index / BITMAP_WORD_BITS;
}

static uint32_t bitmap_word_mask(uint32_t index)
{
    return 1u << (index % BITMAP_WORD_BITS);
}

void bitmap_init(void)
{
    memset(&g_bitmap_stats, 0, sizeof(g_bitmap_stats));
    strcpy(g_bitmap_status, "bitmap: ready");
}

void bitmap_bind(bitmap_t *bitmap, uint32_t *storage, uint32_t bit_count)
{
    if (bitmap == NULL) {
        return;
    }
    bitmap->words = storage;
    bitmap->bit_count = bit_count;
    if (storage != NULL) {
        uint32_t word_count = (bit_count + BITMAP_WORD_BITS - 1u) / BITMAP_WORD_BITS;
        memset(storage, 0, word_count * sizeof(uint32_t));
    }
}

void bitmap_zero(bitmap_t *bitmap)
{
    uint32_t word_count;

    if (bitmap == NULL || bitmap->words == NULL) {
        return;
    }
    word_count = (bitmap->bit_count + BITMAP_WORD_BITS - 1u) / BITMAP_WORD_BITS;
    memset(bitmap->words, 0, word_count * sizeof(uint32_t));
}

bool bitmap_set(bitmap_t *bitmap, uint32_t index)
{
    uint32_t word;
    uint32_t mask;

    if (bitmap == NULL || bitmap->words == NULL || index >= bitmap->bit_count) {
        return false;
    }
    word = bitmap_word_index(index);
    mask = bitmap_word_mask(index);
    g_bitmap_stats.set_ops++;
    if ((bitmap->words[word] & mask) != 0) {
        return false;
    }
    bitmap->words[word] |= mask;
    return true;
}

bool bitmap_clear(bitmap_t *bitmap, uint32_t index)
{
    uint32_t word;
    uint32_t mask;

    if (bitmap == NULL || bitmap->words == NULL || index >= bitmap->bit_count) {
        return false;
    }
    word = bitmap_word_index(index);
    mask = bitmap_word_mask(index);
    g_bitmap_stats.clear_ops++;
    if ((bitmap->words[word] & mask) == 0) {
        return false;
    }
    bitmap->words[word] &= ~mask;
    return true;
}

bool bitmap_test(const bitmap_t *bitmap, uint32_t index)
{
    if (bitmap == NULL || bitmap->words == NULL || index >= bitmap->bit_count) {
        return false;
    }
    g_bitmap_stats.test_ops++;
    return (bitmap->words[bitmap_word_index(index)] & bitmap_word_mask(index)) != 0;
}

int32_t bitmap_find_first_zero(const bitmap_t *bitmap, uint32_t start_index)
{
    if (bitmap == NULL || bitmap->words == NULL || start_index >= bitmap->bit_count) {
        return -1;
    }
    for (uint32_t index = start_index; index < bitmap->bit_count; index++) {
        if (!bitmap_test(bitmap, index)) {
            return (int32_t) index;
        }
    }
    return -1;
}

int32_t bitmap_find_run_zero(const bitmap_t *bitmap, uint32_t start_index, uint32_t run_length, uint32_t align_bits)
{
    uint32_t align = align_bits == 0 ? 1u : align_bits;

    if (bitmap == NULL || bitmap->words == NULL || run_length == 0 || start_index >= bitmap->bit_count) {
        return -1;
    }
    for (uint32_t index = start_index; index + run_length <= bitmap->bit_count; index++) {
        bool free_run = true;

        if ((index % align) != 0) {
            continue;
        }
        for (uint32_t bit = 0; bit < run_length; bit++) {
            if (bitmap_test(bitmap, index + bit)) {
                free_run = false;
                index += bit;
                break;
            }
        }
        if (free_run) {
            return (int32_t) index;
        }
    }
    return -1;
}

int32_t bitmap_allocate_first(bitmap_t *bitmap)
{
    int32_t index = bitmap_find_first_zero(bitmap, 0);

    if (index >= 0 && bitmap_set(bitmap, (uint32_t) index)) {
        g_bitmap_stats.alloc_ops++;
        return index;
    }
    return -1;
}

bool bitmap_release(bitmap_t *bitmap, uint32_t index)
{
    if (!bitmap_clear(bitmap, index)) {
        return false;
    }
    g_bitmap_stats.free_ops++;
    return true;
}

uint32_t bitmap_count_set(const bitmap_t *bitmap)
{
    uint32_t count = 0;

    if (bitmap == NULL || bitmap->words == NULL) {
        return 0;
    }
    for (uint32_t index = 0; index < bitmap->bit_count; index++) {
        if (bitmap_test(bitmap, index)) {
            count++;
        }
    }
    return count;
}

const bitmap_stats_t *bitmap_stats(void)
{
    return &g_bitmap_stats;
}

const char *bitmap_status(void)
{
    return g_bitmap_status;
}
