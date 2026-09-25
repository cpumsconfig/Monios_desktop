/*
 * png.c -- minimal PNG (zlib stored blocks) and BMP 24bpp encoders.
 *
 * Shared by user/apps/screenshot.c and user/apps/record.c.  Pure freestanding
 * C: no malloc, no libc, only memcpy/memset from the runtime string library.
 *
 * PNG structure produced:
 *   signature | IHDR | IDAT (zlib stream, uncompressed stored blocks) | IEND
 *
 * The IDAT stream uses DEFLATE "stored" blocks (BTYPE 00) so no Huffman
 * machinery is needed; decoders accept this perfectly valid form.
 */

#include "png.h"
#include "crc32.h"
#include "string.h"

static void be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v);
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/* Adler-32 over the logical raw scanline stream: per row one filter byte
 * (0x00) followed by w*3 RGB bytes. */
static uint32_t adler_scanlines(const uint8_t *rgb, uint32_t w, uint32_t h)
{
    uint32_t a = 1;
    uint32_t b = 0;
    uint32_t row;

    for (row = 0; row < h; row++) {
        uint32_t c;

        a = (a + 0u) % 65521u; /* filter byte 0x00 */
        b = (b + a) % 65521u;
        for (c = 0; c < w * 3u; c++) {
            a = (a + rgb[row * w * 3u + c]) % 65521u;
            b = (b + a) % 65521u;
        }
    }
    return (b << 16) | a;
}

/* Write one PNG chunk: length(4BE) type(4) data CRC(4BE). Returns new offset. */
static uint32_t png_put_chunk(uint8_t *out, uint32_t offset, uint32_t out_cap,
                              const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t *p;
    uint32_t crc;

    if (offset + 12u + len > out_cap) {
        return 0;
    }
    p = out + offset;
    be32(p, len);
    p += 4;
    p[0] = (uint8_t) type[0];
    p[1] = (uint8_t) type[1];
    p[2] = (uint8_t) type[2];
    p[3] = (uint8_t) type[3];
    crc = crc32_init();
    crc = crc32_update(crc, p, 4u);
    if (len > 0) {
        crc = crc32_update(crc, data, len);
    }
    crc = crc32_final(crc);
    p += 4;
    if (len > 0) {
        memcpy(p, data, len);
        p += len;
    }
    be32(p, crc);
    return offset + 12u + len;
}

uint32_t png_encode_rgb(uint8_t *out, uint32_t out_cap,
                        const uint8_t *rgb, uint32_t w, uint32_t h)
{
    static const uint8_t signature[8] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
    };
    uint8_t ihdr[13];
    uint32_t raw_len = h * (1u + w * 3u);
    uint32_t idat_cap = 2u + raw_len + ((raw_len / 65535u) + 1u) * 5u + 4u;
    uint8_t *idat;
    uint32_t pos = 0;
    uint32_t rp = 0;   /* logical raw-stream position */
    uint32_t outp;

    if (out == 0 || rgb == 0 || w == 0 || h == 0) {
        return 0;
    }
    if (8u + 12u + 13u + idat_cap + 12u > out_cap) {
        return 0;
    }

    memcpy(out, signature, 8u);
    pos = 8u;

    be32(ihdr + 0, w);
    be32(ihdr + 4, h);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 2;   /* color type: truecolour RGB */
    ihdr[10] = 0;  /* compression method */
    ihdr[11] = 0;  /* filter method */
    ihdr[12] = 0;  /* interlace */
    pos = png_put_chunk(out, pos, out_cap, "IHDR", ihdr, sizeof(ihdr));
    if (pos == 0) {
        return 0;
    }

    idat = out + pos;
    idat[0] = 0x78; /* zlib CMF: deflate, 32K window */
    idat[1] = 0x01; /* FLG: no preset dict, fastest level; (0x7801 % 31)==0 */
    outp = 2u;

    /* Emit stored blocks of up to 65535 raw bytes, walking the logical raw
     * stream (filter byte + RGB pixels per scanline). */
    while (rp < raw_len) {
        uint32_t take = raw_len - rp;
        uint32_t hpos;
        uint32_t filled;

        if (take > 65535u) {
            take = 65535u;
        }
        hpos = outp;
        outp += 5u; /* header patched once `take` and final flag are known */
        filled = 0;
        while (filled < take) {
            uint32_t row_idx = rp / (1u + w * 3u);
            uint32_t row_off = rp % (1u + w * 3u);
            uint32_t space = take - filled;

            if (row_off == 0) {
                idat[outp++] = 0x00; /* filter: none */
                filled++;
                rp++;
                continue;
            }
            {
                uint32_t avail = w * 3u - (row_off - 1u);
                uint32_t nbytes = avail < space ? avail : space;

                memcpy(idat + outp,
                       rgb + row_idx * (w * 3u) + (row_off - 1u),
                       nbytes);
                outp += nbytes;
                filled += nbytes;
                rp += nbytes;
            }
        }
        idat[hpos] = (rp == raw_len) ? 0x01u : 0x00u;
        be16(idat + hpos + 1, (uint16_t) take);
        be16(idat + hpos + 3, (uint16_t)(~take));
    }

    be32(idat + outp, adler_scanlines(rgb, w, h));
    outp += 4u;

    pos = png_put_chunk(out, pos, out_cap, "IDAT", idat, outp);
    if (pos == 0) {
        return 0;
    }
    pos = png_put_chunk(out, pos, out_cap, "IEND", 0, 0);
    return pos;
}

uint32_t bmp_encode_rgb(uint8_t *out, uint32_t out_cap,
                        const uint8_t *rgb, uint32_t w, uint32_t h)
{
    uint32_t row_size = (w * 3u + 3u) & ~3u;
    uint32_t pixel_bytes = row_size * h;
    uint32_t file_size = 54u + pixel_bytes;
    uint32_t y;

    if (out == 0 || rgb == 0 || w == 0 || h == 0) {
        return 0;
    }
    if (file_size > out_cap) {
        return 0;
    }
    memset(out, 0, file_size);

    /* BITMAPFILEHEADER (14 bytes) */
    out[0] = 'B';
    out[1] = 'M';
    out[2] = (uint8_t)(file_size);
    out[3] = (uint8_t)(file_size >> 8);
    out[4] = (uint8_t)(file_size >> 16);
    out[5] = (uint8_t)(file_size >> 24);
    out[10] = 54; /* offset to pixel data */

    /* BITMAPINFOHEADER (40 bytes) */
    out[14] = 40;
    be32(out + 18, w);
    be32(out + 22, h);
    out[26] = 1;  /* planes */
    out[28] = 24; /* bpp */
    be32(out + 34, pixel_bytes);

    /* Pixel data: bottom-up rows, BGR order. */
    for (y = 0; y < h; y++) {
        uint32_t src_row = h - 1u - y;
        uint32_t x;
        uint8_t *dst = out + 54u + y * row_size;
        const uint8_t *src = rgb + src_row * w * 3u;

        for (x = 0; x < w; x++) {
            dst[x * 3u + 0u] = src[x * 3u + 2u]; /* B */
            dst[x * 3u + 1u] = src[x * 3u + 1u]; /* G */
            dst[x * 3u + 2u] = src[x * 3u + 0u]; /* R */
        }
    }
    return file_size;
}
