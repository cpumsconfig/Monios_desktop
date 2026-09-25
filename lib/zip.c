#include "zip.h"
#include "crc32.h"
#include "string.h"
#include "stddef.h"

/*
 * Minimal ZIP reader/writer.  See include/zip.h for the design rationale.
 *
 * DEFLATE decompression is a from-scratch implementation of RFC 1951:
 *  - BTYPE 00: stored (uncompressed) blocks
 *  - BTYPE 01: fixed Huffman tables
 *  - BTYPE 02: dynamic Huffman tables
 *  - sliding window = 32KB (the output buffer itself is the window)
 */

/* ---------------- little-endian helpers ---------------- */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t) (p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0]        | ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) ((v >> 8) & 0xFFu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) ((v >> 8) & 0xFFu);
    p[2] = (uint8_t) ((v >> 16) & 0xFFu);
    p[3] = (uint8_t) ((v >> 24) & 0xFFu);
}

/* ---------------- DEFLATE bit reader ---------------- */

typedef struct {
    const uint8_t *buf;
    uint32_t       len;
    uint32_t       pos;
    uint32_t       bb;   /* bit buffer */
    uint32_t       bl;   /* bits valid */
    int            err;
} br_t;

static void br_init(br_t *br, const uint8_t *buf, uint32_t len)
{
    br->buf = buf;
    br->len = len;
    br->pos = 0;
    br->bb  = 0;
    br->bl  = 0;
    br->err = 0;
}

static uint32_t br_read(br_t *br, uint32_t n)
{
    uint32_t want = n;
    uint32_t val;

    while (br->bl < want) {
        if (br->pos >= br->len) {
            br->err = 1;
            return 0;
        }
        br->bb |= (uint32_t) br->buf[br->pos++] << br->bl;
        br->bl += 8;
    }
    val = br->bb & ((1u << n) - 1u);
    br->bb >>= n;
    br->bl  -= n;
    return val;
}

static void br_align(br_t *br)
{
    br->bb = 0;
    br->bl = 0;
}

/* ---------------- canonical Huffman ---------------- */

typedef struct {
    uint16_t count[16];   /* symbol count per code length */
    uint16_t sym[288];    /* symbols ordered by increasing code length */
} huff_t;

static void huff_build(huff_t *h, const uint8_t *lengths, uint32_t n)
{
    uint32_t i;
    uint16_t off[16];
    uint32_t len;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;

    off[1] = 0;
    for (len = 1; len < 15; len++) {
        off[len + 1] = (uint16_t) (off[len] + h->count[len]);
    }
    for (i = 0; i < n; i++) {
        if (lengths[i]) {
            h->sym[off[lengths[i]]++] = (uint16_t) i;
        }
    }
}

/* Read one symbol; returns -1 on error. */
static int huff_decode(br_t *br, const huff_t *h)
{
    uint32_t code = 0;
    uint32_t first = 0;
    uint32_t index = 0;
    uint32_t len;

    for (len = 1; len <= 15; len++) {
        /* Bits are consumed LSB-first from bytes, but the first bit read is
         * the MSB of the Huffman code (RFC 1951), so accumulate MSB-first. */
        code = (code << 1) | br_read(br, 1);
        {
            uint32_t cnt = h->count[len];
            if (code - first < cnt) {
                return h->sym[index + (code - first)];
            }
            index += cnt;
            first = (first + cnt) << 1;
        }
    }
    br->err = 1;
    return -1;
}

/* ---------------- fixed Huffman tables (BTYPE 01) ---------------- */

static void fixed_litlen_lengths(uint8_t *out)
{
    uint32_t i;
    for (i = 0; i < 288; i++) {
        if (i < 144u) {
            out[i] = 8;
        } else if (i < 256u) {
            out[i] = 9;
        } else if (i < 280u) {
            out[i] = 7;
        } else {
            out[i] = 8;
        }
    }
}

