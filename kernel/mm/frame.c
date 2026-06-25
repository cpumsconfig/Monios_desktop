#include "frame.h"
#include "bitmap.h"
#include "common.h"

#define FRAME_BASE_PHYS 0x01000000ULL
#define FRAME_TOTAL_COUNT 4096U

static uint32_t g_frame_storage[(FRAME_TOTAL_COUNT + BITMAP_WORD_BITS - 1u) / BITMAP_WORD_BITS];
static bitmap_t g_frame_bitmap;
static frame_info_t g_frame_info;
static char g_frame_status[64];

static bool frame_range_valid(uint32_t index, uint32_t frame_count)
{
    return frame_count > 0 && index < FRAME_TOTAL_COUNT && index + frame_count <= FRAME_TOTAL_COUNT;
}

static bool frame_index_from_base(uint64_t base, uint32_t *out_index)
{
    uint64_t offset;

    if (base < FRAME_BASE_PHYS || ((base - FRAME_BASE_PHYS) % FRAME_PAGE_SIZE) != 0 || out_index == NULL) {
        return false;
    }
    offset = (base - FRAME_BASE_PHYS) / FRAME_PAGE_SIZE;
    if (offset >= FRAME_TOTAL_COUNT) {
        return false;
    }
    *out_index = (uint32_t) offset;
    return true;
}

static bool frame_mark_range(uint32_t start_index, uint32_t frame_count, bool allocate)
{
    if (!frame_range_valid(start_index, frame_count)) {
        return false;
    }
    for (uint32_t index = 0; index < frame_count; index++) {
        if (allocate) {
            if (!bitmap_set(&g_frame_bitmap, start_index + index)) {
                for (uint32_t rollback = 0; rollback < index; rollback++) {
                    bitmap_clear(&g_frame_bitmap, start_index + rollback);
                }
                return false;
            }
        } else if (!bitmap_clear(&g_frame_bitmap, start_index + index)) {
            for (uint32_t rollback = 0; rollback < index; rollback++) {
                bitmap_set(&g_frame_bitmap, start_index + rollback);
            }
            return false;
        }
    }
    return true;
}

void frame_init(void)
{
    memset(&g_frame_info, 0, sizeof(g_frame_info));
    bitmap_bind(&g_frame_bitmap, g_frame_storage, FRAME_TOTAL_COUNT);
    g_frame_info.base = FRAME_BASE_PHYS;
    g_frame_info.total_frames = FRAME_TOTAL_COUNT;
    strcpy(g_frame_status, "frame: ready");
}

uint64_t frame_alloc(uint32_t frame_count)
{
    return frame_alloc_aligned(frame_count, 1);
}

uint64_t frame_alloc_aligned(uint32_t frame_count, uint32_t align_frames)
{
    int32_t start_index;

    if (frame_count == 0) {
        return 0;
    }
    start_index = bitmap_find_run_zero(&g_frame_bitmap, 0, frame_count, align_frames);
    if (start_index < 0 || !frame_mark_range((uint32_t) start_index, frame_count, true)) {
        strcpy(g_frame_status, "frame: no free run");
        return 0;
    }
    g_frame_info.used_frames += frame_count;
    g_frame_info.alloc_requests++;
    g_frame_info.last_base = FRAME_BASE_PHYS + (uint64_t) start_index * FRAME_PAGE_SIZE;
    g_frame_info.last_count = frame_count;
    strcpy(g_frame_status, "frame: allocated");
    return g_frame_info.last_base;
}

bool frame_free(uint64_t base, uint32_t frame_count)
{
    uint32_t start_index;

    if (!frame_index_from_base(base, &start_index) || !frame_mark_range(start_index, frame_count, false)) {
        strcpy(g_frame_status, "frame: free failed");
        return false;
    }
    if (g_frame_info.used_frames >= frame_count) {
        g_frame_info.used_frames -= frame_count;
    } else {
        g_frame_info.used_frames = 0;
    }
    g_frame_info.free_requests++;
    g_frame_info.last_base = base;
    g_frame_info.last_count = frame_count;
    strcpy(g_frame_status, "frame: freed");
    return true;
}

bool frame_reserve(uint64_t base, uint32_t frame_count)
{
    uint32_t start_index;

    if (!frame_index_from_base(base, &start_index) || !frame_mark_range(start_index, frame_count, true)) {
        strcpy(g_frame_status, "frame: reserve failed");
        return false;
    }
    g_frame_info.used_frames += frame_count;
    g_frame_info.reserved_frames += frame_count;
    g_frame_info.last_base = base;
    g_frame_info.last_count = frame_count;
    strcpy(g_frame_status, "frame: reserved");
    return true;
}

const frame_info_t *frame_info(void)
{
    return &g_frame_info;
}

const char *frame_status(void)
{
    return g_frame_status;
}
