#ifndef _FILE_H_
#define _FILE_H_

#include "stdbool.h"
#include "stdint.h"

/* 文件系统类型 */
typedef enum {
    FS_TYPE_NONE = 0,
    FS_TYPE_FAT16,
    FS_TYPE_FAT32,
    FS_TYPE_ISO9660,
    FS_TYPE_NTFS,
    FS_TYPE_EXTFS,
    FS_TYPE_NETFS
} fs_type_t;

/* 挂载点信息 */
#define MAX_MOUNT_POINTS 8
#define MAX_MOUNT_PATH 128

typedef struct {
    char path[MAX_MOUNT_PATH];  /* 挂载点路径 */
    fs_type_t fs_type;          /* 文件系统类型 */
    int32_t partition;          /* 分区号（-1 表示整个磁盘，0-3 表示 MBR 分区） */
    int32_t blockdev_index;     /* 卷所在的块设备序号（-1 表示未知） */
    bool mounted;               /* 是否已挂载 */
} mount_point_t;

bool file_init(void);
const char *file_backend_name(void);

/* 动态挂载接口 */
bool file_mount(const char *mount_path, const char *fs_type, int32_t partition);
bool file_umount(const char *mount_path);
int32_t file_mount_count(void);
bool file_get_mount_info(int32_t index, mount_point_t *info);
int32_t file_mount_partition_hint(void);
bool file_auto_mount(void);  /* 自动探测并挂载到 / */

/* 当前正在初始化的文件系统应当从哪个块设备读写。
 *
 * 文件系统驱动（fat32/fat16/ntfs/extfs/iso9660）通过它知道卷位于哪块设备，
 * 从而走 drivers/storage/blockdev.c 的块设备抽象，而不是直接读写 legacy
 * ATA PIO 端口（0x1F0）—— 后者在没有 IDE 控制器的机器（AHCI/NVMe-only）
 * 上必然读写失败。返回 <0 表示未知，驱动应回退到 legacy PIO。
 *
 * 语义与 file_mount_partition_hint() 对齐：挂载过程中返回"本次挂载要用的
 * 设备"，否则返回当前激活挂载点所在的设备。 */
int32_t file_blockdev_hint(void);

/* 干净关机/重启前的数据落盘：
 *  - file_sync_all(): 把所有脏缓存（VFS 写回缓存 + 各文件系统自有块缓存）
 *    强制写回磁盘，并丢弃读缓存。调用后已挂载卷的数据与磁盘一致。
 *  - file_unmount_all(): 先 file_sync_all()，再卸载全部已挂载文件系统。
 *    关机/重启流程在停止用户进程后、关闭存储驱动前调用。 */
/* false means VFS dirty data remains: callers must not power off or mark clean.
 * Filesystem-private drivers currently lack error-returning sync interfaces. */
bool file_sync_all(void);
bool file_unmount_all(void);

/* 文件操作 */
bool file_exists(const char *path);
bool file_is_dir(const char *path);
int32_t file_size(const char *path);
uint16_t file_root_entry_count(void);
int32_t file_read(const char *path, void *buffer, uint32_t buffer_size);
int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t file_write(const char *path, const void *buffer, uint32_t size);
bool file_delete(const char *path);
bool file_mkdir(const char *path);
bool file_rmdir(const char *path);
bool file_rename(const char *oldpath, const char *newpath);

/* 修改文件属性位（当前仅 FAT32 后端实现）。
 * attr_mask 指定要修改的位（如 0x01=只读, 0x02=隐藏），attr_value 为这些位的新值。 */
bool file_set_attr(const char *path, uint8_t attr_mask, uint8_t attr_value);

/* File stat structure — all timestamps are Unix seconds (since 1970-01-01 UTC) */
typedef struct {
    uint64_t size;
    bool     is_dir;
    uint8_t  attr;
    uint64_t create_time;
    uint64_t modify_time;
    uint64_t access_time;
} file_stat_t;

bool file_stat(const char *path, file_stat_t *stat_out);
bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size);

/* 磁盘空间信息 */
typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t cluster_size;
} disk_space_t;

bool file_disk_space(const char *path, disk_space_t *info);

/* 盘符信息查询 */
typedef struct {
    char drive;             /* 盘符字母（大写，如 'C'） */
    bool mounted;           /* 是否已挂载 */
    fs_type_t fs_type;      /* 文件系统类型 */
    uint64_t total_bytes;   /* 总容量 */
    uint64_t free_bytes;    /* 空闲容量 */
} drive_info_t;

/* 查询指定盘符的信息（drive 是 A-Z 的字母，大小写均可） */
bool file_get_drive_info(char drive, drive_info_t *info);

/* 判断指定盘符是否已挂载 */
bool file_drive_mounted(char drive);

/* 获取已挂载的盘符列表，返回挂载的数量，drives 缓冲区大小至少 26 */
int32_t file_get_mounted_drives(char *drives, uint32_t max_drives);

/* 格式化指定盘符为指定文件系统（当前仅支持 fat32）。 */
bool file_format_drive(char drive, const char *fs_type);

/* 检查指定盘符路径对应的文件系统（当前仅 FAT32）。
 * 非 FAT32 或失败返回 false；成功时填充各输出参数。 */
bool file_chkdsk(const char *path, uint32_t *errors_out, uint32_t *fixed_out,
                 uint32_t *files_out, uint64_t *kb_out, uint32_t *serial_out);

