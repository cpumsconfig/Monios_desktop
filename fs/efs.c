/* ============================================================
 *  Monios EFS 加密文件系统层实现
 *  fs/efs.c
 *
 *  设计要点：
 *   - 磁盘格式：魔数(12B) + IV(16B) + AES-128-CBC 密文。
 *   - efs_init() 通过 file_efs_set_hooks() 把加密/解密回调注册到
 *     lib/file.c；file.c 在钩子为 NULL 时按明文读写，故未链接本文件
 *     或未调用 init 时行为完全不变。
 *   - 加密文件清单保存在内存中（基础框架），重启动后需重新标记。
 *   - 密钥派生：固定主密钥 + 当前用户名做确定性混合（基础框架）。
 * ============================================================ */

#include "aes.h"
#include "common.h"
#include "efs.h"
#include "file.h"
#include "string.h"

#define EFS_MAGIC        "MONIOS-EFS-1"
#define EFS_MAGIC_LEN    12U
#define EFS_MAX_FILES    16U
#define EFS_MAX_PATH     256U

/* 固定主密钥（基础框架；生产环境应来自用户登录口令派生） */
static const uint8_t efs_master_key[16] = {
    0x4D, 0x6F, 0x6E, 0x69, 0x6F, 0x73, 0x45, 0x46,
    0x53, 0x4B, 0x65, 0x79, 0x32, 0x30, 0x32, 0x36
};

static char g_user[64] = "default";

static char g_encrypted[EFS_MAX_FILES][EFS_MAX_PATH];
static uint32_t g_encrypted_count = 0;

/* ------------------------------------------------------------
 *  密钥派生：把主密钥与用户名混合成 16 字节 AES 密钥。
 *  基础框架：逐字节 XOR + 移位混合，确定性即可。
 * ------------------------------------------------------------ */
static void efs_derive_key(uint8_t out[16])
{
    uint32_t i;

    memcpy(out, efs_master_key, 16);
    for (i = 0; g_user[i] != '\0' && i < sizeof(g_user); i++) {
        out[i & 15] ^= (uint8_t) g_user[i];
        out[(i + 5) & 15] = (uint8_t) ((out[(i + 5) & 15] << 3) |
                                        (out[(i + 5) & 15] >> 5));
    }
    /* 再做一轮扩散 */
    for (i = 0; i < 16; i++) {
        out[i] ^= out[(i + 7) & 15];
        out[i] = (uint8_t) (out[i] * 33 + i);
    }
}

/* 由路径派生一个确定性 IV（基础框架：无 RNG 时用路径哈希） */
static void efs_derive_iv(const char *path, uint8_t iv[16])
{
    uint32_t i;
    uint32_t h = 0x811C9DC5u;

    for (i = 0; i < 16; i++) {
        iv[i] = 0;
    }
    while (path != NULL && *path != '\0') {
        h ^= (uint8_t) *path;
        h *= 0x01000193u;
        path++;
    }
    for (i = 0; i < 16; i++) {
        iv[i] = (uint8_t) (h >> ((i & 3) * 8));
        h = h * 2654435761u + (uint32_t) i;
    }
}

/* ------------------------------------------------------------
 *  加密/解密缓冲（注册到 file.c 的透明回调）
 * ------------------------------------------------------------ */
uint32_t efs_encrypt_buffer(const char *path, const uint8_t *in, uint32_t in_len,
                            uint8_t *out, uint32_t out_cap)
{
    aes_ctx_t ctx;
    uint8_t key[16];
    uint8_t iv[16];
    uint32_t padded;
    uint32_t total;
    uint32_t i;

    if (in == NULL || out == NULL) {
        return 0;
    }
    /* PKCS#7 风格补零到 16 字节边界 */
    padded = (in_len + 15U) & ~15U;
    if (padded == 0U) {
        padded = 16U;
    }
    total = EFS_MAGIC_LEN + AES_BLOCK_SIZE + padded;
    if (total > out_cap) {
        return 0;
    }

    memcpy(out, EFS_MAGIC, EFS_MAGIC_LEN);
    efs_derive_iv(path, iv);
    memcpy(out + EFS_MAGIC_LEN, iv, AES_BLOCK_SIZE);

    efs_derive_key(key);
    if (aes_init(&ctx, key, AES_KEY_SIZE_128) != 0) {
        return 0;
    }
    aes_set_iv(&ctx, iv);

    /* 用 CBC 一次性加密 padded 明文。构造临时 padded 缓冲。 */
    {
        uint8_t padded_buf[4096];
        if (padded > sizeof(padded_buf)) {
            return 0;
        }
        memset(padded_buf, 0, padded);
        memcpy(padded_buf, in, in_len);
        if (aes_encrypt_cbc(&ctx, padded_buf, padded,
                            out + EFS_MAGIC_LEN + AES_BLOCK_SIZE) != 0) {
            return 0;
        }
    }
    for (i = 0; i < 16; i++) {
        key[i] = 0;
    }
    return total;
}

