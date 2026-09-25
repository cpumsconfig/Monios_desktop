/*
 * imageview.c - Monios image viewer (Task 10).
 *
 * Usage: imageview <file>
 *
 * Formats:
 *   - BMP : 24/32-bit BITMAPINFOHEADER, bottom-up flip handled.
 *   - PNG : signature / IHDR / PLTE / IDAT. Supports 8-bit RGB, RGBA,
 *           indexed and grayscale. Full zlib/DEFLATE inflate (BTYPE 00/01/02);
 *           filtered scanlines reconstructed (None/Sub/Up/Average/Paeth).
 *   - JPG : baseline JPEG decode (DQT/SOF0/DHT/SOS, Huffman + IDCT,
 *           YCbCr->RGB), grayscale and YCbCr (4:2:0 / 4:2:2 / 4:4:4).
 *
 * Viewing:
 *   - +/- zoom (10%..500%), 0 resets to 100%, F fits window
 *   - R clockwise 90 deg, L counter-clockwise
 *   - arrow keys or mouse drag pan
 *   - F5 slideshow (3s interval, other images in the same directory),
 *     Esc stops the show / quits
 *   - bottom-left info: file name, resolution, file size, zoom
 *
 * Rendering goes through app_graphics_fill_rect() (one syscall per block);
 * block size adapts to zoom so large images stay usable.
 */

#include "stdio.h"
#include "unistd.h"
#include "appsys.h"
#include "osui_dll.h"
#include "monios_dll.h"
#include "syscall.h"
#include "string.h"
#include "stdint.h"
#include "stdbool.h"
#include "zip.h"
#include "stddef.h"

/* freestanding app link: large JPEG decode stack frames need this stub */
void ___chkstk_ms(void) { }

/* ------------------------------------------------------------------ */
/* key event types (mirror kernel include/keyboard.h enum)            */
/* ------------------------------------------------------------------ */
#define EV_NONE   0
#define EV_CHAR   1
#define EV_UP     2
#define EV_DOWN   3
#define EV_LEFT   4
#define EV_RIGHT  5
#define EV_F5     10
#define EV_ESC    29

typedef struct {
    uint32_t type;
    char ch;
    uint8_t mods;
    uint8_t pad[3];
} app_key_event_t;

/* ------------------------------------------------------------------ */
/* static buffers (no malloc in this environment)                     */
/* ------------------------------------------------------------------ */
#define MAX_DEC_W   1024u
#define MAX_DEC_H   768u
#define FILE_BUF_SZ (512u * 1024u)

static uint32_t g_pixels[MAX_DEC_W * MAX_DEC_H]; /* decoded 0x00RRGGBB */
static uint32_t g_img_w;
static uint32_t g_img_h;
static bool     g_has_image;
static char     g_decode_note[64];

static uint8_t  g_filebuf[FILE_BUF_SZ];
static uint32_t g_file_size;

static uint32_t g_screen_w;
static uint32_t g_screen_h;

/* viewport = the area inside the window frame reserved for the picture */
#define VIEW_X   8u
#define VIEW_Y   44u

static int32_t  g_zoom;     /* percent, 10..500 */
static int32_t  g_pan_x;    /* pixels */
static int32_t  g_pan_y;
static uint32_t g_orient;    /* 0..3, quarter turns clockwise */
static bool     g_dirty;
static bool     g_slideshow;
static uint64_t g_next_slide_tick;

static char g_dir_path[PATH_MAX_LEN];
static char g_file_name[PATH_MAX_LEN];
static char g_full_path[PATH_MAX_LEN];

#define MAX_SLIDE_IMAGES 24
static char g_slide_names[MAX_SLIDE_IMAGES][64];
static uint32_t g_slide_count;
static uint32_t g_slide_index;

/* ------------------------------------------------------------------ */
/* tiny formatting helpers                                            */
/* ------------------------------------------------------------------ */
static uint32_t xstrlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void xfmt_u32(char *out, uint32_t v)
{
    char tmp[12];
    uint32_t i = 0, j = 0;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && i < sizeof(tmp)) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

static void xfmt_size(char *out, uint32_t bytes)
{
    if (bytes >= 1024u * 1024u) {
        uint32_t mb = bytes / (1024u * 1024u);
        uint32_t kb = (bytes % (1024u * 1024u)) / (1024u * 10u);
        char a[12], b[8];
        xfmt_u32(a, mb); xfmt_u32(b, kb);
        out[0] = 0;
        /* build "<mb>.<kb> MB" */
        for (uint32_t k = 0; a[k]; k++) out[xstrlen(out)] = a[k];
        out[xstrlen(out)] = '.';
        for (uint32_t k = 0; b[k]; k++) out[xstrlen(out)] = b[k];
        out[xstrlen(out)] = ' '; out[xstrlen(out)] = 'M'; out[xstrlen(out)] = 'B';
        out[xstrlen(out)] = 0;
    } else if (bytes >= 1024u) {
        uint32_t kb = bytes / 1024u;
        char a[12];
        xfmt_u32(a, kb);
        out[0] = 0;
        for (uint32_t k = 0; a[k]; k++) out[xstrlen(out)] = a[k];
        out[xstrlen(out)] = ' '; out[xstrlen(out)] = 'K'; out[xstrlen(out)] = 'B';
        out[xstrlen(out)] = 0;
    } else {
        char a[12];
        xfmt_u32(a, bytes);
        out[0] = 0;
        for (uint32_t k = 0; a[k]; k++) out[xstrlen(out)] = a[k];
        out[xstrlen(out)] = ' '; out[xstrlen(out)] = 'B';
        out[xstrlen(out)] = 0;
    }
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void put_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= MAX_DEC_W || y >= MAX_DEC_H) return;
    g_pixels[y * MAX_DEC_W + x] = 0x00000000u | ((uint32_t)r << 16) |
                                  ((uint32_t)g << 8) | (uint32_t)b;
}

