#include "common.h"
#include "font.h"
#include "file.h"
#include "kernel.h"
#include "memory.h"

static void *font_stb_malloc(unsigned int size, void *userdata)
{
    (void) userdata;
    return kmalloc(size == 0 ? 1 : size);
}

static void font_stb_free(void *ptr, void *userdata)
{
    (void) userdata;
    if (ptr != NULL) {
        kfree(ptr);
    }
}

static int font_floor(float value)
{
    int truncated = (int) value;
    return value < (float) truncated ? truncated - 1 : truncated;
}

static int font_ceil(float value)
{
    int truncated = (int) value;
    return value > (float) truncated ? truncated + 1 : truncated;
}

static float font_abs(float value)
{
    return value < 0.0f ? -value : value;
}

static float font_sqrt(float value)
{
    float guess;

    if (value <= 0.0f) {
        return 0.0f;
    }
    guess = value > 1.0f ? value : 1.0f;
    for (uint32_t i = 0; i < 12; i++) {
        guess = 0.5f * (guess + value / guess);
    }
    return guess;
}

typedef uint64_t size_t;

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_ifloor(x) font_floor((float) (x))
#define STBTT_iceil(x) font_ceil((float) (x))
#define STBTT_sqrt(x) font_sqrt((float) (x))
#define STBTT_pow(x, y) (1.0f)
#define STBTT_fmod(x, y) (0.0f)
#define STBTT_cos(x) (1.0f)
#define STBTT_acos(x) (0.0f)
#define STBTT_fabs(x) font_abs((float) (x))
#define STBTT_malloc(x, u) font_stb_malloc((unsigned int) (x), (u))
#define STBTT_free(x, u) font_stb_free((x), (u))
#define STBTT_assert(x) ((void) 0)
#define STBTT_strlen(x) strlen((x))
#define STBTT_memcpy memcpy
#define STBTT_memset memset
#include "stb_truetype.h"

#define FONT_GLYPH_CACHE_SIZE 512
#define FONT_BITMAP_BYTES (UI_FONT_WIDTH * UI_FONT_HEIGHT)
#define FONT_PIXEL_HEIGHT 18.0f
#define FONT_BASELINE 14
#define BOOT_FONT_REGION_PHYS 0x04000000ULL
#define BOOT_FONT_HEADER_SIZE 0x1000U
#define BOOT_FONT_MAX_BYTES 0x01800000U
#define BOOT_FONT_MAGIC 0x544E464DU

typedef struct {
    uint32_t magic;
    uint32_t header_size;
    uint64_t data_addr;
    uint64_t data_size;
    uint64_t reserved;
} boot_font_header_t;

static const uint8_t g_ascii_5x7[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x5F, 0x00, 0x00 },
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, { 0x14, 0x7F, 0x14, 0x7F, 0x14 },
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, { 0x23, 0x13, 0x08, 0x64, 0x62 },
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, { 0x00, 0x05, 0x03, 0x00, 0x00 },
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, { 0x00, 0x41, 0x22, 0x1C, 0x00 },
    { 0x14, 0x08, 0x3E, 0x08, 0x14 }, { 0x08, 0x08, 0x3E, 0x08, 0x08 },
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x08, 0x08, 0x08, 0x08, 0x08 },
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, { 0x20, 0x10, 0x08, 0x04, 0x02 },
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E },
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, { 0x00, 0x56, 0x36, 0x00, 0x00 },
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, { 0x14, 0x14, 0x14, 0x14, 0x14 },
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, { 0x02, 0x01, 0x51, 0x09, 0x06 },
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, { 0x7E, 0x11, 0x11, 0x11, 0x7E },
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, { 0x7F, 0x49, 0x49, 0x49, 0x41 },
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, { 0x00, 0x41, 0x7F, 0x41, 0x00 },
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, { 0x7F, 0x02, 0x0C, 0x02, 0x7F },
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, { 0x3E, 0x41, 0x51, 0x21, 0x5E },
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, { 0x46, 0x49, 0x49, 0x49, 0x31 },
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, { 0x3F, 0x40, 0x40, 0x40, 0x3F },
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, { 0x07, 0x08, 0x70, 0x08, 0x07 },
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, { 0x00, 0x7F, 0x41, 0x41, 0x00 },
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, { 0x00, 0x41, 0x41, 0x7F, 0x00 },
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, { 0x40, 0x40, 0x40, 0x40, 0x40 },
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, { 0x20, 0x54, 0x54, 0x54, 0x78 },
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, { 0x38, 0x44, 0x44, 0x44, 0x20 },
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, { 0x38, 0x54, 0x54, 0x54, 0x18 },
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, { 0x0C, 0x52, 0x52, 0x52, 0x3E },
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, { 0x00, 0x44, 0x7D, 0x40, 0x00 },
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, { 0x7F, 0x10, 0x28, 0x44, 0x00 },
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, { 0x7C, 0x04, 0x18, 0x04, 0x78 },
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, { 0x38, 0x44, 0x44, 0x44, 0x38 },
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, { 0x08, 0x14, 0x14, 0x18, 0x7C },
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, { 0x48, 0x54, 0x54, 0x54, 0x20 },
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, { 0x3C, 0x40, 0x40, 0x20, 0x7C },
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, { 0x3C, 0x40, 0x30, 0x40, 0x3C },
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, { 0x0C, 0x50, 0x50, 0x50, 0x3C },
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, { 0x00, 0x08, 0x36, 0x41, 0x00 },
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, { 0x00, 0x41, 0x36, 0x08, 0x00 },
    { 0x10, 0x08, 0x08, 0x10, 0x08 },
};