uint32_t efs_decrypt_buffer(const char *path, const uint8_t *in, uint32_t in_len,
                             uint8_t *out, uint32_t out_cap)
{
    aes_ctx_t ctx;
    uint8_t key[16];
    uint8_t iv[16];
    uint32_t ct_len;
    uint32_t plain_len;
    uint32_t i;

    (void) path;

    if (in == NULL || out == NULL) {
        return 0;
    }
    if (in_len < EFS_MAGIC_LEN + AES_BLOCK_SIZE) {
        return 0;
    }
    if (memcmp(in, EFS_MAGIC, EFS_MAGIC_LEN) != 0) {
        return 0;  /* 不是 EFS 文件 */
    }
    ct_len = in_len - EFS_MAGIC_LEN - AES_BLOCK_SIZE;
    if ((ct_len & 15U) != 0U) {
        return 0;
    }
    if (ct_len > out_cap) {
        return 0;
    }

    memcpy(iv, in + EFS_MAGIC_LEN, AES_BLOCK_SIZE);
    efs_derive_key(key);
    if (aes_init(&ctx, key, AES_KEY_SIZE_128) != 0) {
        return 0;
    }
    aes_set_iv(&ctx, iv);

    if (aes_decrypt_cbc(&ctx, in + EFS_MAGIC_LEN + AES_BLOCK_SIZE, ct_len, out) != 0) {
        return 0;
    }

    /* 去除尾部补零：从尾部回退到最后一个非零字节 */
    plain_len = ct_len;
    while (plain_len > 0 && out[plain_len - 1] == 0) {
        plain_len--;
    }
    for (i = 0; i < 16; i++) {
        key[i] = 0;
    }
    return plain_len;
}

/* ------------------------------------------------------------
 *  加密文件标记表
 * ------------------------------------------------------------ */
static bool efs_mark(const char *path)
{
    uint32_t i;

    if (path == NULL) {
        return false;
    }
    for (i = 0; i < g_encrypted_count; i++) {
        if (strcmp(g_encrypted[i], path) == 0) {
            return true;
        }
    }
    if (g_encrypted_count >= EFS_MAX_FILES) {
        return false;
    }
    strlcpy(g_encrypted[g_encrypted_count], path, EFS_MAX_PATH);
    g_encrypted_count++;
    return true;
}

static void efs_unmark(const char *path)
{
    uint32_t i;

    for (i = 0; i < g_encrypted_count; i++) {
        if (strcmp(g_encrypted[i], path) == 0) {
            /* 用最后一项覆盖 */
            g_encrypted_count--;
            if (i < g_encrypted_count) {
                strlcpy(g_encrypted[i], g_encrypted[g_encrypted_count], EFS_MAX_PATH);
            }
            g_encrypted[g_encrypted_count][0] = '\0';
            return;
        }
    }
}

bool efs_is_encrypted(const char *path)
{
    uint32_t i;

    if (path == NULL) {
        return false;
    }
    for (i = 0; i < g_encrypted_count; i++) {
        if (strcmp(g_encrypted[i], path) == 0) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------
 *  一次性加密/解密整个文件（shell 命令调用）
 * ------------------------------------------------------------ */
#define EFS_ONESHOT_BUF 65536U

bool efs_encrypt_file(const char *path)
{
    static uint8_t plain[EFS_ONESHOT_BUF];
    static uint8_t cipher[EFS_ONESHOT_BUF + 64];
    int32_t size;
    uint32_t ct_len;

    if (path == NULL) {
        return false;
    }
    size = file_size(path);
    if (size <= 0 || (uint32_t) size > EFS_ONESHOT_BUF) {
        return false;
    }
    if (file_read(path, plain, (uint32_t) size) != size) {
        return false;
    }
    ct_len = efs_encrypt_buffer(path, plain, (uint32_t) size,
                                cipher, sizeof(cipher));
    if (ct_len == 0) {
        return false;
    }
    if (file_write(path, cipher, ct_len) != (int32_t) size) {
        return false;
    }
    return efs_mark(path);
}

bool efs_decrypt_file(const char *path)
{
    static uint8_t cipher[EFS_ONESHOT_BUF + 64];
    static uint8_t plain[EFS_ONESHOT_BUF];
    int32_t size;
    uint32_t pt_len;

    if (path == NULL) {
        return false;
    }
    size = file_size(path);
    if (size <= 0 || (uint32_t) size > sizeof(cipher)) {
        return false;
    }
    if (file_read(path, cipher, (uint32_t) size) != size) {
        return false;
    }
    pt_len = efs_decrypt_buffer(path, cipher, (uint32_t) size,
                               plain, sizeof(plain));
    if (pt_len == 0) {
        return false;
    }
    if (file_write(path, plain, pt_len) != (int32_t) pt_len) {
        return false;
    }
    efs_unmark(path);
    return true;
}

void efs_set_user(const char *username)
{
    if (username == NULL) {
        return;
    }
    strlcpy(g_user, username, sizeof(g_user));
}

void efs_init(void)
{
    g_encrypted_count = 0;
    /* 注册透明加解密钩子到 file.c。未注册前 file.c 按明文处理。 */
    file_efs_set_hooks(efs_is_encrypted, efs_encrypt_buffer, efs_decrypt_buffer);
}
