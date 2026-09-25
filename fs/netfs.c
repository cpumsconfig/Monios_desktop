/* ============================================================
 *  fs/netfs.c — Monios Network File System (MNFS) TCP client
 *
 *  See include/netfs.h for the wire format. This driver keeps a
 *  single global TCP session and exposes the same shape of API as
 *  fat32.c so the VFS layer (lib/file.c) can dispatch to it for a
 *  drive mounted as FS_TYPE_NETFS.
 *
 *  Robustness rules:
 *   - If the socket is not open/connected, every operation fails
 *     immediately instead of dereferencing a dead handle.
 *   - Receiving uses a bounded pump loop (net_update() + io_wait());
 *     it never busy-loops forever.
 * ============================================================ */

#include "common.h"
#include "netfs.h"
#include "net.h"
#include "socket.h"
#include "string.h"

/* ============================================================
 *  内部状态
 * ============================================================ */

static int32_t g_nf_handle = -1;
static char    g_nf_server[64];
static uint16_t g_nf_port = 0;
static char    g_nf_share[64];
static bool    g_nf_session_up = false;

/* 连接等待 / 接收等待的 pump 次数上限（与 shell 中 socket 连接循环同量级）。 */
#define NF_CONNECT_WAIT 250000u
#define NF_RECV_WAIT    300000u

#define NF_MAX_PATH     256u
#define NF_REQ_FIXED    (1u + 2u + 4u + 4u)   /* cmd + pathlen + offset + length */
#define NF_RESP_FIXED   (1u + 4u)             /* status + data_len */

/* ============================================================
 *  小端编码辅助（与本协议其它字段一致；x86-64 原生小端）
 * ============================================================ */

static void nf_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) ((v >> 8) & 0xFFu);
}

static void nf_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v & 0xFFu);
    p[1] = (uint8_t) ((v >> 8) & 0xFFu);
    p[2] = (uint8_t) ((v >> 16) & 0xFFu);
    p[3] = (uint8_t) ((v >> 24) & 0xFFu);
}

