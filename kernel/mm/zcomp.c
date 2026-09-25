#include "common.h"
#include "zcomp.h"
#include "zs.h"
#include "memory.h"
#include "string.h"

/*
 * 内存压缩框架实现
 *  - 内置轻量 LZ77 压缩（无外部库）
 *  - 压缩后的字节串存进 zsmalloc 池（zs.c）
 *  - 缺页时 zcomp_decompress_page() 解压恢复
 */

#define ZCOMP_WINDOW      2048u
#define ZCOMP_MAX_MATCH   16u
#define ZCOMP_MIN_MATCH   3u
#define ZCOMP_LITERAL_MAX 255u

#define ZCOMP_TAG_LITERAL 0xFDu
#define ZCOMP_TAG_MATCH   0xFEu
#define ZCOMP_TAG_END     0xFFu

typedef struct {
    uint64_t vaddr;
    zcomp_page_state_t state;
    bool referenced;
    void *zs_handle;
    uint32_t orig_size;
    uint32_t comp_size;
} zcomp_slot_t;

static zcomp_slot_t g_slots[ZCOMP_MAX_PAGES];
static zcomp_info_t g_info;
static zs_pool_t *g_pool;
static char g_status[64];

/* ============================================================
 *  轻量 LZ77 编解码
 *  流格式：
 *    0xFD <count:1> <count bytes literal>
 *    0xFE <off_lo:1> <off_hi4:1> <len_m3:1>   (offset 1..4095, len 3..18)
 *    0xFF       结束
 * ============================================================ */

uint32_t zcomp_lz_encode(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap)
{
    uint32_t i = 0;
    uint32_t o = 0;
    uint32_t lit_start = 0;

    if (src == NULL || dst == NULL || src_len == 0 || dst_cap < 8U) {
        return 0;
    }

    while (i < src_len) {
        uint32_t best_len = 0;
        uint32_t best_off = 0;
        uint32_t max_off = (i < ZCOMP_WINDOW) ? i : ZCOMP_WINDOW;
        uint32_t off;

        /* 在窗口内找最长匹配 */
        for (off = 1; off <= max_off; off++) {
            uint32_t start = i - off;
            uint32_t len = 0;

            while (len < ZCOMP_MAX_MATCH &&
                   (i + len) < src_len &&
                   src[start + len] == src[i + len]) {
                len++;
            }
            if (len > best_len) {
                best_len = len;
                best_off = off;
                if (best_len == ZCOMP_MAX_MATCH) {
                    break;
                }
            }
        }

        if (best_len >= ZCOMP_MIN_MATCH) {
            uint32_t lit_count = i - lit_start;

            /* 先 flush 待处理字面量 */
            if (lit_count > 0) {
                if (o + 2U + lit_count > dst_cap) return 0;
                dst[o++] = ZCOMP_TAG_LITERAL;
                dst[o++] = (uint8_t) lit_count;
                memcpy(dst + o, src + lit_start, lit_count);
                o += lit_count;
            }
            /* 输出匹配 */
            if (o + 4U > dst_cap) return 0;
            dst[o++] = ZCOMP_TAG_MATCH;
            dst[o++] = (uint8_t) (best_off & 0xFFu);
            dst[o++] = (uint8_t) ((best_off >> 8) & 0x0Fu);
            dst[o++] = (uint8_t) (best_len - ZCOMP_MIN_MATCH);
            i += best_len;
            lit_start = i;
        } else {
            i++;
            if ((i - lit_start) >= ZCOMP_LITERAL_MAX) {
                uint32_t lit_count = i - lit_start;

                if (o + 2U + lit_count > dst_cap) return 0;
                dst[o++] = ZCOMP_TAG_LITERAL;
                dst[o++] = (uint8_t) lit_count;
                memcpy(dst + o, src + lit_start, lit_count);
                o += lit_count;
                lit_start = i;
            }
        }
    }

    /* flush 尾部字面量 */
    if (i > lit_start) {
        uint32_t lit_count = i - lit_start;

        if (o + 2U + lit_count > dst_cap) return 0;
        dst[o++] = ZCOMP_TAG_LITERAL;
        dst[o++] = (uint8_t) lit_count;
        memcpy(dst + o, src + lit_start, lit_count);
        o += lit_count;
    }

    if (o + 1U > dst_cap) return 0;
    dst[o++] = ZCOMP_TAG_END;
    return o;
}