/* ---------------- length / distance extra bits ---------------- */

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289,
    16385, 24577
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

/*
 * Decode one dynamic-block length sequence for the lit/length + distance
 * code-length alphabets.
 */
static int read_dynamic_lengths(br_t *br, uint8_t *out, uint32_t total)
{
    static const uint8_t cl_order[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    uint8_t cl_len[19] = { 0 };
    huff_t cl_huff;
    uint32_t hclen;
    uint32_t i;
    uint32_t prev = 0;

    hclen = br_read(br, 4) + 4u;
    for (i = 0; i < hclen; i++) {
        cl_len[cl_order[i]] = (uint8_t) br_read(br, 3);
    }
    huff_build(&cl_huff, cl_len, 19);

    i = 0;
    while (i < total) {
        int sym = huff_decode(br, &cl_huff);
        uint32_t rep;
        uint32_t k;

        if (sym < 0) {
            return -1;
        }
        if (sym < 16) {
            out[i++] = (uint8_t) sym;
            prev = (uint32_t) sym;
        } else if (sym == 16) {
            rep = 3u + br_read(br, 2);
            for (k = 0; k < rep && i < total; k++) {
                out[i++] = (uint8_t) prev;
            }
        } else if (sym == 17) {
            rep = 3u + br_read(br, 3);
            for (k = 0; k < rep && i < total; k++) {
                out[i++] = 0;
            }
            prev = 0;
        } else { /* 18 */
            rep = 11u + br_read(br, 7);
            for (k = 0; k < rep && i < total; k++) {
                out[i++] = 0;
            }
            prev = 0;
        }
    }
    return 0;
}

/*
 * Decode the literal/length stream after the table has been chosen.
 */
static int decode_block_stream(br_t *br, const huff_t *lit, const huff_t *dist,
                              uint8_t *out, uint32_t cap, uint32_t *op)
{
    for (;;) {
        int sym = huff_decode(br, lit);
        uint32_t length;
        uint32_t distance;
        int dsym;
        uint32_t s;
        uint32_t k;

        if (sym < 0) {
            return -1;
        }
        if (sym == 256) {
            return 0; /* end of block */
        }
        if (sym < 256) {
            if (*op >= cap) {
                return -1;
            }
            out[(*op)++] = (uint8_t) sym;
            continue;
        }

        /* length symbol 257..285 */
        s = (uint32_t) (sym - 257);
        if (s >= 29u) {
            return -1;
        }
        length = len_base[s] + br_read(br, len_extra[s]);

        dsym = huff_decode(br, dist);
        if (dsym < 0 || dsym >= 30) {
            return -1;
        }
        distance = dist_base[dsym] + br_read(br, dist_extra[dsym]);

        if (distance > *op) {
            return -1;
        }
        for (k = 0; k < length; k++) {
            if (*op >= cap) {
                return -1;
            }
            out[*op] = out[*op - distance];
            (*op)++;
        }
    }
}

/*
 * Inflate a raw DEFLATE stream into out[].
 * Returns the expanded size, or -1.
 */
int inflate_raw(const uint8_t *src, uint32_t srclen,
                       uint8_t *out, uint32_t outcap)
{
    br_t br;
    uint32_t op = 0;

    br_init(&br, src, srclen);

    for (;;) {
        uint32_t bfinal = br_read(&br, 1);
        uint32_t btype  = br_read(&br, 2);

        if (btype == 0) {
            /* stored block: discard bits to byte boundary */
            uint16_t len;
            uint16_t nlen;
            uint32_t k;

            br_align(&br);
            if (br.pos + 4 > br.len) {
                return -1;
            }
            len  = rd16(br.buf + br.pos);
            nlen = rd16(br.buf + br.pos + 2);
            br.pos += 4;
            if ((uint32_t) len + (uint32_t) nlen != 0xFFFFu) {
                return -1;
            }
            if (br.pos + len > br.len) {
                return -1;
            }
            if ((uint32_t) len > outcap - op) {
                return -1;
            }
            for (k = 0; k < len; k++) {
                out[op++] = br.buf[br.pos++];
            }
        } else if (btype == 1) {
            uint8_t lengths[288];
            uint8_t dlengths[30];
            huff_t lit;
            huff_t dist;

            fixed_litlen_lengths(lengths);
            huff_build(&lit, lengths, 288);
            memset(dlengths, 5, sizeof(dlengths));
            huff_build(&dist, dlengths, 30);
            if (decode_block_stream(&br, &lit, &dist, out, outcap, &op) < 0) {
                return -1;
            }
        } else if (btype == 2) {
            uint8_t lengths[288 + 32];
            uint32_t hlit;
            uint32_t hdist;
            huff_t lit;
            huff_t dist;

            hlit  = br_read(&br, 5) + 257u;
            hdist = br_read(&br, 5) + 1u;
            if (hlit + hdist > sizeof(lengths)) {
                return -1;
            }
            if (read_dynamic_lengths(&br, lengths, hlit + hdist) < 0) {
                return -1;
            }
            huff_build(&lit, lengths, hlit);
            huff_build(&dist, lengths + hlit, hdist);
            if (decode_block_stream(&br, &lit, &dist, out, outcap, &op) < 0) {
                return -1;
            }
        } else {
            return -1;
        }

        if (bfinal) {
            break;
        }
    }

    if (br.err) {
        return -1;
    }
    return (int) op;
}

/*
 * Inflate a zlib-wrapped DEFLATE stream (RFC 1950, as used by PNG IDAT and
 * gzip-ish containers) into out[].  Strips the 2-byte CMF/FLG header and the
 * trailing 4-byte Adler-32 checksum, then hands the raw DEFLATE body to
 * inflate_raw.  Returns the expanded size, or -1.
 */
int zlib_inflate(const uint8_t *src, uint32_t srclen,
                 uint8_t *out, uint32_t outcap)
{
    if (src == NULL || srclen < 6u) {
        return -1;
    }
    return inflate_raw(src + 2u, srclen - 6u, out, outcap);
}

/* ---------------- reader ---------------- */

int zip_reader_open(zip_reader_t *z, const void *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *) data;
    uint32_t limit;
    uint32_t i;
    uint32_t cd_off;
    uint32_t cd_size;
    uint32_t count;
    uint32_t off;

    if (z == NULL) return -1;
    z->data  = p;
    z->size  = size;
    z->count = 0;
    z->ok    = 0;

    if (size < 22 || data == NULL) {
        return -1;
    }

    /* scan for EOCD signature 0x06054b50 */
    limit = size > 65557u ? size - 65557u : 0u;
    for (i = size - 22u; i >= limit; i--) {
        if (rd32(p + i) == 0x06054b50u &&
            rd16(p + i + 20) == size - i - 22u) {
            break;
        }
        if (i == 0) {
            break;
        }
    }
    if (i < limit || rd32(p + i) != 0x06054b50u ||
        rd16(p + i + 20) != size - i - 22u ||
        rd16(p + i + 4) != 0 || rd16(p + i + 6) != 0 ||
        rd16(p + i + 8) != rd16(p + i + 10)) {
        return -1;
    }

    count  = rd16(p + i + 10);
    cd_size = rd32(p + i + 12);
    cd_off  = rd32(p + i + 16);

    if (count > ZIP_MAX_ENTRIES) {
        return -1;
    }
    if (cd_off > i || cd_size > i - cd_off) {
        return -1;
    }

    off = cd_off;
    for (i = 0; i < count; i++) {
        zip_entry_t *e = &z->entries[i];
        uint16_t nlen;
        uint16_t name_copy_len;
        uint16_t xlen;
        uint16_t clen;
        uint32_t record_size;
        uint32_t j;

        if (off > cd_off + cd_size || cd_off + cd_size - off < 46u ||
            rd32(p + off) != 0x02014b50u ||
            (rd16(p + off + 8) & 0x41u) != 0 || rd16(p + off + 34) != 0) {
            return -1;
        }
        e->method           = rd16(p + off + 10);
        e->crc32            = rd32(p + off + 16);
        e->size_compressed  = rd32(p + off + 20);
        e->size_uncompressed = rd32(p + off + 24);
        nlen                = rd16(p + off + 28);
        xlen                = rd16(p + off + 30);
        clen                = rd16(p + off + 32);
        e->local_header_offset = rd32(p + off + 42);

        record_size = 46u + (uint32_t) nlen + (uint32_t) xlen + (uint32_t) clen;
        if (record_size < 46u || record_size > size - off ||
            record_size > cd_off + cd_size - off) {
            return -1;
        }
        name_copy_len = nlen;
        if (name_copy_len >= ZIP_NAME_MAX) {
            return -1; /* Truncation can alias a different destination file. */
        }
        for (j = 0; j < name_copy_len; j++) {
            if (p[off + 46 + j] == 0) return -1;
            e->name[j] = (char) p[off + 46 + j];
        }
        e->name[name_copy_len] = '\0';
        e->is_directory = (name_copy_len > 0 && e->name[name_copy_len - 1] == '/') ? 1u : 0u;

        off += record_size;
        z->count++;
    }

    z->ok = 1;
    return 0;
}