/* ------------------------------------------------------------------ */
/* BMP decoder                                                        */
/* ------------------------------------------------------------------ */
static bool decode_bmp(const uint8_t *d, uint32_t n)
{
    uint32_t off, w, h, row_bytes;
    uint16_t bpp, comp;
    int32_t  hh;
    uint32_t x, y;
    bool top_down;

    if (n < 54u) return false;
    if (d[0] != 'B' || d[1] != 'M') return false;
    off   = rd_le32(d + 10);
    w     = rd_le32(d + 18);
    hh    = (int32_t)rd_le32(d + 22);
    bpp   = rd_le16(d + 28);
    comp  = rd_le32(d + 30);
    if (comp != 0u) { /* uncompressed only */
        xfmt_u32((char *)g_decode_note, 0);
        /* note: compressed BMP unsupported */
        g_decode_note[0] = 'x'; g_decode_note[1] = 'y'; g_decode_note[2] = 0;
    }
    if (bpp != 24u && bpp != 32u) return false;
    if (w == 0 || w > MAX_DEC_W) return false;
    if (hh == 0) return false;

    top_down = (hh < 0);
    h = (uint32_t)(hh < 0 ? -hh : hh);
    if (h > MAX_DEC_H) h = MAX_DEC_H;

    row_bytes = ((w * (uint32_t)bpp / 8u) + 3u) & ~3u;

    for (y = 0; y < h; y++) {
        const uint8_t *row;
        uint32_t sy = top_down ? y : (h - 1u - y);
        if (off + row_bytes * sy + (w * (uint32_t)bpp / 8u) > n) break;
        row = d + off + row_bytes * sy;
        for (x = 0; x < w; x++) {
            const uint8_t *px = row + x * (uint32_t)(bpp / 8u);
            /* BMP stores B,G,R */
            put_pixel(x, y, px[2], px[1], px[0]);
        }
    }
    g_img_w = w;
    g_img_h = h;
    return true;
}

/* ------------------------------------------------------------------ */
/* PNG decoder (stored zlib blocks + scanline filter reconstruction)  */
/* ------------------------------------------------------------------ */
static int32_t paeth(int32_t a, int32_t b, int32_t c)
{
    int32_t p = a + b - c;
    int32_t pa = p > a ? p - a : a - p;
    int32_t pb = p > b ? p - b : b - p;
    int32_t pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

/* zlib inflate: skip 2-byte CMF/FLG header and 4-byte adler32 trailer,
 * then call the full DEFLATE inflate from lib/zip.c (supports BTYPE 00/01/02).
 * Returns bytes written or -1. */
static int32_t png_zlib_inflate(const uint8_t *src, uint32_t n,
                                uint8_t *out, uint32_t out_cap)
{
    if (n < 6) return -1;
    /* src[0..1] = zlib CMF/FLG, src[n-4..n-1] = adler32 (big-endian) */
    return inflate_raw(src + 2, n - 6, out, out_cap);
}

static bool decode_png(const uint8_t *d, uint32_t n)
{
    static uint8_t raw[256u * 1024u];
    uint32_t pos = 8;
    uint32_t w = 0, h = 0, bitdepth = 0, colortype = 0;
    uint8_t plte[256 * 3];
    uint32_t plte_count = 0;
    static uint8_t idat[256u * 1024u];
    uint32_t idat_len = 0;
    uint32_t chans = 0;
    uint32_t stride, i, y, x;
    int32_t  out_len;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    for (i = 0; i < 8; i++) if (d[i] != sig[i]) return false;

    while (pos + 8 <= n) {
        uint32_t clen = rd_be32(d + pos);
        uint32_t ctype;
        uint32_t cdata = pos + 8;
        if (cdata + clen + 4 > n) break;
        ctype = rd_be32(d + pos + 4);
        if (ctype == 0x49484452u) { /* IHDR */
            w = rd_be32(d + cdata);
            h = rd_be32(d + cdata + 4);
            bitdepth = d[cdata + 8];
            colortype = d[cdata + 9];
        } else if (ctype == 0x504c5445u) { /* PLTE */
            plte_count = clen / 3u;
            if (plte_count > 256u) plte_count = 256u;
            for (i = 0; i < plte_count * 3u; i++) plte[i] = d[cdata + i];
        } else if (ctype == 0x49444154u) { /* IDAT */
            if (idat_len + clen <= sizeof(idat)) {
                for (i = 0; i < clen; i++) idat[idat_len++] = d[cdata + i];
            }
        } else if (ctype == 0x49454E44u) { /* IEND */
            break;
        }
        pos = cdata + clen + 4;
    }

    if (w == 0 || h == 0 || w > MAX_DEC_W || h > MAX_DEC_H) return false;
    if (bitdepth != 8u) return false;

    switch (colortype) {
        case 0: chans = 1; break;
        case 2: chans = 3; break;
        case 3: chans = 1; break;
        case 4: chans = 2; break;
        case 6: chans = 4; break;
        default: return false;
    }

    out_len = png_zlib_inflate(idat, idat_len, raw, sizeof(raw));
    if (out_len < 0) {
        g_decode_note[0] = 'z'; g_decode_note[1] = 'l'; g_decode_note[2] = 'i'; g_decode_note[3] = 'b'; g_decode_note[4] = 0;
        return false;
    }

    stride = w * chans;
    /* reconstruct filters in place. raw layout: filter byte per scanline. */
    {
        uint32_t i2 = 0;
        for (y = 0; y < h; y++) {
            uint8_t f = raw[i2++];
            uint8_t *line = &raw[i2];
            uint8_t *prev = (y > 0) ? &raw[i2 - stride - 1] : 0;
            uint32_t k;
            if (f > 4u) return false;
            for (k = 0; k < stride; k++) {
                int32_t a = (k >= chans) ? line[k - chans] : 0;
                int32_t b = prev ? prev[k] : 0;
                int32_t c = (prev && k >= chans) ? prev[k - chans] : 0;
                int32_t v = line[k];
                switch (f) {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) / 2; break;
                    default: v += paeth(a, b, c); break;
                }
                line[k] = (uint8_t)(v & 0xFF);
            }
            i2 += stride;
        }
    }

    for (y = 0; y < h; y++) {
        uint32_t off = y * (stride + 1u) + 1u;
        for (x = 0; x < w; x++) {
            uint8_t r = 0, g = 0, b = 0;
            uint32_t p = off + x * chans;
            if (p + chans > (uint32_t)out_len) break;
            switch (colortype) {
                case 0: r = g = b = raw[p]; break;
                case 4: r = g = b = raw[p]; break;
                case 2: r = raw[p]; g = raw[p + 1]; b = raw[p + 2]; break;
                case 6: r = raw[p]; g = raw[p + 1]; b = raw[p + 2]; break;
                case 3: {
                    uint32_t idx = raw[p];
                    if (idx < plte_count) {
                        r = plte[idx * 3]; g = plte[idx * 3 + 1]; b = plte[idx * 3 + 2];
                    }
                    break;
                }
                default: break;
            }
            put_pixel(x, y, r, g, b);
        }
    }
    g_img_w = w;
    g_img_h = h;
    return true;
}

