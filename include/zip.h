#ifndef _ZIP_H_
#define _ZIP_H_

#include "stdint.h"

/*
 * Minimal ZIP archive reader/writer (store + inflate) for Monios.
 *
 * Design notes:
 *  - Pure freestanding C, no malloc: every object the caller needs is
 *    embedded in the reader/writer structs below.
 *  - The archive image lives in a caller-provided buffer.  This matches the
 *    Monios application layer where whole files are moved with
 *    app_file_read() / app_file_write().
 *  - Decompression supports method 0 (store) and method 8 (DEFLATE,
 *    BTYPE 00 / fixed / dynamic Huffman).  Compression writes method 0
 *    (store) by default; DEFLATE compression is intentionally not provided
 *    here (a safe, always-correct path), while still producing archives
 *    that any standard unzip can read.
 */

#define ZIP_METHOD_STORE   0U
#define ZIP_METHOD_DEFLATE 8U

#define ZIP_MAX_ENTRIES    64U
#define ZIP_NAME_MAX        128U

typedef struct {
    char     name[ZIP_NAME_MAX];
    uint32_t method;
    uint32_t crc32;
    uint32_t size_compressed;
    uint32_t size_uncompressed;
    uint32_t local_header_offset;
    uint32_t is_directory;
} zip_entry_t;

/* -------- raw inflate entry points (shared with the PNG decoder) -------- */

/*
 * Inflate a raw RFC 1951 DEFLATE bitstream (no zlib header / Adler-32
 * trailer) into out[].  Returns the expanded byte count, or -1 on error.
 */
int inflate_raw(const uint8_t *src, uint32_t srclen,
                uint8_t *out, uint32_t outcap);

/*
 * Inflate an RFC 1950 zlib stream (2-byte CMF/FLG header + raw DEFLATE +
 * trailing 4-byte Adler-32), as carried in PNG IDAT chunks, into out[].
 * Returns -1 when srclen < 6.  Returns the expanded byte count, or -1 on
 * error.
 */
int zlib_inflate(const uint8_t *src, uint32_t srclen,
                 uint8_t *out, uint32_t outcap);

/* -------- reader -------- */

typedef struct {
    const uint8_t *data;
    uint32_t       size;
    zip_entry_t   entries[ZIP_MAX_ENTRIES];
    uint32_t       count;
    int            ok;
} zip_reader_t;

/* Parse an archive image. Returns 0 on success, <0 on error. */
int zip_reader_open(zip_reader_t *z, const void *data, uint32_t size);

/*
 * Extract entry `index` into out[] (capacity out_cap).
 * Returns the expanded byte count, or -1 on error / overflow / CRC mismatch.
 */
int zip_reader_extract(const zip_reader_t *z, uint32_t index,
                       void *out, uint32_t out_cap);

/* -------- writer -------- */

typedef struct {
    uint8_t *out;
    uint32_t out_cap;
    uint32_t offset;
    uint32_t cd_offset;
    zip_entry_t entries[ZIP_MAX_ENTRIES];
    uint32_t count;
} zip_writer_t;

void zip_writer_init(zip_writer_t *w, void *out, uint32_t cap);

/*
 * Add one file (or a directory entry, when data == NULL / size == 0 and the
 * name ends with '/').  Always stored uncompressed.
 * Returns 0 on success, <0 if the output buffer is full.
 */
int zip_writer_add_file(zip_writer_t *w, const char *name,
                        const void *data, uint32_t size);

/*
 * Emit the central directory and end-of-central-directory record.
 * Returns the total archive size in bytes, or -1 on error.
 */
int zip_writer_finish(zip_writer_t *w);

#endif