int zip_reader_extract(const zip_reader_t *z, uint32_t index,
                       void *out, uint32_t out_cap)
{
    const zip_entry_t *e;
    const uint8_t *base;
    uint16_t nlen;
    uint16_t xlen;
    const uint8_t *cdata;
    uint32_t cdata_len;
    int expanded;
    uint32_t crc;

    if (z == NULL || !z->ok || index >= z->count || out == NULL) {
        return -1;
    }
    e = &z->entries[index];
    if (e->size_uncompressed > out_cap || e->size_uncompressed > 0x7FFFFFFFu)
        return -1;

    if (e->local_header_offset > z->size || z->size - e->local_header_offset < 30u ||
        rd32(z->data + e->local_header_offset) != 0x04034b50u) {
        return -1;
    }
    nlen  = rd16(z->data + e->local_header_offset + 26);
    xlen  = rd16(z->data + e->local_header_offset + 28);
    {
        uint32_t data_off = e->local_header_offset + 30u;
        uint32_t variable = (uint32_t) nlen + (uint32_t) xlen;
        if (variable > z->size - data_off) {
            return -1;
        }
        data_off += variable;
        if (e->size_compressed > z->size - data_off) {
            return -1;
        }
        base = z->data + data_off;
    }

    /* sizes come from the central directory (handles data-descriptor bit 3) */
    cdata_len = e->size_compressed;
    cdata = base;

    if (e->method == ZIP_METHOD_STORE) {
        if (cdata_len > out_cap) {
            return -1;
        }
        memcpy(out, cdata, cdata_len);
        expanded = (int) cdata_len;
    } else if (e->method == ZIP_METHOD_DEFLATE) {
        expanded = inflate_raw(cdata, cdata_len, (uint8_t *) out, out_cap);
        if (expanded < 0) {
            return -1;
        }
    } else {
        return -1;
    }

    if ((uint32_t) expanded != e->size_uncompressed) {
        return -1;
    }
    crc = crc32_buf(out, (uint32_t) expanded);
    if (crc != e->crc32) {
        return -1;
    }
    return expanded;
}