/* ------------------------------------------------------------------ */
/* JPEG baseline decoder (Huffman + IDCT + YCbCr->RGB)                */
/* ------------------------------------------------------------------ */
#define JPEG_MAX_COMPONENTS 3
#define JPEG_MAX_SAMPLING   4
#define JPEG_MAX_MCU_SAMPLES (JPEG_MAX_SAMPLING * 8u * JPEG_MAX_SAMPLING * 8u)

typedef struct {
    bool     valid;
    uint16_t values[64];
} jpeg_quant_table_t;

typedef struct {
    bool    valid;
    uint8_t counts[17];
    uint8_t symbols[256];
    int32_t first_code[17];
    uint16_t first_symbol[17];
} jpeg_huff_table_t;

typedef struct {
    uint8_t id;
    uint8_t h;
    uint8_t v;
    uint8_t quant_table;
    uint8_t dc_table;
    uint8_t ac_table;
    int32_t dc_pred;
} jpeg_component_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t bit_buffer;
    uint8_t  bits_left;
    bool     failed;
} jpeg_bit_reader_t;

typedef struct {
    jpeg_quant_table_t quant[4];
    jpeg_huff_table_t  huffman[2][4];
    jpeg_component_t   components[JPEG_MAX_COMPONENTS];
    uint8_t  scan_order[JPEG_MAX_COMPONENTS];
    uint16_t width;
    uint16_t height;
    uint8_t  component_count;
    uint8_t  scan_count;
    uint8_t  max_h;
    uint8_t  max_v;
} jpeg_decoder_t;

static const uint8_t g_jpeg_zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static uint16_t jpeg_rd_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int32_t jpeg_descale(int64_t value, uint8_t shift)
{
    int64_t rounding = (int64_t)1 << (shift - 1);
    if (value >= 0) return (int32_t)((value + rounding) >> shift);
    return -(int32_t)(((-value) + rounding) >> shift);
}

static int32_t jpeg_receive_bits(jpeg_bit_reader_t *r, uint8_t count)
{
    int32_t result = 0;
    if (r == NULL || count > 16) return -1;
    while (count > 0) {
        if (r->bits_left == 0) {
            int32_t byte;
            if (r->pos >= r->size) { r->failed = true; return -1; }
            byte = (int32_t)r->data[r->pos++];
            if (byte == 0xFF) {
                while (r->pos < r->size && r->data[r->pos] == 0xFF) r->pos++;
                if (r->pos < r->size && r->data[r->pos] == 0x00) {
                    r->pos++;
                } else {
                    r->failed = true;
                    return -1;
                }
            }
            r->bit_buffer = (uint32_t)byte;
            r->bits_left = 8;
        }
        result = (result << 1) | (int32_t)((r->bit_buffer >> (r->bits_left - 1u)) & 1u);
        r->bits_left--;
        count--;
    }
    return result;
}

static bool jpeg_get_bits(jpeg_bit_reader_t *r, uint8_t count, int32_t *value)
{
    int32_t bits = jpeg_receive_bits(r, count);
    if (bits < 0) return false;
    if (count > 0 && bits < (1 << (count - 1u))) bits -= (1 << count) - 1;
    *value = bits;
    return true;
}

static bool jpeg_decode_huffman(jpeg_bit_reader_t *r,
                                const jpeg_huff_table_t *table,
                                uint8_t *symbol)
{
    int32_t code = 0;
    if (r == NULL || table == NULL || symbol == NULL || !table->valid) return false;
    for (uint8_t len = 1; len <= 16; len++) {
        int32_t bit = jpeg_receive_bits(r, 1);
        if (bit < 0) return false;
        code = (code << 1) | bit;
        if (table->counts[len] == 0) continue;
        if (code >= table->first_code[len] &&
            code < table->first_code[len] + (int32_t)table->counts[len]) {
            *symbol = table->symbols[table->first_symbol[len] +
                       (uint16_t)(code - table->first_code[len])];
            return true;
        }
    }
    return false;
}

