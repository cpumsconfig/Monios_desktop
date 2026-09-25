/*
 * Host-based reproduction / regression test for the Monios DEFLATE inflate.
 *
 * Build (MinGW gcc):
 *   gcc -O2 -Wall -Wextra -I..\include test_inflate.c -o test_inflate.exe
 *
 * It #includes zip.c directly so the (possibly still-static) inflate_raw is
 * visible.  crc32_buf is stubbed: it is only referenced by zip_reader_extract,
 * which this test never calls.
 */
#include <stdio.h>
#include <stdlib.h>

#include "zip.c"          /* brings in inflate_raw + monios freestanding headers */
#include "test_vectors.h"

/* stub: satisfies the reference inside zip_reader_extract; unused here */
uint32_t crc32_buf(const void *d, uint32_t n) { (void)d; (void)n; return 0; }

static uint8_t out[1u << 20];

int main(void)
{
    unsigned long failures = 0;
    unsigned long i;

    for (i = 0; i < NVECTORS; i++) {
        const test_vec_t *v = &VECTORS[i];
        int got;
        uint32_t j;

        for (j = 0; j < sizeof(out); j++) {
            out[j] = 0xEE;   /* poison tail so truncation is visible */
        }

        got = inflate_raw(v->comp, v->comp_len, out, (uint32_t) sizeof(out));

        if (got < 0) {
            printf("FAIL %-24s : inflate returned error\n", v->name);
            failures++;
            continue;
        }
        if ((uint32_t) got != v->exp_len) {
            printf("FAIL %-24s : len mismatch got=%d want=%u\n",
                   v->name, got, v->exp_len);
            failures++;
            continue;
        }
        for (j = 0; j < v->exp_len; j++) {
            if (out[j] != v->exp[j]) {
                printf("FAIL %-24s : first mismatch at byte %u "
                       "(got 0x%02x want 0x%02x)\n",
                       v->name, j, out[j], v->exp[j]);
                /* show a small window around the mismatch */
                if (j > 8) j -= 8; else j = 0;
                uint32_t e = v->exp_len < j + 24 ? v->exp_len : j + 24;
                printf("   out :");
                for (; j < e; j++) printf(" %02x", out[j]);
                printf("\n   want:");
                for (j = (v->exp_len < e - 8 ? 0 : e - 24); j < e; j++)
                    printf(" %02x", v->exp[j]);
                printf("\n");
                break;
            }
        }
        if (j == v->exp_len) {
            printf("ok   %-24s : %u bytes\n", v->name, v->exp_len);
        } else {
            failures++;
        }
    }

    /* ---- second pass: exercise the zlib_inflate wrapper (RFC1950 framing) */
    {
        static uint8_t framed[1u << 20];
        for (i = 0; i < NVECTORS; i++) {
            const test_vec_t *v = &VECTORS[i];
            uint32_t flen = v->comp_len + 6u;
            uint32_t j;
            int got;

            framed[0] = 0x78; framed[1] = 0x9c;          /* fake CMF/FLG */
            for (j = 0; j < v->comp_len; j++) {
                framed[2u + j] = v->comp[j];
            }
            framed[flen - 4] = framed[flen - 3] =
            framed[flen - 2] = framed[flen - 1] = 0;      /* fake adler32 */

            for (j = 0; j < sizeof(out); j++) out[j] = 0xEE;
            got = zlib_inflate(framed, flen, out, (uint32_t) sizeof(out));
            if (got != (int) v->exp_len) {
                printf("FAIL zlib_wrap %-18s : got=%d want=%u\n",
                       v->name, got, v->exp_len);
                failures++;
                continue;
            }
            for (j = 0; j < v->exp_len; j++) {
                if (out[j] != v->exp[j]) {
                    printf("FAIL zlib_wrap %-18s : mismatch at %u\n",
                           v->name, j);
                    failures++;
                    break;
                }
            }
        }
        /* short input must be rejected */
        if (zlib_inflate(framed, 5, out, (uint32_t) sizeof(out)) != -1) {
            printf("FAIL zlib_wrap : srclen<6 not rejected\n");
            failures++;
        }
    }

    printf("\n%lu/%lu passed", NVECTORS - failures, (unsigned long) NVECTORS);
    if (failures) {
        printf("  --  %lu FAILED\n", failures);
        return 1;
    }
    printf("  --  ALL OK\n");
    return 0;
}