/* ---------------- writer ---------------- */

void zip_writer_init(zip_writer_t *w, void *out, uint32_t cap)
{
    w->out       = (uint8_t *) out;
    w->out_cap   = cap;
    w->offset    = 0;
    w->cd_offset = 0;
    w->count     = 0;
}

static int push_bytes(zip_writer_t *w, const void *data, uint32_t len)
{
    if (w->offset > w->out_cap || len > w->out_cap - w->offset ||
        (len != 0 && (data == NULL || w->out == NULL))) {
        return -1;
    }
    if (len) {
        memcpy(w->out + w->offset, data, len);
        w->offset += len;
    }
    return 0;
}

int zip_writer_add_file(zip_writer_t *w, const char *name,
                        const void *data, uint32_t size)
{
    uint8_t header[30];
    uint32_t nlen;
    uint32_t crc;
    uint32_t local_off;
    zip_entry_t *e;
    uint32_t i;

    if (w == NULL || w->count >= ZIP_MAX_ENTRIES || name == NULL ||
        (size != 0 && data == NULL)) {
        return -1;
    }
    nlen = (uint32_t) strlen(name);
    if (nlen >= ZIP_NAME_MAX) {
        return -1;
    }
    /* Check the entire record before writing anything or reading file data. */
    if (w->out == NULL || w->offset > w->out_cap ||
        30u + nlen > w->out_cap - w->offset ||
        size > w->out_cap - w->offset - 30u - nlen) return -1;

    local_off = w->offset;
    crc = crc32_buf(data ? data : "", size);

    memset(header, 0, sizeof(header));
    wr32(header + 0, 0x04034b50u);
    wr16(header + 4, 20);          /* version needed */
    wr16(header + 6, 0x0800u);      /* flags: UTF-8 names */
    wr16(header + 8, ZIP_METHOD_STORE);
    wr32(header + 14, crc);
    wr32(header + 18, size);       /* compressed size */
    wr32(header + 22, size);       /* uncompressed size */
    wr16(header + 26, (uint16_t) nlen);
    /* extra len = 0 */

    if (push_bytes(w, header, 30) < 0) return -1;
    if (push_bytes(w, name, nlen) < 0) return -1;
    if (size > 0 && push_bytes(w, data, size) < 0) return -1;

    e = &w->entries[w->count++];
    for (i = 0; i < nlen; i++) {
        e->name[i] = name[i];
    }
    e->name[nlen] = '\0';
    e->method = ZIP_METHOD_STORE;
    e->crc32  = crc;
    e->size_compressed   = size;
    e->size_uncompressed = size;
    e->local_header_offset = local_off;
    e->is_directory = (nlen > 0 && name[nlen - 1] == '/') ? 1u : 0u;
    return 0;
}