static void jpeg_idct_block(const int32_t *block, uint8_t *out_pixels)
{
    static const int32_t c[8] = { 724, 1024, 1024, 1024, 1024, 1024, 1024, 1024 };
    static const int32_t cos_table[8][8] = {
        { 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024 },
        { 1004, 851, 569, 200, -200, -569, -851, -1004 },
        { 946, 392, -392, -946, -946, -392, 392, 946 },
        { 851, -200, -1004, -569, 569, 1004, 200, -851 },
        { 724, -724, -724, 724, 724, -724, -724, 724 },
        { 569, -1004, 200, 851, -851, -200, 1004, -569 },
        { 392, -946, 946, -392, -392, 946, -946, 392 },
        { 200, -569, 851, -1004, 1004, -851, 569, -200 }
    };
    int64_t temp[64];
    bool has_ac = false;
    for (uint16_t idx = 1; idx < 64; idx++) {
        if (block[idx] != 0) { has_ac = true; break; }
    }
    if (!has_ac) {
        int32_t value = jpeg_descale(block[0], 3) + 128;
        if (value < 0) value = 0;
        else if (value > 255) value = 255;
        for (int i = 0; i < 64; i++) out_pixels[i] = (uint8_t)value;
        return;
    }
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (uint8_t u = 0; u < 8; u++)
                sum += (int64_t)block[y * 8u + u] * c[u] * cos_table[u][x];
            temp[y * 8u + x] = sum;
        }
    }
    for (uint8_t y = 0; y < 8; y++) {
        for (uint8_t x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (uint8_t v = 0; v < 8; v++)
                sum += (int64_t)c[v] * temp[v * 8u + x] * cos_table[v][y];
            int32_t value = jpeg_descale(sum, 42) + 128;
            if (value < 0) value = 0;
            else if (value > 255) value = 255;
            out_pixels[y * 8u + x] = (uint8_t)value;
        }
    }
}

static bool jpeg_decode_block(jpeg_decoder_t *dec, jpeg_bit_reader_t *r,
                              uint8_t comp_idx, int32_t *block)
{
    jpeg_component_t *comp;
    const jpeg_quant_table_t *quant;
    uint8_t symbol, run_length, size_bits, k = 1;
    if (dec == NULL || r == NULL || block == NULL || comp_idx >= dec->component_count)
        return false;
    comp = &dec->components[comp_idx];
    if (comp->quant_table >= 4u || !dec->quant[comp->quant_table].valid) return false;
    quant = &dec->quant[comp->quant_table];
    for (int i = 0; i < 64; i++) block[i] = 0;
    if (!jpeg_decode_huffman(r, &dec->huffman[0][comp->dc_table], &symbol)) return false;
    size_bits = symbol;
    if (size_bits > 0) {
        int32_t delta;
        if (!jpeg_get_bits(r, size_bits, &delta)) return false;
        comp->dc_pred += delta;
    }
    block[0] = comp->dc_pred * (int32_t)quant->values[0];
    while (k < 64) {
        if (!jpeg_decode_huffman(r, &dec->huffman[1][comp->ac_table], &symbol)) return false;
        if (symbol == 0x00u) break;
        if (symbol == 0xF0u) { k = (uint8_t)(k + 16u); continue; }
        run_length = (uint8_t)(symbol >> 4u);
        size_bits = (uint8_t)(symbol & 0x0Fu);
        k = (uint8_t)(k + run_length);
        if (k >= 64u) return false;
        {
            int32_t coeff;
            uint8_t natural = g_jpeg_zigzag[k];
            if (!jpeg_get_bits(r, size_bits, &coeff)) return false;
            block[natural] = coeff * (int32_t)quant->values[natural];
        }
        k++;
    }
    return true;
}