uint32_t zcomp_lz_decode(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_cap)
{
    uint32_t i = 0;
    uint32_t o = 0;

    if (src == NULL || dst == NULL || src_len == 0) {
        return 0;
    }

    while (i < src_len) {
        uint8_t tag = src[i++];

        if (tag == ZCOMP_TAG_END) {
            break;
        } else if (tag == ZCOMP_TAG_LITERAL) {
            uint8_t cnt;

            if (i >= src_len) return 0;
            cnt = src[i++];
            if (o + cnt > dst_cap || i + cnt > src_len) return 0;
            memcpy(dst + o, src + i, cnt);
            i += cnt;
            o += cnt;
        } else if (tag == ZCOMP_TAG_MATCH) {
            uint32_t offset;
            uint32_t len;
            uint32_t k;

            if (i + 3U > src_len) return 0;
            offset = (uint32_t) src[i] | ((uint32_t) (src[i + 1] & 0x0Fu) << 8);
            len = (uint32_t) src[i + 2] + ZCOMP_MIN_MATCH;
            i += 3U;
            if (offset == 0 || offset > o || o + len > dst_cap) {
                return 0;
            }
            for (k = 0; k < len; k++) {
                dst[o] = dst[o - offset];  /* 逐字节复制，支持重叠 */
                o++;
            }
        } else {
            return 0;  /* 非法 tag */
        }
    }
    return o;
}

/* ============================================================
 *  页跟踪 / 回收
 * ============================================================ */

static zcomp_slot_t *zcomp_find_slot(uint64_t vaddr)
{
    for (uint32_t i = 0; i < ZCOMP_MAX_PAGES; i++) {
        if (g_slots[i].state != ZCOMP_PAGE_FREE && g_slots[i].vaddr == vaddr) {
            return &g_slots[i];
        }
    }
    return NULL;
}

void zcomp_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_info, 0, sizeof(g_info));
    strcpy(g_status, "zcomp: ready");

    g_pool = zs_create_pool("zcomp", ZS_COMP_LZ4);
    if (g_pool != NULL) {
        g_info.pool_ready = true;
    }
    g_info.enabled = true;
}

int32_t zcomp_track(uint64_t vaddr, const void *page_data)
{
    zcomp_slot_t *slot;

    (void) page_data;
    if (vaddr == 0) {
        return -1;
    }
    slot = zcomp_find_slot(vaddr);
    if (slot != NULL) {
        return (int32_t) (slot - g_slots);
    }
    for (uint32_t i = 0; i < ZCOMP_MAX_PAGES; i++) {
        if (g_slots[i].state == ZCOMP_PAGE_FREE) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            g_slots[i].vaddr = vaddr;
            g_slots[i].state = ZCOMP_PAGE_ACTIVE;
            g_slots[i].referenced = true;
            g_info.tracked_pages++;
            return (int32_t) i;
        }
    }
    g_info.failures++;
    return -1;
}

void zcomp_touch(uint64_t vaddr)
{
    zcomp_slot_t *slot = zcomp_find_slot(vaddr);

    if (slot != NULL) {
        slot->referenced = true;
    }
}

bool zcomp_compress_page(uint64_t vaddr, const void *page_data)
{
    zcomp_slot_t *slot;
    uint8_t *scratch;
    uint32_t comp_len;

    if (!g_info.enabled || page_data == NULL || g_pool == NULL) {
        return false;
    }
    slot = zcomp_find_slot(vaddr);
    if (slot == NULL) {
        /* 未跟踪则自动注册 */
        int32_t idx = zcomp_track(vaddr, page_data);

        if (idx < 0) {
            return false;
        }
        slot = &g_slots[idx];
    }
    if (slot->state == ZCOMP_PAGE_COMPRESSED) {
        return true;  /* 已经压缩过 */
    }

    scratch = (uint8_t *) kmalloc(ZCOMP_PAGE_SIZE + 64U);
    if (scratch == NULL) {
        g_info.failures++;
        return false;
    }

    comp_len = zcomp_lz_encode((const uint8_t *) page_data, ZCOMP_PAGE_SIZE,
                               scratch, ZCOMP_PAGE_SIZE + 64U);
    if (comp_len == 0 || comp_len >= ZCOMP_PAGE_SIZE) {
        /* 压缩没收益，保持 active */
        kfree(scratch);
        slot->state = ZCOMP_PAGE_ACTIVE;
        return false;
    }

    {
        void *handle = zs_malloc(g_pool, comp_len);
        if (handle == NULL) {
            kfree(scratch);
            g_info.failures++;
            return false;
        }
        memcpy(handle, scratch, comp_len);
        kfree(scratch);

        if (slot->zs_handle != NULL) {
            zs_free(g_pool, slot->zs_handle);
        }
        slot->zs_handle = handle;
        slot->orig_size = ZCOMP_PAGE_SIZE;
        slot->comp_size = comp_len;
        slot->state = ZCOMP_PAGE_COMPRESSED;
        slot->referenced = false;

        g_info.compress_ops++;
        g_info.compressed_pages++;
        g_info.total_original_bytes += ZCOMP_PAGE_SIZE;
        g_info.total_compressed_bytes += comp_len;
        if (g_info.total_original_bytes > 0) {
            g_info.last_ratio = (float) g_info.total_compressed_bytes /
                                (float) g_info.total_original_bytes;
        }
    }
    strcpy(g_status, "zcomp: page compressed");
    return true;
}