static uint32_t nf_get_le32(const uint8_t *p)
{
    return ((uint32_t) p[0]) |
           ((uint32_t) p[1] << 8) |
           ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* ============================================================
 *  接收：非阻塞地精确收满 len 字节
 *  返回 true 成功；false = 连接断开或超时
 * ============================================================ */

static bool nf_recv_full(uint8_t *out, uint32_t len)
{
    uint32_t got = 0;
    uint32_t waited = 0;

    if (g_nf_handle < 0) {
        return false;
    }

    while (got < len) {
        uint16_t chunk = (uint16_t) ((len - got > 0xFFFFu) ? 0xFFFFu : (len - got));
        int32_t n = socket_tcp_recv(g_nf_handle, out + got, chunk);

        if (n > 0) {
            got += (uint32_t) n;
            waited = 0;
            continue;
        }
        if (n < 0) {
            g_nf_session_up = false;
            return false;
        }

        /* n == 0：暂无可读数据，驱动网络栈后稍候重试（有界）。 */
        net_update();
        io_wait();
        if (++waited > NF_RECV_WAIT) {
            return false;
        }
    }
    return true;
}

/* 丢弃 n 字节（响应数据比调用方缓冲区大时，保持 TCP 流同步）。 */
static bool nf_drain(uint32_t n)
{
    uint8_t scratch[256];

    while (n > 0u) {
        uint32_t chunk = (n > sizeof(scratch)) ? (uint32_t) sizeof(scratch) : n;
        if (!nf_recv_full(scratch, chunk)) {
            return false;
        }
        n -= chunk;
    }
    return true;
}

/* ============================================================
 *  发送请求并接收响应
 *
 *  返回值：
 *     -1  传输错误 / 未连接 / 超时
 *     -2  服务器返回 status != OK（逻辑失败，如不存在）
 *   >= 0  成功：写入 resp_data 的字节数（被 resp_cap 截断）
 * ============================================================ */

static int32_t nf_transact(uint8_t cmd, const char *path,
                           uint32_t offset, uint32_t length,
                           const void *payload, uint16_t payload_len,
                           uint8_t *resp_data, uint32_t resp_cap)
{
    uint8_t hdr[NF_REQ_FIXED + NF_MAX_PATH];
    uint32_t plen;
    uint32_t i = 0;
    uint8_t rhdr[NF_RESP_FIXED];
    uint8_t status;
    uint32_t rlen;

    if (!g_nf_session_up || g_nf_handle < 0) {
        return -1;
    }
    if (path == NULL) {
        path = "/";
    }

    plen = (uint32_t) strlen(path);
    if (plen > NF_MAX_PATH) {
        return -1;
    }

    hdr[i++] = cmd;
    nf_put_le16(hdr + i, (uint16_t) plen);
    i += 2u;
    memcpy(hdr + i, path, plen);
    i += plen;
    nf_put_le32(hdr + i, offset);
    i += 4u;
    nf_put_le32(hdr + i, length);
    i += 4u;

    if (socket_tcp_send(g_nf_handle, hdr, (uint16_t) i) != (int32_t) i) {
        g_nf_session_up = false;
        return -1;
    }
    /* WRITE 的负载紧随请求头之后（同一 TCP 字节流）。 */
    if (payload != NULL && payload_len > 0u) {
        if (socket_tcp_send(g_nf_handle, (const uint8_t *) payload, payload_len) != (int32_t) payload_len) {
            g_nf_session_up = false;
            return -1;
        }
    }

    if (!nf_recv_full(rhdr, sizeof(rhdr))) {
        return -1;
    }
    status = rhdr[0];
    rlen = nf_get_le32(rhdr + 1);

    if (status != NETFS_STATUS_OK) {
        /* 丢弃服务器可能附带的数据，保持字节流同步。 */
        if (rlen > 0u) {
            (void) nf_drain(rlen);
        }
        return -2;
    }

    if (rlen == 0u) {
        return 0;
    }

    if (resp_data == NULL) {
        return nf_drain(rlen) ? (int32_t) rlen : -1;
    }

    {
        uint32_t cap = (rlen > resp_cap) ? resp_cap : rlen;

        if (!nf_recv_full(resp_data, cap)) {
            return -1;
        }
        if (rlen > cap) {
            if (!nf_drain(rlen - cap)) {
                return -1;
            }
        }
        return (int32_t) cap;
    }
}

/* ============================================================
 *  公共 API
 * ============================================================ */

bool netfs_init(const char *server, uint16_t port, const char *share)
{
    uint32_t waited = 0;

    netfs_disconnect();

    if (server == NULL || server[0] == '\0' || share == NULL) {
        return false;
    }
    if (port == 0u) {
        port = NETFS_DEFAULT_PORT;
    }

    g_nf_handle = socket_tcp_open(0);
    if (g_nf_handle < 0) {
        return false;
    }

    if (!socket_tcp_connect(g_nf_handle, server, port)) {
        (void) socket_close(g_nf_handle);
        g_nf_handle = -1;
        return false;
    }

    while (waited < NF_CONNECT_WAIT && !socket_tcp_is_connected(g_nf_handle)) {
        net_update();
        io_wait();
        waited++;
    }
    if (!socket_tcp_is_connected(g_nf_handle)) {
        (void) socket_close(g_nf_handle);
        g_nf_handle = -1;
        return false;
    }

    g_nf_session_up = true;
    (void) strlcpy(g_nf_server, server, sizeof(g_nf_server));
    g_nf_port = port;
    (void) strlcpy(g_nf_share, share, sizeof(g_nf_share));
    return true;
}

bool netfs_exists(const char *path)
{
    return nf_transact(NETFS_CMD_EXISTS, path, 0, 0, NULL, 0, NULL, 0) >= 0;
}

bool netfs_is_dir(const char *path)
{
    return nf_transact(NETFS_CMD_IS_DIR, path, 0, 0, NULL, 0, NULL, 0) >= 0;
}

int32_t netfs_file_size(const char *path)
{
    uint8_t buf[4];
    int32_t r = nf_transact(NETFS_CMD_SIZE, path, 0, 0, NULL, 0, buf, sizeof(buf));

    if (r >= 4) {
        return (int32_t) nf_get_le32(buf);
    }
    return -1;
}

int32_t netfs_read_file(const char *path, void *buffer, uint32_t buffer_size)
{
    return netfs_read_file_at(path, 0, buffer, buffer_size);
}

int32_t netfs_read_file_at(const char *path, uint32_t offset,
                           void *buffer, uint32_t buffer_size)
{
    if (buffer == NULL) {
        return -1;
    }
    return nf_transact(NETFS_CMD_READ, path, offset, buffer_size,
                       NULL, 0, (uint8_t *) buffer, buffer_size);
}

int32_t netfs_write_file(const char *path, const void *buffer, uint32_t size)
{
    int32_t r;

    if (buffer == NULL || size == 0u || size > 0xFFFFu) {
        /* 单条 TCP 发送负载受 uint16_t 限制；超大写入由上层分块。 */
        if (size == 0u && buffer != NULL) {
            return 0;
        }
        return -1;
    }
    r = nf_transact(NETFS_CMD_WRITE, path, 0, size,
                    buffer, (uint16_t) size, NULL, 0);
    return (r >= 0) ? (int32_t) size : -1;
}

bool netfs_delete(const char *path)
{
    return nf_transact(NETFS_CMD_DELETE, path, 0, 0, NULL, 0, NULL, 0) >= 0;
}

bool netfs_mkdir(const char *path)
{
    return nf_transact(NETFS_CMD_MKDIR, path, 0, 0, NULL, 0, NULL, 0) >= 0;
}

bool netfs_rmdir(const char *path)
{
    return nf_transact(NETFS_CMD_RMDIR, path, 0, 0, NULL, 0, NULL, 0) >= 0;
}

bool netfs_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    int32_t r;

    if (buffer == NULL || buffer_size == 0u) {
        return false;
    }
    r = nf_transact(NETFS_CMD_LIST, path, 0, 0, NULL, 0,
                    (uint8_t *) buffer, buffer_size - 1u);
    if (r < 0) {
        return false;
    }
    buffer[r] = '\0';
    return true;
}

bool netfs_connected(void)
{
    if (g_nf_handle < 0) {
        g_nf_session_up = false;
        return false;
    }
    if (!socket_tcp_is_connected(g_nf_handle)) {
        g_nf_session_up = false;
        return false;
    }
    return g_nf_session_up;
}

void netfs_disconnect(void)
{
    if (g_nf_handle >= 0) {
        (void) socket_close(g_nf_handle);
        g_nf_handle = -1;
    }
    g_nf_session_up = false;
    g_nf_server[0] = '\0';
    g_nf_share[0] = '\0';
    g_nf_port = 0;
}
