#include "crc32.h"

/*
 * CRC-32 implementation (reflected, poly 0xEDB88320).
 * Table is built on first use (no static initializer assumptions, no malloc).
 */

static uint32_t g_crc_table[256];
static int      g_crc_ready;

static void crc32_build_table(void)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < 256u; i++) {
        uint32_t c = i;
        for (j = 0; j < 8u; j++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[i] = c;
    }
    g_crc_ready = 1;
}

uint32_t crc32_init(void)
{
    if (!g_crc_ready) {
        crc32_build_table();
    }
    return 0xFFFFFFFFu;
}

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *) data;
    uint32_t i;

    if (!g_crc_ready) {
        crc32_build_table();
    }
    for (i = 0; i < len; i++) {
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_buf(const void *data, uint32_t len)
{
    uint32_t crc = crc32_init();
    crc = crc32_update(crc, data, len);
    return crc32_final(crc);
}