static bool decode_jpg(const uint8_t *d, uint32_t n)
{
    static jpeg_decoder_t dec;
    jpeg_bit_reader_t reader;
    uint32_t pos;
    bool saw_sof = false, saw_sos = false;
    uint16_t mcu_width, mcu_height, mcu_cols, mcu_rows;
    static uint8_t samples[JPEG_MAX_COMPONENTS][JPEG_MAX_MCU_SAMPLES];
    uint16_t sample_width[JPEG_MAX_COMPONENTS];
    uint16_t sample_height[JPEG_MAX_COMPONENTS];

    if (d == NULL || n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
    for (int i = 0; i < (int)sizeof(dec); i++) ((uint8_t*)&dec)[i] = 0;
    pos = 2;

    while (pos + 4 <= n) {
        uint8_t marker;
        uint16_t seg_size;
        uint32_t seg_end;
        if (d[pos] != 0xFF) { pos++; continue; }
        while (pos < n && d[pos] == 0xFF) pos++;
        if (pos >= n) break;
        marker = d[pos++];
        if (marker == 0x00) continue;
        if (marker == 0xD9) break;
        if (marker >= 0xD0 && marker <= 0xD7) continue;
        if (pos + 2 > n) return false;
        seg_size = jpeg_rd_be16(d + pos);
        if (seg_size < 2) return false;
        seg_end = pos + seg_size;
        if (seg_end > n) return false;

        if (marker == 0xDB) { /* DQT - quantization tables */
            uint32_t cursor = pos + 2;
            while (cursor < seg_end) {
                uint8_t table_info = d[cursor++];
                uint8_t precision = table_info >> 4u;
                uint8_t table_id = table_info & 0x0Fu;
                if (table_id >= 4u || precision > 1u) return false;
                if (cursor + (precision == 0 ? 64u : 128u) > seg_end) return false;
                for (uint32_t vi = 0; vi < 64; vi++) {
                    uint16_t value;
                    uint8_t natural = g_jpeg_zigzag[vi];
                    if (precision == 0) { value = d[cursor++]; }
                    else { value = jpeg_rd_be16(d + cursor); cursor += 2; }
                    dec.quant[table_id].values[natural] = value;
                }
                dec.quant[table_id].valid = true;
            }
        } else if (marker == 0xC0) { /* SOF0 - baseline */
            uint32_t cursor = pos + 2;
            uint8_t comp_count;
            if (seg_end - cursor < 6) return false;
            if (d[cursor++] != 8) return false; /* only 8-bit */
            dec.height = jpeg_rd_be16(d + cursor); cursor += 2;
            dec.width  = jpeg_rd_be16(d + cursor); cursor += 2;
            comp_count = d[cursor++];
            if (comp_count == 0 || comp_count > JPEG_MAX_COMPONENTS ||
                comp_count == 2 || dec.width == 0 || dec.height == 0 ||
                dec.width > MAX_DEC_W || dec.height > MAX_DEC_H) return false;
            dec.component_count = comp_count;
            dec.max_h = 0; dec.max_v = 0;
            for (uint8_t i = 0; i < comp_count; i++) {
                jpeg_component_t *comp = &dec.components[i];
                comp->id = d[cursor++];
                comp->h = d[cursor] >> 4u;
                comp->v = d[cursor] & 0x0Fu;
                cursor++;
                comp->quant_table = d[cursor++];
                comp->dc_table = 0; comp->ac_table = 0; comp->dc_pred = 0;
                if (comp->h == 0 || comp->v == 0 ||
                    comp->h > JPEG_MAX_SAMPLING || comp->v > JPEG_MAX_SAMPLING ||
                    comp->quant_table >= 4u) return false;
                if (comp->h > dec.max_h) dec.max_h = comp->h;
                if (comp->v > dec.max_v) dec.max_v = comp->v;
            }
            saw_sof = true;
        } else if (marker == 0xC4) { /* DHT - Huffman tables */
            uint32_t cursor = pos + 2;
            while (cursor < seg_end) {
                uint8_t table_info = d[cursor++];
                uint8_t table_class = table_info >> 4u;
                uint8_t table_id = table_info & 0x0Fu;
                uint32_t total = 0;
                uint16_t sym_idx = 0;
                int32_t code = 0;
                if (table_class > 1u || table_id >= 4u || cursor + 16 > seg_end) return false;
                for (int i = 0; i < (int)sizeof(jpeg_huff_table_t); i++)
                    ((uint8_t*)&dec.huffman[table_class][table_id])[i] = 0;
                dec.huffman[table_class][table_id].valid = true;
                for (uint8_t len = 1; len <= 16; len++) {
                    dec.huffman[table_class][table_id].counts[len] = d[cursor++];
                    total += dec.huffman[table_class][table_id].counts[len];
                    if (dec.huffman[table_class][table_id].counts[len] > 0) {
                        dec.huffman[table_class][table_id].first_code[len] = code;
                        dec.huffman[table_class][table_id].first_symbol[len] = sym_idx;
                    }
                    code = (code + dec.huffman[table_class][table_id].counts[len]) << 1;
                    sym_idx += dec.huffman[table_class][table_id].counts[len];
                }
                if (cursor + total > seg_end || total > 256u) return false;
                for (uint32_t i = 0; i < total; i++)
                    dec.huffman[table_class][table_id].symbols[i] = d[cursor++];
            }
        } else if (marker == 0xDA) { /* SOS - start of scan */
            uint32_t cursor = pos + 2;
            if (!saw_sof || dec.component_count == 0) return false;
            if (cursor >= seg_end) return false;
            dec.scan_count = d[cursor++];
            if (dec.scan_count == 0 || dec.scan_count != dec.component_count ||
                cursor + (uint32_t)dec.scan_count * 2u + 3u > seg_end) return false;
            for (uint8_t i = 0; i < dec.scan_count; i++) {
                uint8_t comp_id = d[cursor++];
                uint8_t tables = d[cursor++];
                bool found = false;
                for (uint8_t j = 0; j < dec.component_count; j++) {
                    if (dec.components[j].id == comp_id) {
                        dec.components[j].dc_table = tables >> 4u;
                        dec.components[j].ac_table = tables & 0x0Fu;
                        if (dec.components[j].dc_table >= 4u ||
                            dec.components[j].ac_table >= 4u) return false;
                        dec.scan_order[i] = j;
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            if (d[cursor++] != 0 || d[cursor++] != 63 || d[cursor++] != 0) return false;
            saw_sos = true;
            pos = cursor;
            break;
        }
        pos = seg_end;
    }

    if (!saw_sof || !saw_sos) return false;

    /* ── entropy decode the scan ── */
    mcu_width  = (uint16_t)(dec.max_h * 8u);
    mcu_height = (uint16_t)(dec.max_v * 8u);
    if (mcu_width == 0 || mcu_height == 0) return false;
    reader.data = d + pos;
    reader.size = n - pos;
    reader.pos = 0;
    reader.bit_buffer = 0;
    reader.bits_left = 0;
    reader.failed = false;

    mcu_cols = (uint16_t)((dec.width + mcu_width - 1u) / mcu_width);
    mcu_rows = (uint16_t)((dec.height + mcu_height - 1u) / mcu_height);
    for (uint8_t i = 0; i < dec.component_count; i++) {
        sample_width[i]  = (uint16_t)(dec.components[i].h * 8u);
        sample_height[i] = (uint16_t)(dec.components[i].v * 8u);
        if (sample_width[i] == 0 || sample_height[i] == 0) return false;
    }

    for (uint16_t mcu_y = 0; mcu_y < mcu_rows; mcu_y++) {
        for (uint16_t mcu_x = 0; mcu_x < mcu_cols; mcu_x++) {
            uint16_t src_x_base = (uint16_t)(mcu_x * mcu_width);
            uint16_t src_y_base = (uint16_t)(mcu_y * mcu_height);
            for (uint8_t order = 0; order < dec.scan_count; order++) {
                uint8_t ci = dec.scan_order[order];
                jpeg_component_t *comp = &dec.components[ci];
                uint16_t sw = sample_width[ci];
                for (uint8_t by = 0; by < comp->v; by++) {
                    for (uint8_t bx = 0; bx < comp->h; bx++) {
                        int32_t block[64];
                        uint8_t block_pixels[64];
                        if (!jpeg_decode_block(&dec, &reader, ci, block)) return false;
                        jpeg_idct_block(block, block_pixels);
                        for (uint8_t yy = 0; yy < 8; yy++) {
                            uint8_t *row = samples[ci] + (by * 8u + yy) * sw + bx * 8u;
                            for (uint8_t xx = 0; xx < 8; xx++) row[xx] = block_pixels[yy * 8u + xx];
                        }
                    }
                }
            }
            /* convert MCU to RGB and write pixels */
            for (uint16_t py = 0; py < mcu_height; py++) {
                uint16_t src_y = (uint16_t)(src_y_base + py);
                if (src_y >= dec.height) continue;
                for (uint16_t px = 0; px < mcu_width; px++) {
                    uint16_t src_x = (uint16_t)(src_x_base + px);
                    uint8_t yv;
                    int32_t cb = 0, cr = 0;
                    if (src_x >= dec.width) continue;
                    {
                        uint16_t yx = (uint16_t)((px * sample_width[0]) / mcu_width);
                        uint16_t yy = (uint16_t)((py * sample_height[0]) / mcu_height);
                        if (yx >= sample_width[0]) yx = (uint16_t)(sample_width[0] - 1u);
                        if (yy >= sample_height[0]) yy = (uint16_t)(sample_height[0] - 1u);
                        yv = samples[0][yy * sample_width[0] + yx];
                    }
                    if (dec.component_count > 1) {
                        uint16_t cx = (uint16_t)((px * sample_width[1]) / mcu_width);
                        uint16_t cy = (uint16_t)((py * sample_height[1]) / mcu_height);
                        uint16_t rx = (uint16_t)((px * sample_width[2]) / mcu_width);
                        uint16_t ry = (uint16_t)((py * sample_height[2]) / mcu_height);
                        if (cx >= sample_width[1]) cx = (uint16_t)(sample_width[1] - 1u);
                        if (cy >= sample_height[1]) cy = (uint16_t)(sample_height[1] - 1u);
                        if (rx >= sample_width[2]) rx = (uint16_t)(sample_width[2] - 1u);
                        if (ry >= sample_height[2]) ry = (uint16_t)(sample_height[2] - 1u);
                        cb = samples[1][cy * sample_width[1] + cx] - 128;
                        cr = samples[2][ry * sample_width[2] + rx] - 128;
                    }
                    if (dec.component_count > 1) {
                        int32_t r = (int32_t)yv + ((91881 * cr) >> 16);
                        int32_t g = (int32_t)yv - ((22554 * cb + 46802 * cr) >> 16);
                        int32_t b = (int32_t)yv + ((116130 * cb) >> 16);
                        if (r < 0) r = 0; else if (r > 255) r = 255;
                        if (g < 0) g = 0; else if (g > 255) g = 255;
                        if (b < 0) b = 0; else if (b > 255) b = 255;
                        put_pixel(src_x, src_y, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        put_pixel(src_x, src_y, yv, yv, yv);
                    }
                }
            }
        }
    }
    if (reader.failed) return false;
    g_img_w = dec.width;
    g_img_h = dec.height;
    return true;
}

/* ------------------------------------------------------------------ */
/* file loading / dispatch                                            */
/* ------------------------------------------------------------------ */
static bool load_image(const char *path)
{
    int32_t sz;
    uint32_t i;

    g_has_image = false;
    g_img_w = 0; g_img_h = 0;
    g_decode_note[0] = 0;

    sz = app_file_size(path);
    if (sz <= 0) return false;
    g_file_size = (uint32_t)sz;
    if (g_file_size > FILE_BUF_SZ) g_file_size = FILE_BUF_SZ;

    if (app_file_read(path, g_filebuf, g_file_size) < 0) return false;

    if (decode_bmp(g_filebuf, g_file_size)) { g_has_image = true; }
    else if (decode_png(g_filebuf, g_file_size)) { g_has_image = true; }
    else if (decode_jpg(g_filebuf, g_file_size)) { g_has_image = true; }

    if (g_has_image) {
        g_zoom = 100;
        g_pan_x = 0; g_pan_y = 0; g_orient = 0;
        g_dirty = true;
    }
    return g_has_image;
}

/* split "C:\path\to\file.bmp" into dir + base name */
static void split_path(const char *full)
{
    uint32_t i = xstrlen(full);
    uint32_t base = i;
    while (i > 0 && full[i - 1] != '/' && full[i - 1] != '\\') i--;
    base = i;
    uint32_t dl = i;
    while (dl > 0 && full[dl - 1] != ':') dl--; /* keep drive */
    if (base == 0) { g_dir_path[0] = 'C'; g_dir_path[1] = ':'; g_dir_path[2] = '\\'; g_dir_path[3] = 0; }
    else {
        uint32_t k = 0;
        while (k < base && k < sizeof(g_dir_path) - 1) { g_dir_path[k] = full[k]; k++; }
        g_dir_path[k] = 0;
    }
    uint32_t j = 0;
    while (full[base] && j < sizeof(g_file_name) - 1) g_file_name[j++] = full[base++];
    g_file_name[j] = 0;
}

static bool has_image_ext(const char *name)
{
    uint32_t n = xstrlen(name);
    if (n < 5) return false;
    const char *ext = name + n - 4;
    /* compare ignoring case */
    static const char *exts[3] = { ".bmp", ".png", ".jpg" };
    for (uint32_t k = 0; k < 3; k++) {
        bool ok = true;
        for (uint32_t t = 0; t < 4; t++) {
            char a = ext[t], b = exts[k][t];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

static void build_slide_list(void)
{
    static char listbuf[2048];
    uint32_t i = 0;
    g_slide_count = 0;
    if (app_file_list_dir(g_dir_path, listbuf, sizeof(listbuf)) < 0) return;
    while (listbuf[i] && g_slide_count < MAX_SLIDE_IMAGES) {
        uint32_t start = i;
        while (listbuf[i] && listbuf[i] != '\n') i++;
        uint32_t len = i - start;
        if (len > 0 && start < i) {
            char name[64];
            uint32_t k = 0;
            while (k < len && k < sizeof(name) - 1) { name[k] = listbuf[start + k]; k++; }
            name[k] = 0;
            if (name[k - 1] == '/') { /* directory, skip */ }
            else if (has_image_ext(name)) {
                uint32_t m = 0;
                while (name[m] && m < sizeof(g_slide_names[0]) - 1) {
                    g_slide_names[g_slide_count][m] = name[m]; m++;
                }
                g_slide_names[g_slide_count][m] = 0;
                g_slide_count++;
            }
        }
        if (listbuf[i] == '\n') i++;
    }
    /* point at current file */
    for (uint32_t s = 0; s < g_slide_count; s++) {
        if (strcmp(g_slide_names[s], g_file_name) == 0) { g_slide_index = s; break; }
    }
}

static void load_slide(uint32_t idx)
{
    char full[PATH_MAX_LEN];
    uint32_t k = 0;
    while (g_dir_path[k] && k < sizeof(full) - 2) { full[k] = g_dir_path[k]; k++; }
    if (k == 0 || full[k - 1] != '/' && full[k - 1] != '\\') { full[k++] = '/'; }
    for (uint32_t m = 0; g_slide_names[idx][m] && k < sizeof(full) - 1; m++) {
        full[k++] = g_slide_names[idx][m];
    }
    full[k] = 0;
    if (load_image(full)) {
        uint32_t j = 0;
        while (full[j] && j < sizeof(g_full_path) - 1) { g_full_path[j] = full[j]; j++; }
        g_full_path[j] = 0;
        split_path(full);
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                          */
/* ------------------------------------------------------------------ */
static uint32_t sample_oriented(uint32_t sx, uint32_t sy)
{
    uint32_t lx, ly;
    switch (g_orient & 3u) {
        case 0: lx = sx; ly = sy; break;
        case 1: lx = g_img_h - 1u - sy; ly = sx; break;
        case 2: lx = g_img_w - 1u - sx; ly = g_img_h - 1u - sy; break;
        default: lx = sy; ly = g_img_w - 1u - sx; break;
    }
    if (lx >= g_img_w || ly >= g_img_h) return 0;
    return g_pixels[ly * MAX_DEC_W + lx];
}

static void render(void)
{
    uint32_t view_w = g_screen_w - VIEW_X * 2u;
    uint32_t view_h = g_screen_h - VIEW_Y - 36u;
    int32_t scale_num = g_zoom;      /* /100 */
    uint32_t ew = g_img_w, eh = g_img_h;
    int32_t ox, oy;
    uint32_t bx, by;

    osui_canvas(0x0014181F);
    osui_titlebar((osui_rect_t){ 0, 0, (uint16_t)g_screen_w, 36 }, "Monios Image Viewer", true);

    /* image backdrop */
    app_graphics_fill_rect((uint16_t)VIEW_X, (uint16_t)VIEW_Y,
                           (uint16_t)view_w, (uint16_t)view_h, 0x000B0E13);

    if (!g_has_image) {
        app_graphics_draw_text((uint16_t)(VIEW_X + 20), (uint16_t)(VIEW_Y + 20),
                               "No image loaded. Run: imageview <file>", 0x009AA4B2);
        app_graphics_draw_text((uint16_t)(VIEW_X + 20), (uint16_t)(VIEW_Y + 44),
                               "Keys: +/- zoom, 0=100%, F=fit, R/L rotate,", 0x006B7280);
        app_graphics_draw_text((uint16_t)(VIEW_X + 20), (uint16_t)(VIEW_Y + 64),
                               "arrows pan, F5 slideshow, Esc quit", 0x006B7280);
        osui_statusbar((osui_rect_t){ 0, (uint16_t)(g_screen_h - 28), (uint16_t)g_screen_w, 28 },
                       "imageview", "ready", OSUI_STATE_MUTED);
        osui_present();
        return;
    }

    if (g_orient & 1u) { uint32_t t = ew; ew = eh; eh = t; }

    {
        int32_t dw = (int32_t)ew * scale_num / 100;
        int32_t dh = (int32_t)eh * scale_num / 100;
        ox = (int32_t)VIEW_X + ((int32_t)view_w - dw) / 2 + g_pan_x;
        oy = (int32_t)VIEW_Y + ((int32_t)view_h - dh) / 2 + g_pan_y;
    }

    {
        int32_t block = scale_num / 100;
        int32_t step = 100 / scale_num;
        if (block < 1) block = 1;
        if (step < 1) step = 1;
        uint32_t bx32 = (uint32_t)block;
        uint32_t sx = 0;
        while (sx < ew) {
            uint32_t sy = 0;
            int32_t dx = ox + (int32_t)sx * scale_num / 100;
            while (sy < eh) {
                int32_t dy = oy + (int32_t)sy * scale_num / 100;
                if (dx + (int32_t)bx32 >= (int32_t)VIEW_X &&
                    dy + (int32_t)bx32 >= (int32_t)VIEW_Y &&
                    dx < (int32_t)(VIEW_X + view_w) &&
                    dy < (int32_t)(VIEW_Y + view_h)) {
                    uint16_t cx = (uint16_t)(dx < (int32_t)VIEW_X ? VIEW_X : dx);
                    uint16_t cy = (uint16_t)(dy < (int32_t)VIEW_Y ? VIEW_Y : dy);
                    uint32_t cw = bx32;
                    uint32_t ch = bx32;
                    int32_t ex = dx + (int32_t)bx32;
                    int32_t ey = dy + (int32_t)bx32;
                    if (ex > (int32_t)(VIEW_X + view_w)) cw = (uint32_t)(VIEW_X + view_w - cx);
                    if (ey > (int32_t)(VIEW_Y + view_h)) ch = (uint32_t)(VIEW_Y + view_h - cy);
                    if (cw > 0 && ch > 0) {
                        app_graphics_fill_rect(cx, cy, (uint16_t)cw, (uint16_t)ch,
                                               sample_oriented(sx, sy));
                    }
                }
                sy += (uint32_t)step;
            }
            sx += (uint32_t)step;
        }
    }

    /* info line bottom-left */
    {
        char line[128];
        char num[16];
        uint32_t k = 0;
        uint32_t z = (uint32_t)g_zoom;
        uint32_t rw, rh;
        if (g_orient & 1u) { rw = g_img_h; rh = g_img_w; } else { rw = g_img_w; rh = g_img_h; }

        line[k++] = ' ';
        for (uint32_t m = 0; g_file_name[m] && k < sizeof(line) - 20; m++) line[k++] = g_file_name[m];
        line[k++] = ' '; line[k++] = '(';
        xfmt_u32(num, rw); for (uint32_t m = 0; num[m]; m++) line[k++] = num[m];
        line[k++] = 'x';
        xfmt_u32(num, rh); for (uint32_t m = 0; num[m]; m++) line[k++] = num[m];
        line[k++] = ')';
        line[k++] = ' ';
        {
            char sz[24];
            xfmt_size(sz, g_file_size);
            for (uint32_t m = 0; sz[m] && k < sizeof(line) - 8; m++) line[k++] = sz[m];
        }
        line[k++] = ' ';
        xfmt_u32(num, z); for (uint32_t m = 0; num[m]; m++) line[k++] = num[m];
        line[k++] = '%';
        if (g_decode_note[0]) {
            line[k++] = ' '; line[k++] = '[';
            for (uint32_t m = 0; g_decode_note[m] && k < sizeof(line) - 2; m++) line[k++] = g_decode_note[m];
            line[k++] = ']';
        }
        line[k] = 0;

        bx = VIEW_X; by = g_screen_h - 28u;
        osui_statusbar((osui_rect_t){ 0, (uint16_t)(g_screen_h - 28), (uint16_t)g_screen_w, 28 },
                       line, g_slideshow ? "slideshow" : "ready",
                       g_slideshow ? OSUI_STATE_WARNING : OSUI_STATE_SUCCESS);
    }

    osui_present();
}

static void fit_window(void)
{
    if (!g_has_image) return;
    uint32_t view_w = g_screen_w - VIEW_X * 2u;
    uint32_t view_h = g_screen_h - VIEW_Y - 36u;
    uint32_t ew = g_img_w, eh = g_img_h;
    if (g_orient & 1u) { uint32_t t = ew; ew = eh; eh = t; }
    if (ew == 0 || eh == 0) return;
    int32_t zx = (int32_t)view_w * 100 / (int32_t)ew;
    int32_t zy = (int32_t)view_h * 100 / (int32_t)eh;
    g_zoom = zx < zy ? zx : zy;
    if (g_zoom < 10) g_zoom = 10;
    if (g_zoom > 500) g_zoom = 500;
    g_pan_x = 0; g_pan_y = 0;
}

/* ------------------------------------------------------------------ */
/* main loop                                                          */
/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    app_key_event_t ev;
    app_mouse_snapshot_t mouse;
    int32_t prev_btn = 0;
    int32_t last_mx = 0, last_my = 0;

    fputs("imageview.exe\r\n");

    if (argc < 2) {
        fputs("usage: imageview <file>\r\n");
    }

    app_enter_graphics_mode();
    g_screen_w = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_WIDTH);
    g_screen_h = (uint32_t)monios_syscall0(SYS_GRAPHICS_GET_HEIGHT);
    if (g_screen_w == 0) g_screen_w = 1024;
    if (g_screen_h == 0) g_screen_h = 768;

    g_full_path[0] = 0;
    if (argc >= 2) {
        uint32_t j = 0;
        while (argv[1][j] && j < sizeof(g_full_path) - 1) { g_full_path[j] = argv[1][j]; j++; }
        g_full_path[j] = 0;
    }

    if (g_full_path[0] && load_image(g_full_path)) {
        split_path(g_full_path);
        build_slide_list();
    }

    g_slideshow = false;
    g_dirty = true;

    for (;;) {
        /* drain keyboard event queue */
        bool did_action = false;
        while (syscall1(SYS_KEYBOARD_READ_EVENT, (uint64_t)&ev) == 1) {
            did_action = true;
            if (ev.type == EV_CHAR) {
                switch (ev.ch) {
                    case '+': case '=':
                        g_zoom += 10; if (g_zoom > 500) g_zoom = 500; break;
                    case '-': case '_':
                        g_zoom -= 10; if (g_zoom < 10) g_zoom = 10; break;
                    case '0': g_zoom = 100; g_pan_x = 0; g_pan_y = 0; break;
                    case 'f': case 'F': fit_window(); break;
                    case 'r': case 'R': g_orient = (g_orient + 1u) & 3u; break;
                    case 'l': case 'L': g_orient = (g_orient + 3u) & 3u; break;
                    case 'q': case 'Q': app_exit(0); break;
                    default: break;
                }
            } else if (ev.type == EV_UP)    { g_pan_y += 24; }
              else if (ev.type == EV_DOWN)  { g_pan_y -= 24; }
              else if (ev.type == EV_LEFT)  { g_pan_x += 24; }
              else if (ev.type == EV_RIGHT) { g_pan_x -= 24; }
              else if (ev.type == EV_F5) {
                g_slideshow = !g_slideshow;
                g_next_slide_tick = app_ticks() + 300u;
              } else if (ev.type == EV_ESC) {
                if (g_slideshow) { g_slideshow = false; } else { app_exit(0); }
              }
            g_dirty = true;
        }

        /* mouse drag pan */
        if (app_get_mouse(&mouse) >= 0) {
            int32_t btn = (int32_t)mouse.buttons;
            if ((btn & 1) && (prev_btn & 1)) {
                g_pan_x += mouse.x_pixels - last_mx;
                g_pan_y += mouse.y_pixels - last_my;
                g_dirty = true;
            }
            prev_btn = btn;
            last_mx = mouse.x_pixels;
            last_my = mouse.y_pixels;
        }

        /* slideshow timer (3s @100Hz tick) */
        if (g_slideshow && g_slide_count > 1 && app_ticks() >= g_next_slide_tick) {
            g_slide_index = (g_slide_index + 1u) % g_slide_count;
            load_slide(g_slide_index);
            g_next_slide_tick = app_ticks() + 300u;
            g_dirty = true;
        }

        if (g_dirty) {
            render();
            g_dirty = false;
        }

        if (!did_action) {
            app_sleep_ticks(2);
        }
    }
    return 0;
}
