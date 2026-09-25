#ifndef _EFS_H_
#define _EFS_H_

#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  Monios EFS 加密文件系统层 (security group)
 *
 *  - 使用 kernel/net/aes.c 的 AES-128-CBC 做透明加解密。
 *  - 磁盘格式：魔数 "MONIOS-EFS-1" (12B) + IV (16B) + CBC 密文。
 *  - 透明性：file.c 的读/写路径通过注册钩子自动加解密。
 *  - 密钥：固定主密钥 + 当前用户名派生 AES 密钥（基础框架）。
 *
 *  未调用 efs_init() 前 file.c 钩子为 NULL，所有文件按明文处理，
 *  不影响现有 VFS 行为与 119 项回归测试。
 * ============================================================ */

/* 初始化：注册 file.c 的加解密钩子。 */
void efs_init(void);

/* 设置当前用户名（用于密钥派生）。 */
void efs_set_user(const char *username);

/* 一次性把指定明文文件加密落盘，并标记为 EFS 文件。 */
bool efs_encrypt_file(const char *path);

/* 一次性把 EFS 文件解密回明文，并清除标记。 */
bool efs_decrypt_file(const char *path);

/* 该路径是否被标记为 EFS 加密文件。 */
bool efs_is_encrypted(const char *path);

/* 以下三个函数是注册到 file.c 的透明加解密回调，一般不直接调用。 */
uint32_t efs_encrypt_buffer(const char *path, const uint8_t *in, uint32_t in_len,
                             uint8_t *out, uint32_t out_cap);
uint32_t efs_decrypt_buffer(const char *path, const uint8_t *in, uint32_t in_len,
                             uint8_t *out, uint32_t out_cap);

#endif /* _EFS_H_ */
