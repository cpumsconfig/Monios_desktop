#ifndef _NETFS_H_
#define _NETFS_H_

#include "stdbool.h"
#include "stdint.h"

/* ============================================================
 *  Monios Network File System (MNFS) client
 *
 *  A simplified TCP file-system client used to map a remote share
 *  to a local drive letter (e.g. Z: -> \\server\share). The wire
 *  protocol is a tiny length-framed request/response protocol:
 *
 *    request : [cmd:1][path_len:2 LE][path][offset:4 LE][length:4 LE][payload:length]
 *              (payload bytes only follow for the WRITE command)
 *    response: [status:1][data_len:4 LE][data:data_len]
 *
 *  status: 0 = OK, 1 = logical failure (e.g. not found), 2 = error.
 *
 *  The client keeps a single global TCP session. All operations are
 *  non-blocking: a bounded pump loop (net_update + io_wait) is used
 *  while waiting for the response, so a dead/unreachable server can
 *  never hang the kernel forever.
 * ============================================================ */

#define NETFS_DEFAULT_PORT 445u

/* 命令码 */
#define NETFS_CMD_EXISTS    0x01u
#define NETFS_CMD_READ      0x02u
#define NETFS_CMD_WRITE     0x03u
#define NETFS_CMD_DELETE    0x04u
#define NETFS_CMD_LIST      0x05u
#define NETFS_CMD_SIZE      0x06u
#define NETFS_CMD_IS_DIR    0x07u
#define NETFS_CMD_MKDIR     0x08u
#define NETFS_CMD_RMDIR     0x09u

/* 响应状态码 */
#define NETFS_STATUS_OK     0x00u
#define NETFS_STATUS_FAIL   0x01u
#define NETFS_STATUS_ERROR  0x02u

/* 建立到 server:port\share 的会话。失败（无法连接/超时）返回 false。 */
bool netfs_init(const char *server, uint16_t port, const char *share);

bool netfs_exists(const char *path);
bool netfs_is_dir(const char *path);
int32_t netfs_file_size(const char *path);
int32_t netfs_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t netfs_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t netfs_write_file(const char *path, const void *buffer, uint32_t size);
bool netfs_delete(const char *path);
bool netfs_mkdir(const char *path);
bool netfs_rmdir(const char *path);
bool netfs_list_dir(const char *path, char *buffer, uint32_t buffer_size);

/* 当前会话是否已连接（会探测底层 socket 状态）。 */
bool netfs_connected(void);

/* 关闭并释放当前会话。 */
void netfs_disconnect(void);

#endif
