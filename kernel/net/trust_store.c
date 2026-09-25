#include "common.h"
#include "file.h"
#include "kernel.h"
#include "memory.h"
#include "trust_store.h"

#define TRUST_STORE_MAGIC "MONIOS-TRUST-ROOTS1"
#define TRUST_STORE_MAGIC_SIZE ((uint32_t) (sizeof(TRUST_STORE_MAGIC) - 1U))
#define TRUST_STORE_HEADER_SIZE (TRUST_STORE_MAGIC_SIZE + 4U)
#define TRUST_STORE_RECORD_HEADER_SIZE 6U

/* bundle 一次性读进内存的上限。当前 assets/cent 下 556 张根证书约 676KB，
 * 4MB 足够宽裕，同时挡住被污染的超大文件把内核堆吃光。 */
#define TRUST_STORE_MAX_BUNDLE_BYTES (4U * 1024U * 1024U)

/* 每条记录最少占 6 字节（6 字节记录头 + 0 长度名字 + 0 长度数据），
 * 用它把 record_count 约束进文件实际大小，避免损坏的头导致超长循环。 */
#define TRUST_STORE_MIN_RECORD_BYTES TRUST_STORE_RECORD_HEADER_SIZE

/* codeSigning 扩展密钥用法：id-kp-codeSigning = 1.3.6.1.5.5.7.3.3
 * （OID 长 8，外层 06 08） */
#define TRUST_STORE_CODE_SIGNING_OID_LEN 10U

static const uint8_t g_trust_store_code_signing_oid[TRUST_STORE_CODE_SIGNING_OID_LEN] = {
    0x06U, 0x08U, 0x2BU, 0x06U, 0x01U, 0x05U, 0x05U, 0x07U, 0x03U, 0x03U
};

static x509_trust_store_t g_trust_store;
static bool g_trust_store_ready;
static uint32_t g_trust_store_certificate_count;
static uint32_t g_trust_store_parsed_count;

static uint16_t trust_store_read_u16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t trust_store_read_u32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

/* 在证书 DER 里查找 codeSigning 的 id-kp OID。
 *
 * 这个判定**只用来决定装载顺序**，不参与任何可信性判断——可信判定始终是
 * 完整 ASN.1 解析加链上签名校验。误判的后果仅是某张根证书提前或延后装载，
 * 不会放宽信任。
 *
 * 之所以需要它：x509_trust_store_t 的容量远小于 bundle 里的根证书数量，
 * 早先的实现在容量满后静默丢弃剩余记录，只能靠 "签名证书被命名为全零因而
 * 排在最前" 这一隐含约定保证签名根一定被装上。现在按 OID 把带 codeSigning
 * 扩展的根排到第一遍装载，签名根是否装上只取决于它自己的扩展字段。 */
static bool trust_store_is_code_signing_root(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (len < TRUST_STORE_CODE_SIGNING_OID_LEN) {
        return false;
    }
    for (i = 0; i + TRUST_STORE_CODE_SIGNING_OID_LEN <= len; i++) {
        if (memcmp(data + i, g_trust_store_code_signing_oid,
                   TRUST_STORE_CODE_SIGNING_OID_LEN) == 0) {
            return true;
        }
    }
    return false;
}

/* 校验 bundle 头，取出记录数。bundle 必须已完整读入内存。 */
static bool trust_store_read_bundle_header(const uint8_t *bundle, uint32_t bundle_size,
                                           uint32_t *out_record_count)
{
    uint32_t record_count;
    uint32_t max_records;

    if (bundle_size < TRUST_STORE_HEADER_SIZE ||
        memcmp(bundle, TRUST_STORE_MAGIC, TRUST_STORE_MAGIC_SIZE) != 0) {
        return false;
    }
    record_count = trust_store_read_u32(bundle + TRUST_STORE_MAGIC_SIZE);
    max_records = (bundle_size - TRUST_STORE_HEADER_SIZE) / TRUST_STORE_MIN_RECORD_BYTES;
    if (record_count > max_records) {
        kernel_log_hex_u32("trust-store: record count ", record_count);
        kernel_log_hex_u32("trust-store: max records ", max_records);
        return false;
    }
    *out_record_count = record_count;
    return true;
}

/* 遍历全部记录，按 want_code_signing 筛选并装载，直到容量满或记录走完。
 * 返回本次实际新装的根证书数量。 */