int zip_writer_finish(zip_writer_t *w)
{
    uint8_t cd_entry[46];
    uint8_t eocd[22];
    uint32_t i;
    uint32_t cd_start;
    uint32_t nlen;

    cd_start = w->offset;

    for (i = 0; i < w->count; i++) {
        const zip_entry_t *e = &w->entries[i];
        nlen = (uint32_t) strlen(e->name);

        memset(cd_entry, 0, sizeof(cd_entry));
        wr32(cd_entry + 0, 0x02014b50u);
        wr16(cd_entry + 4, 20);        /* version made by */
        wr16(cd_entry + 6, 20);        /* version needed */
        wr16(cd_entry + 8, 0x0800u);
        wr16(cd_entry + 10, (uint16_t) e->method);
        wr32(cd_entry + 16, e->crc32);
        wr32(cd_entry + 20, e->size_compressed);
        wr32(cd_entry + 24, e->size_uncompressed);
        wr16(cd_entry + 28, (uint16_t) nlen);
        wr32(cd_entry + 42, e->local_header_offset);

        if (push_bytes(w, cd_entry, 46) < 0) return -1;
        if (push_bytes(w, e->name, nlen) < 0) return -1;
    }

    memset(eocd, 0, sizeof(eocd));
    wr32(eocd + 0, 0x06054b50u);
    wr16(eocd + 8, (uint16_t) w->count);
    wr16(eocd + 10, (uint16_t) w->count);
    wr32(eocd + 12, w->offset - cd_start);
    wr32(eocd + 16, cd_start);

    if (push_bytes(w, eocd, 22) < 0) return -1;
    return (int) w->offset;
}