/* ============================================================
 *  回收站 (Recycle Bin, Windows 风格)
 * ============================================================ */

/* 返回指定盘符的回收站目录路径（静态缓冲区，如 C:\$RECYCLE.BIN）。 */
const char *file_recycle_path(char drive);

/* 把文件移入回收站：在目标盘符根目录创建 $RECYCLE.BIN，复制文件后删除原文件。
 * 失败（只读文件系统 / 内存不足等）返回 false，调用方应回退为直接删除。 */
bool file_send_to_recycle_bin(const char *path);

/* 从回收站恢复文件：把 recycled_path 复制回 original_path 并删除回收站副本。 */
bool file_restore_from_recycle_bin(const char *recycled_path, const char *original_path);

/* 清空指定盘符的回收站。 */
bool file_empty_recycle_bin(char drive);

/* 返回指定盘符回收站占用的总字节数。 */
uint64_t file_recycle_bin_size(char drive);

/* ============================================================
 *  符号链接 (Symbolic Links)
 * ============================================================ */

/* 创建符号链接：linkpath 指向 target（target 可为绝对或相对路径）。 */
bool file_symlink(const char *target, const char *linkpath);

/* 读取符号链接目标路径到 buffer，返回目标长度，非符号链接返回 -1。 */
int32_t file_readlink(const char *path, char *buffer, uint32_t size);

/* 判断路径是否为符号链接。 */
bool file_is_symlink(const char *path);

/* ============================================================
 *  磁盘配额 (per-drive space limit)
 * ============================================================ */

typedef struct {
    char drive;
    uint64_t limit_bytes;  /* 0 = 无限制 */
    uint64_t used_bytes;   /* 已用空间（实时计算） */
} disk_quota_t;

/* 设置指定盘符的配额限制（内存中保存，limit_bytes=0 表示取消限制）。 */
bool file_set_quota(char drive, uint64_t limit_bytes);

/* 获取配额信息，used_bytes 通过 file_disk_space() 实时计算。 */
bool file_get_quota(char drive, disk_quota_t *info);

/* 检查写入 write_size 字节是否会超出配额，返回 true 表示允许写入。 */
bool file_check_quota(char drive, uint64_t write_size);

/* ============================================================
 *  网络驱动器映射 (net use / \\server\share)
 * ============================================================ */

/* 把远端 share 通过 MNFS 会话映射到本地盘符 drive（如 'Z'）。
 * port 为 0 时使用默认端口 445。连接失败 / 盘符被占用 / 无空闲挂载点时返回 false。 */
bool file_map_network_drive(char drive, const char *server, uint16_t port, const char *share);

/* 断开指定盘符的网络映射并关闭 MNFS 会话。 */
bool file_unmap_network_drive(char drive);

/* 查询某个盘符的网络映射详情；未映射返回 false。
 * 任一输出指针可为 NULL。 */
bool file_get_network_mapping(char drive, char *host_out, uint16_t *port_out, char *share_out);

/* ============================================================
 *  安全子系统钩子注册（由 fs/efs.c 与 kernel/security/uac.c 在
 *  初始化时调用；未注册前 file.c 的所有读/写/删除路径一律放行，
 *  保证 VFS 回归测试不受影响）。
 * ============================================================ */

/* EFS 透明加解密：
 *  is_enc(winpath)        : 该路径是否为 EFS 加密文件
 *  enc(winpath,in,in_len,out,out_cap) : 明文->密文，返回密文长度
 *  dec(winpath,in,in_len,out,out_cap) : 密文->明文，返回明文长度
 *  任一回调为 NULL 表示不启用 EFS。 */
typedef bool (*file_efs_is_encrypted_fn)(const char *winpath);
typedef uint32_t (*file_efs_crypt_fn)(const char *winpath,
                                      const uint8_t *in, uint32_t in_len,
                                      uint8_t *out, uint32_t out_cap);
void file_efs_set_hooks(file_efs_is_encrypted_fn is_enc,
                        file_efs_crypt_fn enc,
                        file_efs_crypt_fn dec);

/* UAC 文件权限检查：
 *  hook(winpath, write) : true=允许访问，false=拒绝。
 *  为 NULL 表示不启用权限隔离（全部放行）。 */
typedef bool (*file_uac_access_fn)(const char *winpath, bool write);
void file_uac_set_hook(file_uac_access_fn hook);
/* ============================================================
 *  One-click raw partition backup (streaming, low memory)
 * ============================================================ */
int64_t file_backup_begin(const char *image_path);
int32_t file_backup_read_src(uint32_t rel_lba, uint32_t sectors, void *buf);
int32_t file_backup_append(const void *buf, uint32_t len);
int32_t file_backup_end(void);
int64_t file_backup_restore_begin(const char *image_path);
int32_t file_backup_read_img(const char *image_path, uint32_t offset, void *buf, uint32_t len);
int32_t file_backup_write_src(uint32_t rel_lba, uint32_t sectors, const void *buf);
int32_t file_backup_restore_end(void);
/* Recycle bin index & policy (size cap / age purge) */
void file_recycle_record(const char *recycled_name, const char *original_path, uint32_t size);
void file_recycle_maintenance(char drive);
int32_t file_recycle_restore_by_name(const char *recycled_name);
uint32_t file_recycle_list(char drive, char *out, uint32_t out_size);

#endif