typedef struct {
    bool valid;
    uint32_t codepoint;
    int32_t advance;
    uint8_t bitmap[FONT_BITMAP_BYTES];
} font_cached_glyph_t;

static uint8_t *g_font_data;
static uint32_t g_font_size;
static stbtt_fontinfo g_font_info;
static float g_font_scale;
static font_cached_glyph_t g_font_cache[FONT_GLYPH_CACHE_SIZE];
static uint32_t g_font_cache_next;
static bool g_font_ready;
static bool g_font_has_ascii;

static uint32_t font_default_advance(uint32_t codepoint)
{
    if (codepoint == '\t') {
        return UI_FONT_ADVANCE * 4u;
    }
    if (codepoint < 0x80) {
        return UI_FONT_ADVANCE;
    }
    return UI_FONT_WIDE_ADVANCE;
}

static void font_log_size(const char *prefix, uint32_t value)
{
    char line[64];
    char digits[11];
    uint32_t digit_count = 0;
    uint32_t pos;

    strcpy(line, prefix);
    if (value == 0) {
        strcpy(line + strlen(line), "0");
        log_write(line);
        return;
    }
    while (value > 0 && digit_count < sizeof(digits)) {
        digits[digit_count++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    pos = (uint32_t) strlen(line);
    while (digit_count > 0 && pos + 1 < sizeof(line)) {
        line[pos++] = digits[--digit_count];
    }
    line[pos] = '\0';
    log_write(line);
}

static int32_t font_round_to_int(float value)
{
    if (value >= 0.0f) {
        return (int32_t) (value + 0.5f);
    }
    return (int32_t) (value - 0.5f);
}

static void font_cache_clear(void)
{
    memset(g_font_cache, 0, sizeof(g_font_cache));
    g_font_cache_next = 0;
}

static bool font_activate(uint8_t *data, uint32_t size, const char *loaded_prefix)
{
    int32_t offset;

    if (data == NULL || size == 0) {
        return false;
    }
    offset = stbtt_GetFontOffsetForIndex(data, 0);
    if (offset < 0 || !stbtt_InitFont(&g_font_info, data, offset)) {
        return false;
    }
    g_font_data = data;
    g_font_size = size;
    g_font_scale = stbtt_ScaleForPixelHeight(&g_font_info, FONT_PIXEL_HEIGHT);
    g_font_has_ascii = stbtt_FindGlyphIndex(&g_font_info, 'A') != 0;
    font_cache_clear();
    g_font_ready = true;
    font_log_size(loaded_prefix, g_font_size);
    return true;
}

static bool font_init_boot_region(void)
{
    const boot_font_header_t *header = (const boot_font_header_t *) (uintptr_t) BOOT_FONT_REGION_PHYS;
    uint64_t data_start = BOOT_FONT_REGION_PHYS + BOOT_FONT_HEADER_SIZE;
    uint64_t data_end = data_start + BOOT_FONT_MAX_BYTES;
    uint64_t data_addr;
    uint64_t data_size;

    if (header->magic != BOOT_FONT_MAGIC) {
        return false;
    }
    data_addr = header->data_addr;
    data_size = header->data_size;
    if (header->header_size < sizeof(boot_font_header_t) ||
        header->header_size > BOOT_FONT_HEADER_SIZE ||
        data_size == 0 ||
        data_size > BOOT_FONT_MAX_BYTES ||
        data_addr < data_start ||
        data_addr > data_end ||
        data_size > data_end - data_addr) {
        log_write("font: boot font header invalid");
        return false;
    }
    if (!font_activate((uint8_t *) (uintptr_t) data_addr,
                       (uint32_t) data_size,
                       "font: loaded boot font bytes=")) {
        log_write("font: boot font parse failed");
        return false;
    }
    return true;
}

bool font_ready(void)
{
    return g_font_ready;
}

uint32_t font_utf8_next(const char **cursor)
{
    const uint8_t *text = (const uint8_t *) *cursor;
    uint32_t codepoint;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    if (text[0] < 0x80) {
        (*cursor)++;
        return text[0];
    }
    if ((text[0] & 0xE0) == 0xC0 && (text[1] & 0xC0) == 0x80) {
        codepoint = ((uint32_t) (text[0] & 0x1F) << 6) | (text[1] & 0x3F);
        *cursor += 2;
        return codepoint;
    }
    if ((text[0] & 0xF0) == 0xE0 && (text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80) {
        codepoint = ((uint32_t) (text[0] & 0x0F) << 12) |
                    ((uint32_t) (text[1] & 0x3F) << 6) |
                    (text[2] & 0x3F);
        *cursor += 3;
        return codepoint;
    }
    (*cursor)++;
    return '?';
}

uint32_t font_text_width(const char *text)
{
    uint32_t width = 0;

    while (text != NULL && *text != '\0') {
        uint32_t codepoint = font_utf8_next(&text);
        width += font_codepoint_advance(codepoint);
    }
    return width;
}

static font_cached_glyph_t *font_find_cached_glyph(uint32_t codepoint)
{
    for (uint32_t i = 0; i < FONT_GLYPH_CACHE_SIZE; i++) {
        if (g_font_cache[i].valid && g_font_cache[i].codepoint == codepoint) {
            return &g_font_cache[i];
        }
    }
    return NULL;
}

static void font_draw_fallback(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color, font_plot_fn plot)
{
    uint16_t width = (uint16_t) (UI_FONT_WIDTH - 4);
    uint16_t height = (uint16_t) (UI_FONT_HEIGHT - 4);

    if (plot == NULL || codepoint == ' ') {
        return;
    }
    if (codepoint >= 0x21 && codepoint <= 0x7E) {
        const uint8_t *glyph = g_ascii_5x7[codepoint - 0x20];

        for (uint16_t col = 0; col < 5; col++) {
            for (uint16_t row = 0; row < 7; row++) {
                if ((glyph[col] & (1u << row)) != 0) {
                    uint16_t px = (uint16_t) (x + 2 + col * 2);
                    uint16_t py = (uint16_t) (y + 2 + row * 2);

                    plot(px, py, color);
                    plot((uint16_t) (px + 1), py, color);
                    plot(px, (uint16_t) (py + 1), color);
                    plot((uint16_t) (px + 1), (uint16_t) (py + 1), color);
                }
            }
        }
        return;
    }
    for (uint16_t col = 0; col < width; col++) {
        plot((uint16_t) (x + 2 + col), (uint16_t) (y + 2), color);
        plot((uint16_t) (x + 2 + col), (uint16_t) (y + 2 + height - 1), color);
    }
    for (uint16_t row = 0; row < height; row++) {
        plot((uint16_t) (x + 2), (uint16_t) (y + 2 + row), color);
        plot((uint16_t) (x + 2 + width - 1), (uint16_t) (y + 2 + row), color);
    }
}

static font_cached_glyph_t *font_rasterize_glyph(uint32_t codepoint)
{
    font_cached_glyph_t *glyph;
    int32_t glyph_index;
    int32_t advance_width;
    int32_t left_bearing;
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
    int32_t width;
    int32_t height;
    int32_t x_offset;
    int32_t y_offset;
    uint8_t *bitmap;
    int32_t draw_x;
    int32_t draw_y;

    if (!g_font_ready) {
        return NULL;
    }
    if (codepoint < 0x80 && !g_font_has_ascii) {
        return NULL;
    }
    glyph_index = stbtt_FindGlyphIndex(&g_font_info, (int32_t) codepoint);
    if (glyph_index == 0 && codepoint != 0) {
        return NULL;
    }
    glyph = &g_font_cache[g_font_cache_next++ % FONT_GLYPH_CACHE_SIZE];
    memset(glyph, 0, sizeof(*glyph));
    glyph->valid = true;
    glyph->codepoint = codepoint;
    stbtt_GetGlyphHMetrics(&g_font_info, glyph_index, &advance_width, &left_bearing);
    glyph->advance = font_round_to_int((float) advance_width * g_font_scale);
    if (glyph->advance <= 0 || glyph->advance > UI_FONT_WIDTH) {
        glyph->advance = (int32_t) font_default_advance(codepoint);
    }
    stbtt_GetGlyphBitmapBox(&g_font_info, glyph_index, g_font_scale, g_font_scale, &x0, &y0, &x1, &y1);
    width = x1 - x0;
    height = y1 - y0;
    if (width <= 0 || height <= 0) {
        return glyph;
    }
    bitmap = stbtt_GetGlyphBitmap(&g_font_info, g_font_scale, g_font_scale, glyph_index, &width, &height, &x_offset, &y_offset);
    if (bitmap == NULL) {
        return glyph;
    }
    draw_x = (UI_FONT_WIDTH - width) / 2;
    if (codepoint >= 0x20 && codepoint <= 0x7E) {
        draw_x = x_offset + 1;
    }
    draw_y = FONT_BASELINE + y_offset;
    for (int32_t row = 0; row < height; row++) {
        int32_t dst_y = draw_y + row;
        if (dst_y < 0 || dst_y >= UI_FONT_HEIGHT) {
            continue;
        }
        for (int32_t col = 0; col < width; col++) {
            int32_t dst_x = draw_x + col;
            if (dst_x < 0 || dst_x >= UI_FONT_WIDTH) {
                continue;
            }
            glyph->bitmap[dst_y * UI_FONT_WIDTH + dst_x] = bitmap[row * width + col];
        }
    }
    stbtt_FreeBitmap(bitmap, NULL);
    return glyph;
}

uint32_t font_codepoint_advance(uint32_t codepoint)
{
    font_cached_glyph_t *glyph;

    if (codepoint == 0) {
        return 0;
    }
    if (codepoint == '\t') {
        return UI_FONT_ADVANCE * 4u;
    }
    if (codepoint < 0x80 && g_font_ready && !g_font_has_ascii) {
        return font_default_advance(codepoint);
    }
    glyph = font_find_cached_glyph(codepoint);
    if (glyph == NULL) {
        glyph = font_rasterize_glyph(codepoint);
    }
    if (glyph == NULL || glyph->advance <= 0) {
        return font_default_advance(codepoint);
    }
    return (uint32_t) glyph->advance;
}

void font_draw_codepoint(uint16_t x, uint16_t y, uint32_t codepoint, uint32_t color, font_plot_fn plot)
{
    font_cached_glyph_t *glyph;

    if (plot == NULL || codepoint == 0) {
        return;
    }
    if (codepoint == '\t') {
        codepoint = ' ';
    }
    glyph = font_find_cached_glyph(codepoint);
    if (glyph == NULL) {
        glyph = font_rasterize_glyph(codepoint);
    }
    if (glyph == NULL) {
        font_draw_fallback(x, y, codepoint, color, plot);
        return;
    }
    for (uint16_t row = 0; row < UI_FONT_HEIGHT; row++) {
        for (uint16_t col = 0; col < UI_FONT_WIDTH; col++) {
            if (glyph->bitmap[row * UI_FONT_WIDTH + col] >= 72) {
                plot((uint16_t) (x + col), (uint16_t) (y + row), color);
            }
        }
    }
}

void font_init(void)
{
    int32_t size;
    int32_t read_size;
    const char *path = UI_FONT_PATH;

    if (g_font_ready) {
        return;
    }
    if (font_init_boot_region()) {
        return;
    }
    size = file_size(UI_FONT_PATH);
    if (size <= 0) {
        path = UI_FONT_FALLBACK_PATH;
        size = file_size(path);
    }
    if (size <= 0) {
        log_write("font: msyh.ttc not found");
        return;
    }
    g_font_data = (uint8_t *) kmalloc((uint32_t) size);
    if (g_font_data == NULL) {
        log_write("font: alloc failed");
        return;
    }
    read_size = file_read(path, g_font_data, (uint32_t) size);
    if (read_size != size) {
        kfree(g_font_data);
        g_font_data = NULL;
        log_write("font: read failed");
        return;
    }
    if (!font_activate(g_font_data, (uint32_t) size, "font: loaded msyh.ttc bytes=")) {
        kfree(g_font_data);
        g_font_data = NULL;
        log_write("font: parse failed");
        return;
    }
}