bool zcomp_decompress_page(uint64_t vaddr, void *out_buffer)
{
    zcomp_slot_t *slot;
    uint32_t comp_len;
    uint32_t out_len;

    if (out_buffer == NULL) {
        return false;
    }
    slot = zcomp_find_slot(vaddr);
    if (slot == NULL || slot->state != ZCOMP_PAGE_COMPRESSED || slot->zs_handle == NULL) {
        return false;
    }

    comp_len = zs_malloc_usable_size(slot->zs_handle);
    if (comp_len == 0) {
        return false;
    }
    out_len = zcomp_lz_decode((const uint8_t *) slot->zs_handle, comp_len,
                              (uint8_t *) out_buffer, ZCOMP_PAGE_SIZE);
    if (out_len != ZCOMP_PAGE_SIZE) {
        g_info.failures++;
        return false;
    }

    /* 解压后释放压缩副本，槽位回到 free（页已驻留） */
    zs_free(g_pool, slot->zs_handle);
    slot->zs_handle = NULL;
    slot->state = ZCOMP_PAGE_FREE;
    slot->vaddr = 0;
    if (g_info.compressed_pages > 0) {
        g_info.compressed_pages--;
    }
    g_info.tracked_pages = (g_info.tracked_pages > 0) ? g_info.tracked_pages - 1U : 0U;
    g_info.decompress_ops++;
    strcpy(g_status, "zcomp: page decompressed");
    return true;
}

uint32_t zcomp_reclaim_pass(void)
{
    uint32_t compressed = 0;

    if (!g_info.enabled) {
        return 0;
    }
    g_info.reclaim_passes++;

    for (uint32_t i = 0; i < ZCOMP_MAX_PAGES; i++) {
        zcomp_slot_t *slot = &g_slots[i];

        if (slot->state != ZCOMP_PAGE_ACTIVE) {
            continue;
        }
        if (slot->referenced) {
            /* 一轮引用清除：下次再回收 */
            slot->referenced = false;
            continue;
        }
        /* 未引用：从 vaddr 读取驻留页并压缩 */
        if (zcomp_compress_page(slot->vaddr, (const void *) slot->vaddr)) {
            compressed++;
        }
    }
    return compressed;
}

const zcomp_info_t *zcomp_info(void)
{
    return &g_info;
}

const char *zcomp_status(void)
{
    return g_status;
}

uint64_t zcomp_ctl(uint64_t cmd, uint64_t arg1, uint64_t arg2)
{
    (void) arg2;

    switch (cmd) {
    case ZCOMP_CTL_STATUS: {
        zcomp_info_t *out = (zcomp_info_t *) arg1;
        if (out != NULL) {
            *out = g_info;
        }
        return 0;
    }
    case ZCOMP_CTL_RECLAIM:
        return (uint64_t) zcomp_reclaim_pass();
    case ZCOMP_CTL_ENABLE:
        g_info.enabled = (arg1 != 0U);
        return 0;
    case ZCOMP_CTL_DROP:
        for (uint32_t i = 0; i < ZCOMP_MAX_PAGES; i++) {
            if (g_slots[i].state == ZCOMP_PAGE_COMPRESSED && g_slots[i].zs_handle != NULL) {
                zs_free(g_pool, g_slots[i].zs_handle);
                g_slots[i].zs_handle = NULL;
            }
            g_slots[i].state = ZCOMP_PAGE_FREE;
        }
        g_info.compressed_pages = 0;
        return 0;
    default:
        return (uint64_t) -1;
    }
}