static uint32_t trust_store_load_pass(const uint8_t *bundle, uint32_t bundle_size,
                                      uint32_t record_count, bool want_code_signing)
{
    uint32_t offset = TRUST_STORE_HEADER_SIZE;
    uint32_t loaded = 0;

    for (uint32_t i = 0; i < record_count; i++) {
        uint16_t name_size;
        uint32_t data_size;
        uint32_t data_offset;

        if (offset > bundle_size ||
            bundle_size - offset < TRUST_STORE_RECORD_HEADER_SIZE) {
            break;
        }
        name_size = trust_store_read_u16(bundle + offset);
        data_size = trust_store_read_u32(bundle + offset + 2U);
        data_offset = offset + TRUST_STORE_RECORD_HEADER_SIZE + (uint32_t) name_size;
        if (data_offset > bundle_size || data_size > bundle_size - data_offset) {
            break;
        }
        offset = data_offset + data_size;

        if (g_trust_store.count >= X509_MAX_TRUSTED_ROOTS) {
            break;
        }
        if (data_size == 0 || data_size > X509_MAX_CERT_SIZE) {
            continue;
        }
        if (trust_store_is_code_signing_root(bundle + data_offset, data_size) !=
            want_code_signing) {
            continue;
        }
        if (x509_add_trusted_root(&g_trust_store, bundle + data_offset, data_size) == 0) {
            loaded++;
        }
    }
    return loaded;
}

bool trust_store_load(const char *path)
{
    uint8_t *bundle;
    int32_t bundle_size;
    uint32_t record_count;
    uint32_t code_signing_roots;
    uint32_t other_roots;

    g_trust_store_ready = false;
    g_trust_store_certificate_count = 0;
    g_trust_store_parsed_count = 0;
    x509_init_trust_store(&g_trust_store);
    if (path == NULL || path[0] == '\0' || !file_exists(path)) {
        log_write("trust-store: bundle not found");
        return false;
    }
    if (file_is_dir(path)) {
        log_write("trust-store: bundle path is a directory");
        return false;
    }
    bundle_size = file_size(path);
    if (bundle_size < (int32_t) TRUST_STORE_HEADER_SIZE) {
        log_write("trust-store: bundle size invalid");
        return false;
    }
    if ((uint32_t) bundle_size > TRUST_STORE_MAX_BUNDLE_BYTES) {
        kernel_log_hex_u32("trust-store: bundle too large ", (uint32_t) bundle_size);
        return false;
    }

    /* 整个 bundle 只读一次盘，后续解析、分类、装载全在内存里做。
     * 早先的实现对每条记录各做一次 file_read_at，556 条记录就是上千次
     * 路径解析 + 读缓存查找，既慢又没法回头做第二遍筛选。 */
    bundle = (uint8_t *) kmalloc((uint32_t) bundle_size);
    if (bundle == NULL) {
        log_write("trust-store: bundle buffer unavailable");
        return false;
    }
    if (file_read(path, bundle, (uint32_t) bundle_size) != bundle_size) {
        kfree(bundle);
        log_write("trust-store: bundle read failed");
        return false;
    }
    if (!trust_store_read_bundle_header(bundle, (uint32_t) bundle_size, &record_count)) {
        kfree(bundle);
        log_write("trust-store: bundle header invalid");
        return false;
    }

    /* 两遍装载：第一遍只收带 codeSigning 扩展密钥用法的根，第二遍用剩余槽位
     * 收普通根。这样签名根一定排在最前，不再依赖文件命名约定。 */
    code_signing_roots = trust_store_load_pass(bundle, (uint32_t) bundle_size,
                                               record_count, true);
    other_roots = trust_store_load_pass(bundle, (uint32_t) bundle_size,
                                        record_count, false);
    kfree(bundle);

    g_trust_store_certificate_count = record_count;
    g_trust_store_parsed_count = code_signing_roots + other_roots;
    kernel_log_hex_u32("trust-store: records ", record_count);
    kernel_log_hex_u32("trust-store: code-signing roots ", code_signing_roots);
    kernel_log_hex_u32("trust-store: parsed roots ", g_trust_store_parsed_count);
    if (code_signing_roots == 0) {
        /* 驱动 .sys 与 kernel.exe 都靠 codeSigning 根验签，一个都没装上说明
         * bundle 内容或分类逻辑出了问题，必须在启动日志里显式暴露。 */
        log_write("trust-store: warning - no code-signing root loaded");
    }
    if (g_trust_store_parsed_count < record_count) {
        kernel_log_hex_u32("trust-store: roots skipped ",
                           record_count - g_trust_store_parsed_count);
        log_write(g_trust_store.count >= X509_MAX_TRUSTED_ROOTS ?
                  "trust-store: warning - root capacity reached, extra roots skipped" :
                  "trust-store: warning - some roots failed to parse");
    }
    g_trust_store_ready = g_trust_store_certificate_count > 0 &&
                          g_trust_store_parsed_count > 0;
    log_write(g_trust_store_ready ? "trust-store: roots loaded" :
              "trust-store: no parseable roots");
    return g_trust_store_ready;
}

bool trust_store_ready(void)
{
    return g_trust_store_ready;
}

uint32_t trust_store_certificate_count(void)
{
    return g_trust_store_certificate_count;
}

uint32_t trust_store_parsed_count(void)
{
    return g_trust_store_parsed_count;
}

const x509_trust_store_t *trust_store_get(void)
{
    return g_trust_store_ready ? &g_trust_store : NULL;
}
