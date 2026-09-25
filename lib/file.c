#include "common.h"
#include "file.h"
#include "blockdev.h"
#include "cmos.h"
#include "registry.h"
#include "extfs.h"
#include "fat16.h"
#include "fat32.h"
#include "fs_cache.h"
#include "fs_perm.h"
#include "gpt.h"
#include "iso9660.h"
#include "kernel.h"
#include "netfs.h"
#include "ntfs.h"
#include "path.h"
#include "stddef.h"
#include "string.h"
#include "spinlock.h"
#include "memory.h"

/* ============================================================
 *  瀹夊叏瀛愮郴缁熼挬瀛愶紙EFS 閫忔槑鍔犺В瀵?/ UAC 鏉冮檺妫€鏌ワ級
 *  鏈敞鍐屽墠鍏ㄩ儴鏀捐锛屼繚璇佺幇鏈?VFS 琛屼负涓嶅彉銆?
 * ============================================================ */
#define FILE_EFS_SCRATCH_SIZE 65536U

static file_efs_is_encrypted_fn g_efs_is_encrypted = NULL;
static file_efs_crypt_fn g_efs_encrypt = NULL;
static file_efs_crypt_fn g_efs_decrypt = NULL;
static uint8_t g_efs_raw[FILE_EFS_SCRATCH_SIZE];
static uint8_t g_efs_plain[FILE_EFS_SCRATCH_SIZE];

static file_uac_access_fn g_uac_access = NULL;

void file_efs_set_hooks(file_efs_is_encrypted_fn is_enc,
                        file_efs_crypt_fn enc,
                        file_efs_crypt_fn dec)
{
    g_efs_is_encrypted = is_enc;
    g_efs_encrypt = enc;
    g_efs_decrypt = dec;
}

void file_uac_set_hook(file_uac_access_fn hook)
{
    g_uac_access = hook;
}

static bool file_uac_allow(const char *resolved, bool write)
{
    if (g_uac_access == NULL) {
        return true;
    }
    return g_uac_access(resolved, write);
}
/* ============================================================
 *  鎸傝浇鐐圭鐞?
 * ============================================================ */

static mount_point_t g_mount_points[MAX_MOUNT_POINTS];
static int32_t g_mount_count = 0;
static int32_t g_pending_mount_partition = -1;

/* 块设备提示，语义与 g_pending_mount_partition 一一对应：
 *   g_probe_blockdev   —— file_auto_mount() 正在探测的块设备序号
 *   g_pending_blockdev —— 下一次 fs 初始化应当使用的块设备序号
 *   g_current_blockdev —— 当前激活挂载点所在的块设备序号
 * 三者 <0 都表示"未知"，此时 fs 驱动回退到 legacy ATA PIO。
 * 存在的理由：fs 驱动过去直连 0x1F0，AHCI/NVMe-only 机器上完全挂不上盘。 */
static int32_t g_probe_blockdev = -1;
static int32_t g_pending_blockdev = -1;
static int32_t g_current_blockdev = -1;

/* 褰撳墠婵€娲荤殑鏂囦欢绯荤粺锛堢敤浜庡叏灞€鎿嶄綔锛?*/
static fs_type_t g_current_fs = FS_TYPE_NONE;
static char g_current_mount_path[MAX_MOUNT_PATH] = PATH_ROOT;
static int32_t g_current_mount_partition = -1;
/* Serialises mutations of the mount table against concurrent mount calls
 * and interrupt-driven FS activity.  Held only for the small slot-update
 * critical sections, never across FS init / cache invalidation. */
static spinlock_t g_fs_lock = SPINLOCK_INITIALIZER;

#define FILE_CACHE_KEY_SEP ((char) 0x1F)
#define FILE_CACHE_KEY_MAX 512U
#define FILE_AUTO_DRIVE_FIRST 'C'
#define FILE_AUTO_DRIVE_LAST  'Z'
#define FILE_MBR_PARTITIONS   4
#define FILE_MAX_VOLUME_CANDIDATES 16

#define FILE_ATA_DATA_PORT         0x1F0
#define FILE_ATA_SECTOR_COUNT_PORT 0x1F2
#define FILE_ATA_LBA_LOW_PORT      0x1F3
#define FILE_ATA_LBA_MID_PORT      0x1F4
#define FILE_ATA_LBA_HIGH_PORT     0x1F5
#define FILE_ATA_DRIVE_PORT        0x1F6
#define FILE_ATA_COMMAND_PORT      0x1F7
#define FILE_ATA_STATUS_PORT       0x1F7
#define FILE_ATA_CMD_READ_SECTORS  0x20
#define FILE_ATA_STATUS_BSY        0x80
#define FILE_ATA_STATUS_DRQ        0x08
#define FILE_ATA_WAIT_LIMIT        1000000U

typedef struct {
    int32_t lba;
    uint8_t type;
} file_volume_candidate_t;

/* ============================================================
 *  杈呭姪鍑芥暟锛氭枃浠剁郴缁熺被鍨嬪悕绉拌浆鎹?
 * ============================================================ */

static fs_type_t fs_type_from_name(const char *name)
{
    if (name == NULL) {
        return FS_TYPE_NONE;
    }
    if (strcmp(name, "fat16") == 0 || strcmp(name, "FAT16") == 0) {
        return FS_TYPE_FAT16;
    }
    if (strcmp(name, "fat32") == 0 || strcmp(name, "FAT32") == 0) {
        return FS_TYPE_FAT32;
    }
    if (strcmp(name, "iso9660") == 0 || strcmp(name, "ISO9660") == 0 ||
        strcmp(name, "cdrom") == 0 || strcmp(name, "CDROM") == 0) {
        return FS_TYPE_ISO9660;
    }
    if (strcmp(name, "ntfs") == 0 || strcmp(name, "NTFS") == 0) {
        return FS_TYPE_NTFS;
    }
    if (strcmp(name, "ext") == 0 || strcmp(name, "ext2") == 0 ||
        strcmp(name, "ext3") == 0 || strcmp(name, "ext4") == 0 ||
        strcmp(name, "extfs") == 0 || strcmp(name, "EXTFS") == 0) {
        return FS_TYPE_EXTFS;
    }
    if (strcmp(name, "netfs") == 0 || strcmp(name, "network") == 0 ||
        strcmp(name, "smb") == 0 || strcmp(name, "NETFS") == 0) {
        return FS_TYPE_NETFS;
    }
    return FS_TYPE_NONE;
}

static const char *fs_type_to_name(fs_type_t type)
{
    switch (type) {
        case FS_TYPE_FAT16:   return "fat16";
        case FS_TYPE_FAT32:   return "fat32";
        case FS_TYPE_ISO9660: return "iso9660";
        case FS_TYPE_NTFS:    return "ntfs";
        case FS_TYPE_EXTFS:   return "extfs";
        case FS_TYPE_NETFS:   return "netfs";
        default:              return "none";
    }
}

/* ============================================================
 *  杈呭姪鍑芥暟锛氬垵濮嬪寲鎸囧畾绫诲瀷鐨勬枃浠剁郴缁?
 * ============================================================ */

static uint16_t file_read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t file_read_le32(const uint8_t *data)
{
    return ((uint32_t) data[0]) |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static bool file_ata_wait_not_busy(void)
{
    for (uint32_t i = 0; i < FILE_ATA_WAIT_LIMIT; i++) {
        if ((inb(FILE_ATA_STATUS_PORT) & FILE_ATA_STATUS_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool file_ata_wait_data_ready(void)
{
    if (!file_ata_wait_not_busy()) {
        return false;
    }
    for (uint32_t i = 0; i < FILE_ATA_WAIT_LIMIT; i++) {
        if ((inb(FILE_ATA_STATUS_PORT) & FILE_ATA_STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

static bool file_ata_read_sector(uint32_t lba, uint8_t sector[512])
{
    uint16_t *dst = (uint16_t *) sector;

    if (!file_ata_wait_not_busy()) {
        return false;
    }
    outb(FILE_ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(FILE_ATA_SECTOR_COUNT_PORT, 1);
    outb(FILE_ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(FILE_ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(FILE_ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(FILE_ATA_COMMAND_PORT, FILE_ATA_CMD_READ_SECTORS);

    if (!file_ata_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < 256; i++) {
        dst[i] = inw(FILE_ATA_DATA_PORT);
    }
    return true;
}

static bool file_mount_path_for_drive(char drive, char out[MAX_MOUNT_PATH])
{
    if (out == NULL || drive < FILE_AUTO_DRIVE_FIRST || drive > FILE_AUTO_DRIVE_LAST) {
        return false;
    }
    out[0] = drive;
    out[1] = ':';
    out[2] = PATH_SEPARATOR;
    out[3] = '\0';
    return true;
}

static bool fs_init_by_type(fs_type_t type)
{
    switch (type) {
        case FS_TYPE_FAT32:
            return fat32_init();
        case FS_TYPE_FAT16:
            return fat16_init();
        case FS_TYPE_ISO9660:
            return iso9660_init();
        case FS_TYPE_NTFS:
            return ntfs_init();
        case FS_TYPE_EXTFS:
            return extfs_init();
        case FS_TYPE_NETFS:
            return netfs_connected();
        default:
            return false;
    }
}

/* ============================================================
 *  杈呭姪鍑芥暟锛氳矾寰勫尮閰嶏紙鏈€闀垮墠缂€鍖归厤锛?
 * ============================================================ */

static bool normalize_mount_path(const char *input, char output[MAX_MOUNT_PATH])
{
    char resolved[PATH_MAX_LEN];
    uint64_t len;

    if (input == NULL || output == NULL || input[0] == '\0') {
        return false;
    }
    if (!path_resolve(NULL, input, resolved, sizeof(resolved))) {
        return false;
    }
    len = strlcpy(output, resolved, MAX_MOUNT_PATH);
    if (len == 0 || len >= MAX_MOUNT_PATH ||
        output[0] < 'A' || output[0] > 'Z' ||
        output[1] != ':' || output[2] != PATH_SEPARATOR) {
        return false;
    }
    while (len > 3 && output[len - 1] == PATH_SEPARATOR) {
        output[len - 1] = '\0';
        len--;
    }
    return true;
}

static void set_current_mount(const mount_point_t *mount)
{
    if (mount == NULL || !mount->mounted) {
        g_current_fs = FS_TYPE_NONE;
        g_current_mount_path[0] = '\0';
        g_current_mount_partition = -1;
        g_current_blockdev = -1;
        return;
    }
    g_current_fs = mount->fs_type;
    g_current_mount_partition = mount->partition;
    g_current_blockdev = mount->blockdev_index;
    strlcpy(g_current_mount_path, mount->path, sizeof(g_current_mount_path));
}

static bool restore_current_mount(fs_type_t fs, int32_t partition, int32_t blockdev,
                                  const char *mount_path)
{
    if (fs == FS_TYPE_NONE || mount_path == NULL || mount_path[0] == '\0') {
        return false;
    }
    g_pending_mount_partition = partition;
    g_pending_blockdev = blockdev;
    if (!fs_init_by_type(fs)) {
        g_pending_mount_partition = -1;
        g_pending_blockdev = -1;
        return false;
    }
    g_pending_mount_partition = -1;
    g_pending_blockdev = -1;
    g_current_fs = fs;
    g_current_mount_partition = partition;
    g_current_blockdev = blockdev;
    strlcpy(g_current_mount_path, mount_path, sizeof(g_current_mount_path));
    return true;
}

static void make_relative_path(const char *path, const char *mount_path, const char **relative_path)
{
    uint32_t mp_len = (uint32_t) strlen(mount_path);

    if (path[mp_len] == PATH_SEPARATOR) {
        *relative_path = path + mp_len;
    } else if (path[mp_len] == '\0') {
        *relative_path = PATH_SEPARATOR_STR;
    } else {
        *relative_path = path + mp_len;
    }
}

static bool file_path_to_backend(const char *path, char output[PATH_MAX_LEN])
{
    uint32_t i = 0;

    if (path == NULL || output == NULL) {
        return false;
    }
    while (path[i] != '\0') {
        if (i + 1 >= PATH_MAX_LEN) {
            return false;
        }
        output[i] = path[i] == PATH_SEPARATOR ? '/' : path[i];
        i++;
    }
    if (i == 0) {
        output[i++] = '/';
    }
    output[i] = '\0';
    return true;
}

static bool file_build_cache_key(const char *rel_path, char *out, uint32_t out_size)
{
    uint64_t mount_len;
    uint64_t rel_len;

    if (rel_path == NULL || out == NULL || out_size == 0 || g_current_mount_path[0] == '\0') {
        return false;
    }
    mount_len = strlen(g_current_mount_path);
    rel_len = strlen(rel_path);
    if (mount_len + 1 + rel_len + 1 > out_size) {
        return false;
    }
    memcpy(out, g_current_mount_path, mount_len);
    out[mount_len] = FILE_CACHE_KEY_SEP;
    memcpy(out + mount_len + 1, rel_path, rel_len + 1);
    return true;
}

static const char *file_cache_relative_path(const char *cache_key)
{
    const char *sep = strchr(cache_key, (uint8_t) FILE_CACHE_KEY_SEP);

    return sep != NULL ? sep + 1 : cache_key;
}

static void file_invalidate_cached_file(const char *rel_path)
{
    char cache_key[FILE_CACHE_KEY_MAX];

    if (file_build_cache_key(rel_path, cache_key, sizeof(cache_key))) {
        fs_cache_invalidate_path(cache_key);
    } else {
        fs_cache_invalidate_all();
    }
}

static int32_t find_mount_point(const char *path)
{
    int32_t best_idx = -1;
    uint32_t best_len = 0;
    uint32_t path_len;
    int32_t i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    path_len = (uint32_t) strlen(path);

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!g_mount_points[i].mounted) {
            continue;
        }

        uint32_t mp_len = strlen(g_mount_points[i].path);
        if (mp_len == 0) {
            continue;
        }
        if (mp_len == 3 &&
            g_mount_points[i].path[2] == PATH_SEPARATOR) {
            if (path[0] == g_mount_points[i].path[0] &&
                path[1] == ':' &&
                path[2] == PATH_SEPARATOR &&
                mp_len > best_len) {
                best_len = mp_len;
                best_idx = i;
            }
            continue;
        }

        if (path_len >= mp_len && strncmp(path, g_mount_points[i].path, mp_len) == 0) {
            if (path[mp_len] == '\0' || path[mp_len] == PATH_SEPARATOR) {
                if (mp_len > best_len) {
                    best_len = mp_len;
                    best_idx = i;
                }
            }
        }
    }

    return best_idx;
}

/* ============================================================
 *  杈呭姪鍑芥暟锛氬垏鎹㈠埌鎸囧畾鎸傝浇鐐圭殑鏂囦欢绯荤粺
 * ============================================================ */

static bool switch_to_mount(int32_t mount_idx, const char **relative_path)
{
    const char *path = *relative_path;
    mount_point_t *mount;

    if (mount_idx < 0 || mount_idx >= MAX_MOUNT_POINTS) {
        return false;
    }

    mount = &g_mount_points[mount_idx];
    if (!mount->mounted) {
        return false;
    }

    if (g_current_fs == mount->fs_type &&
        g_current_mount_partition == mount->partition &&
        g_current_blockdev == mount->blockdev_index) {
        set_current_mount(mount);
        make_relative_path(path, mount->path, relative_path);
        return true;
    }

    g_pending_mount_partition = mount->partition;
    g_pending_blockdev = mount->blockdev_index;
    if (!fs_init_by_type(mount->fs_type)) {
        g_pending_mount_partition = -1;
        g_pending_blockdev = -1;
        return false;
    }
    g_pending_mount_partition = -1;
    g_pending_blockdev = -1;

    set_current_mount(mount);
    make_relative_path(path, mount->path, relative_path);

    return true;
}

/* ============================================================
 *  鍔ㄦ€佹寕杞芥帴鍙?
 * ============================================================ */

bool file_mount(const char *mount_path, const char *fs_type, int32_t partition)
{
    char normalized_path[MAX_MOUNT_PATH];
    char previous_mount_path[MAX_MOUNT_PATH];
    fs_type_t previous_fs = g_current_fs;
    int32_t previous_partition = g_current_mount_partition;
    int32_t previous_blockdev = g_current_blockdev;
    int32_t mount_blockdev = g_probe_blockdev;
    fs_type_t type;
    int32_t i;
    int32_t empty_idx = -1;
    bool restore_previous = false;

    if (fs_type == NULL || !normalize_mount_path(mount_path, normalized_path)) {
        return false;
    }
    strlcpy(previous_mount_path, g_current_mount_path, sizeof(previous_mount_path));

    type = fs_type_from_name(fs_type);
    if (type == FS_TYPE_NONE) {
        return false;
    }
    restore_previous = previous_fs != FS_TYPE_NONE &&
                       previous_mount_path[0] != '\0';

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, normalized_path) == 0) {
            return false;
        }
        if (!g_mount_points[i].mounted && empty_idx < 0) {
            empty_idx = i;
        }
    }

    if (empty_idx < 0) {
        return false;
    }

    g_pending_mount_partition = partition;
    g_pending_blockdev = mount_blockdev;
    if (!fs_init_by_type(type)) {
        g_pending_mount_partition = -1;
        g_pending_blockdev = -1;
        if (restore_previous) {
            (void) restore_current_mount(previous_fs, previous_partition,
                                         previous_blockdev, previous_mount_path);
        }
        return false;
    }
    g_pending_mount_partition = -1;
    g_pending_blockdev = -1;

    {
        uint64_t fs_flags = 0;
        spin_lock_irqsave(&g_fs_lock, &fs_flags);
        strlcpy(g_mount_points[empty_idx].path, normalized_path, sizeof(g_mount_points[empty_idx].path));
        g_mount_points[empty_idx].fs_type = type;
        g_mount_points[empty_idx].partition = partition;
        g_mount_points[empty_idx].blockdev_index = mount_blockdev;
        g_mount_points[empty_idx].mounted = true;
        g_mount_count++;
        spin_unlock_irqrestore(&g_fs_lock, fs_flags);
    }

    if (g_mount_count == 1 || strcmp(normalized_path, PATH_ROOT) == 0) {
        set_current_mount(&g_mount_points[empty_idx]);
    } else if (restore_previous) {
        (void) restore_current_mount(previous_fs, previous_partition,
                                     previous_blockdev, previous_mount_path);
    }

    fs_cache_invalidate_all();
    return true;
}

bool file_umount(const char *mount_path)
{
    char normalized_path[MAX_MOUNT_PATH];
    int32_t i;

    if (!normalize_mount_path(mount_path, normalized_path)) {
        return false;
    }

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, normalized_path) == 0) {
            if (!file_sync_all()) return false;
            g_mount_points[i].mounted = false;
            g_mount_points[i].fs_type = FS_TYPE_NONE;
            g_mount_points[i].path[0] = '\0';
            g_mount_count--;

            if (strcmp(g_current_mount_path, normalized_path) == 0) {
                if (g_mount_count > 0) {
                    int32_t j;
                    bool activated = false;
                    for (j = 0; j < MAX_MOUNT_POINTS; j++) {
                        if (g_mount_points[j].mounted) {
                            activated = restore_current_mount(g_mount_points[j].fs_type,
                                                              g_mount_points[j].partition,
                                                              g_mount_points[j].blockdev_index,
                                                              g_mount_points[j].path);
                            if (activated) {
                                break;
                            }
                        }
                    }
                    if (!activated) {
                        set_current_mount(NULL);
                    }
                } else {
                    set_current_mount(NULL);
                }
            }

            fs_cache_invalidate_all();
            return true;
        }
    }

    return false;
}

int32_t file_mount_count(void)
{
    return g_mount_count;
}

bool file_sync_all(void)
{
    /* 1) VFS generic write-back cache: flush every dirty slot to disk. */
    (void) fs_cache_flush();

    /* 2) Filesystem-private block caches. FAT32/FAT16/NTFS write through
     *    synchronously via PIO sector writes and keep no dirty cache; extfs
     *    keeps a block cache whose dirty blocks must be flushed explicitly. */
    bool ext_ok = extfs_sync();

    /* 3) Drop the read cache. */
    fs_cache_invalidate_all();
    return ext_ok && fs_cache_info()->dirty_count == 0;
}

bool file_unmount_all(void)
{
    char mount_path[MAX_MOUNT_PATH];
    int32_t i;

    /* Flush once more so metadata produced while closing files hits disk. */
    if (!file_sync_all()) return false;

    /* Mark the FAT32 volume cleanly unmounted so the next boot skips WAL
     * recovery / auto-chkdsk. No-op if FAT32 is not mounted. */
    fat32_wal_mark_clean();

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!g_mount_points[i].mounted) {
            continue;
        }
        strlcpy(mount_path, g_mount_points[i].path, sizeof(mount_path));
        if (!file_umount(mount_path)) return false;
    }
    return true;
}

int32_t file_mount_partition_hint(void)
{
    int32_t i;

    if (g_pending_mount_partition >= 0) {
        return g_pending_mount_partition;
    }
    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted && g_mount_points[i].partition >= 0) {
            return g_mount_points[i].partition;
        }
    }
    return -1;
}

int32_t file_blockdev_hint(void)
{
    if (g_pending_blockdev >= 0) {
        return g_pending_blockdev;
    }
    return g_current_blockdev;
}

bool file_get_mount_info(int32_t index, mount_point_t *info)
{
    int32_t i;
    int32_t count = 0;

    if (info == NULL || index < 0) {
        return false;
    }

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted) {
            if (count == index) {
                memcpy(info, &g_mount_points[i], sizeof(mount_point_t));
                return true;
            }
            count++;
        }
    }

    return false;
}

/* ============================================================
 *  鏂囦欢鎿嶄綔锛堝甫鎸傝浇鐐硅В鏋愶級
 * ============================================================ */

static bool resolve_path(const char *path,
                         char resolved[PATH_MAX_LEN],
                         char backend_path[PATH_MAX_LEN])
{
    const char *relative_path;
    int32_t mount_idx;

    if (!path_resolve(PATH_ROOT, path, resolved, PATH_MAX_LEN)) {
        return false;
    }
    mount_idx = find_mount_point(resolved);
    if (mount_idx < 0) {
        return false;
    }
    relative_path = resolved;
    if (!switch_to_mount(mount_idx, &relative_path)) {
        return false;
    }
    return file_path_to_backend(relative_path, backend_path);
}

/* ============================================================
 *  绗﹀彿閾炬帴璺熼殢
 * ============================================================ */

#define FILE_MAX_SYMLINK_DEPTH 8U

static bool file_backend_is_symlink(const char *backend_path)
{
    switch (g_current_fs) {
        case FS_TYPE_FAT32:  return fat32_is_symlink(backend_path);
        case FS_TYPE_EXTFS:  return extfs_is_symlink(backend_path);
        default:             return false;
    }
}

static int32_t file_backend_readlink(const char *backend_path,
                                     char *target, uint32_t target_size)
{
    switch (g_current_fs) {
        case FS_TYPE_FAT32:  return fat32_read_symlink(backend_path, target, target_size);
        case FS_TYPE_EXTFS:  return extfs_read_symlink(backend_path, target, target_size);
        default:             return -1;
    }
}

static bool file_follow_links(const char *input,
                              char resolved_out[PATH_MAX_LEN],
                              char backend_out[PATH_MAX_LEN])
{
    char current[PATH_MAX_LEN];
    char target[PATH_MAX_LEN];
    uint32_t depth = 0;

    if (input == NULL ||
        strlcpy(current, input, sizeof(current)) >= sizeof(current)) {
        return false;
    }

    while (depth < FILE_MAX_SYMLINK_DEPTH) {
        char resolved[PATH_MAX_LEN];
        char backend[PATH_MAX_LEN];

        if (!resolve_path(current, resolved, backend)) {
            return false;
        }
        if (!file_backend_is_symlink(backend)) {
            strlcpy(resolved_out, resolved, PATH_MAX_LEN);
            strlcpy(backend_out, backend, PATH_MAX_LEN);
            return true;
        }

        int32_t n = file_backend_readlink(backend, target, sizeof(target) - 1U);
        if (n <= 0) {
            strlcpy(resolved_out, resolved, PATH_MAX_LEN);
            strlcpy(backend_out, backend, PATH_MAX_LEN);
            return true;
        }
        target[n] = '\0';

        if (!path_is_absolute(target)) {
            char dir[PATH_MAX_LEN];
            uint32_t len = (uint32_t) strlen(resolved);
            uint32_t cut = len;

            while (cut > 3 && resolved[cut - 1] != PATH_SEPARATOR) {
                cut--;
            }
            if (cut > 3) {
                memcpy(dir, resolved, cut);
                dir[cut] = '\0';
            } else {
                dir[0] = resolved[0];
                dir[1] = ':';
                dir[2] = PATH_SEPARATOR;
                dir[3] = '\0';
            }
            if (!path_resolve(dir, target, current, sizeof(current))) {
                return false;
            }
        } else {
            strlcpy(current, target, sizeof(current));
        }
        depth++;
    }
    return false;
}

bool file_exists(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!file_follow_links(path, resolved, backend_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_exists(backend_path);
        case FS_TYPE_FAT16:   return fat16_exists(backend_path);
        case FS_TYPE_ISO9660: return iso9660_exists(backend_path);
        case FS_TYPE_NTFS:    return ntfs_exists(backend_path);
        case FS_TYPE_EXTFS:   return extfs_exists(backend_path);
        case FS_TYPE_NETFS:   return netfs_exists(backend_path);
        default:              return false;
    }
}

bool file_is_dir(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!file_follow_links(path, resolved, backend_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_is_dir(backend_path);
        case FS_TYPE_FAT16:   return fat16_is_dir(backend_path);
        case FS_TYPE_ISO9660: return iso9660_is_dir(backend_path);
        case FS_TYPE_NTFS:    return ntfs_is_dir(backend_path);
        case FS_TYPE_EXTFS:   return extfs_is_dir(backend_path);
        case FS_TYPE_NETFS:   return netfs_is_dir(backend_path);
        default:              return false;
    }
}

int32_t file_size(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!file_follow_links(path, resolved, backend_path)) {
        return -1;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_file_size(backend_path);
        case FS_TYPE_FAT16:   return fat16_file_size(backend_path);
        case FS_TYPE_ISO9660: return iso9660_file_size(backend_path);
        case FS_TYPE_NTFS:    return ntfs_file_size(backend_path);
        case FS_TYPE_EXTFS:   return extfs_file_size(backend_path);
        case FS_TYPE_NETFS:   return netfs_file_size(backend_path);
        default:              return -1;
    }
}

/* ============================================================
 *  鍐呴儴璇诲彇鍥炶皟锛堢敤浜庣紦瀛橈級
 * ============================================================ */

static int32_t cached_read_at(const char *path, uint32_t offset,
                               void *buffer, uint32_t buffer_size)
{
    const char *rel_path = file_cache_relative_path(path);

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_NETFS:   return netfs_read_file_at(rel_path, offset, buffer, buffer_size);
        default:              return -1;
    }
}

int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    char cache_key[FILE_CACHE_KEY_MAX];
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!file_follow_links(path, resolved, backend_path)) {
        return -1;
    }

    /* UAC 璇绘潈闄愭鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, false)) {
        return -1;
    }

    /* Unix 鏂囦欢鏉冮檺妫€鏌ワ紙鍐呮牳涓婁笅鏂?/ root 鑷姩鏀捐锛?*/
    if (!fs_perm_check(resolved, false, false)) {
        return -1;
    }

    /* EFS 閫忔槑瑙ｅ瘑锛氬姞瀵嗘枃浠剁粫杩囩紦瀛橈紝鏁存枃浠惰鍑哄悗瑙ｅ瘑鍐嶆寜鍋忕Щ鍒囩墖 */
    if (g_efs_is_encrypted != NULL && g_efs_decrypt != NULL &&
        g_efs_is_encrypted(resolved)) {
        int32_t raw_len = cached_read_at(backend_path, 0, g_efs_raw,
                                         FILE_EFS_SCRATCH_SIZE);
        uint32_t plain_len;

        if (raw_len <= 0) {
            return -1;
        }
        plain_len = g_efs_decrypt(resolved, g_efs_raw, (uint32_t) raw_len,
                                 g_efs_plain, FILE_EFS_SCRATCH_SIZE);
        if (plain_len == 0) {
            return -1;
        }
        if (offset >= plain_len) {
            return 0;
        }
        if (offset + buffer_size > plain_len) {
            buffer_size = plain_len - offset;
        }
        memcpy(buffer, g_efs_plain + offset, buffer_size);
        return (int32_t) buffer_size;
    }
    /* 娉ㄦ剰锛氱紦瀛樼殑 key 搴旇鍖呭惈鎸傝浇鐐癸紝杩欓噷绠€鍖栧鐞?*/
    if (!file_build_cache_key(backend_path, cache_key, sizeof(cache_key))) {
        return cached_read_at(backend_path, offset, buffer, buffer_size);
    }
    return fs_cache_read_at(cache_key, offset, buffer, buffer_size, cached_read_at);
}

int32_t file_read(const char *path, void *buffer, uint32_t buffer_size)
{
    return file_read_at(path, 0, buffer, buffer_size);
}

int32_t file_write(const char *path, const void *buffer, uint32_t size)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];
    int32_t written = -1;
    const void *write_buf = buffer;
    uint32_t write_size = size;

    if (!file_follow_links(path, resolved, backend_path)) {
        return -1;
    }

    /* UAC 鍐欐潈闄愭鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, true)) {
        return -1;
    }

    /* Unix 鏂囦欢鏉冮檺妫€鏌ワ紙鍐呮牳涓婁笅鏂?/ root 鑷姩鏀捐锛?*/
    if (!fs_perm_check(resolved, true, false)) {
        return -1;
    }

    /* 纾佺洏閰嶉妫€鏌ワ細鍐欏叆鍓嶇‘璁や笉浼氳秴鍑鸿鐩樼閰嶉 */
    if (!file_check_quota(resolved[0], (uint64_t) size)) {
        return -1;
    }

    /* EFS 閫忔槑鍔犲瘑锛氭槑鏂囧姞瀵嗗埌 scratch锛屽啀鍐欏瘑鏂囪惤鐩?*/
    if (g_efs_is_encrypted != NULL && g_efs_encrypt != NULL &&
        g_efs_is_encrypted(resolved)) {
        uint32_t enc_len = g_efs_encrypt(resolved, buffer, size,
                                         g_efs_raw, FILE_EFS_SCRATCH_SIZE);
        if (enc_len == 0 || enc_len > FILE_EFS_SCRATCH_SIZE) {
            return -1;
        }
        write_buf = g_efs_raw;
        write_size = enc_len;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   written = fat32_write_file(backend_path, write_buf, write_size); break;
        case FS_TYPE_FAT16:   written = fat16_write_file(backend_path, write_buf, write_size); break;
        case FS_TYPE_ISO9660: written = iso9660_write_file(backend_path, write_buf, write_size); break;
        case FS_TYPE_NTFS:    written = ntfs_write_file(backend_path, write_buf, write_size); break;
        case FS_TYPE_EXTFS:   written = extfs_write_file(backend_path, write_buf, write_size); break;
        case FS_TYPE_NETFS:   written = netfs_write_file(backend_path, write_buf, write_size); break;
        default:              written = -1; break;
    }

    if (written >= 0) {
        file_invalidate_cached_file(backend_path);
        written = (int32_t) size;
    }
    return written;
}

bool file_delete(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];
    bool ok = false;

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    /* UAC 鏉冮檺妫€鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, true)) {
        return false;
    }

    /* Unix 鏂囦欢鏉冮檺妫€鏌ワ紙鍐呮牳涓婁笅鏂?/ root 鑷姩鏀捐锛?*/
    if (!fs_perm_check(resolved, true, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_delete(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_delete(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_delete(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_delete(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_delete(backend_path); break;
        case FS_TYPE_NETFS:   ok = netfs_delete(backend_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        file_invalidate_cached_file(backend_path);
    }
    return ok;
}

bool file_mkdir(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];
    bool ok = false;

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    /* UAC 鏉冮檺妫€鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, true)) {
        return false;
    }

    /* Unix 鏂囦欢鏉冮檺妫€鏌ワ紙鍐呮牳涓婁笅鏂?/ root 鑷姩鏀捐锛?*/
    if (!fs_perm_check(resolved, true, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_mkdir(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_mkdir(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_mkdir(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_mkdir(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_mkdir(backend_path); break;
        case FS_TYPE_NETFS:   ok = netfs_mkdir(backend_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_rmdir(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];
    bool ok = false;

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    /* UAC 鏉冮檺妫€鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, true)) {
        return false;
    }

    /* Unix 鏂囦欢鏉冮檺妫€鏌ワ紙鍐呮牳涓婁笅鏂?/ root 鑷姩鏀捐锛?*/
    if (!fs_perm_check(resolved, true, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_rmdir(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_rmdir(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_rmdir(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_rmdir(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_rmdir(backend_path); break;
        case FS_TYPE_NETFS:   ok = netfs_rmdir(backend_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

/* Convert FAT32 date/time (packed DOS format) to Unix seconds. */
static uint64_t file_fat_datetime_to_unix(uint16_t fat_date, uint16_t fat_time)
{
    uint32_t year = ((fat_date >> 9) & 0x7FU) + 1980U;
    uint32_t month = (fat_date >> 5) & 0x0FU;
    uint32_t day = fat_date & 0x1FU;
    uint32_t hour = (fat_time >> 11) & 0x1FU;
    uint32_t minute = (fat_time >> 5) & 0x3FU;
    uint32_t second = (fat_time & 0x1FU) * 2U;
    if (month <= 2U) {
        year--;
        month += 12U;
    }
    uint32_t era = year / 100U;
    uint32_t yoe = year - era * 100U;
    uint32_t doy = (153U * (month - 3U) + 2U) / 5U + day - 1U;
    uint32_t doe = yoe * 365U + yoe / 4U + doy;
    uint64_t days = (uint64_t)era * 36524U + (uint64_t)doe - (uint64_t)(era / 4U);
    return days * 86400ULL + (uint64_t)hour * 3600ULL + (uint64_t)minute * 60ULL + (uint64_t)second;
}

/* NTFS timestamp (100-ns intervals since 1601-01-01) to Unix seconds. */
static uint64_t file_ntfs_time_to_unix(uint64_t ntfs_time)
{
    if (ntfs_time < 116444736000000000ULL) {
        return 0;
    }
    return (ntfs_time / 10000000ULL) - 11644473600ULL;
}

/* Cross-filesystem copy: read source in chunks, write to destination. */
static bool file_copy_regular(const char *src_path, const char *dst_path)
{
    int32_t fsize = file_size(src_path);
    if (fsize < 0) {
        return false;
    }
    uint8_t *buf = (uint8_t *)kmalloc(65536);
    if (buf == NULL) {
        return false;
    }
    uint32_t offset = 0;
    bool ok = true;
    while (offset < (uint32_t)fsize) {
        uint32_t chunk = (uint32_t)fsize - offset;
        if (chunk > 65536U) {
            chunk = 65536U;
        }
        int32_t rd = file_read_at(src_path, offset, buf, chunk);
        if (rd <= 0) {
            ok = false;
            break;
        }
        if (offset == 0U) {
            if (!file_write(dst_path, buf, (uint32_t)rd)) {
                ok = false;
                break;
            }
        } else {
            int32_t existing = file_size(dst_path);
            if (existing < 0) {
                ok = false;
                break;
            }
            uint8_t *combined = (uint8_t *)kmalloc((uint32_t)existing + (uint32_t)rd);
            if (combined == NULL) {
                ok = false;
                break;
            }
            if (existing > 0 && file_read_at(dst_path, 0, combined, (uint32_t)existing) != existing) {
                kfree(combined);
                ok = false;
                break;
            }
            memcpy(combined + existing, buf, (uint32_t)rd);
            bool wrote = file_write(dst_path, combined, (uint32_t)existing + (uint32_t)rd);
            kfree(combined);
            if (!wrote) {
                ok = false;
                break;
            }
        }
        offset += (uint32_t)rd;
    }
    kfree(buf);
    return ok;
}

bool file_rename(const char *oldpath, const char *newpath)
{
    char old_resolved[PATH_MAX_LEN], old_backend[PATH_MAX_LEN];
    char new_resolved[PATH_MAX_LEN], new_backend[PATH_MAX_LEN];
    bool ok = false;
    fs_type_t old_fs, new_fs;

    if (!resolve_path(oldpath, old_resolved, old_backend)) {
        return false;
    }
    old_fs = g_current_fs;

    if (!resolve_path(newpath, new_resolved, new_backend)) {
        return false;
    }
    new_fs = g_current_fs;

    if (!file_uac_allow(old_resolved, true) || !file_uac_allow(new_resolved, true) ||
        !fs_perm_check(old_resolved, true, false) || !fs_perm_check(new_resolved, true, false)) {
        return false;
    }

    /* Cross-filesystem: copy then delete (regular files only). */
    if (old_fs != new_fs) {
        if (file_is_dir(oldpath)) {
            return false;
        }
        if (file_exists(newpath)) {
            return false;
        }
        if (!file_copy_regular(old_resolved, new_resolved)) {
            return false;
        }
        if (!file_delete(old_resolved)) {
            return false;
        }
        fs_cache_invalidate_all();
        return true;
    }

    /* Permission check on old path (write access = modify) */
    if (!file_uac_allow(old_resolved, true)) {
        return false;
    }
    if (!fs_perm_check(old_resolved, true, false)) {
        return false;
    }

    switch (old_fs) {
        case FS_TYPE_FAT32:   ok = fat32_rename(old_backend, new_backend); break;
        case FS_TYPE_NTFS:    ok = ntfs_rename(old_backend, new_backend); break;
        case FS_TYPE_EXTFS:   ok = extfs_rename(old_backend, new_backend); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_stat(const char *path, file_stat_t *stat_out)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (stat_out == NULL) {
        return false;
    }
    memset(stat_out, 0, sizeof(*stat_out));

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    /* Permission check: read access */
    if (!file_uac_allow(resolved, false)) {
        return false;
    }
    if (!fs_perm_check(resolved, false, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32: {
            fat32_stat_t f32_stat;
            if (!fat32_stat(backend_path, &f32_stat)) {
                return false;
            }
            stat_out->size = f32_stat.file_size;
            stat_out->is_dir = f32_stat.is_dir;
            stat_out->attr = f32_stat.attr;
            stat_out->create_time = file_fat_datetime_to_unix(f32_stat.create_date, f32_stat.create_time);
            stat_out->modify_time = file_fat_datetime_to_unix(f32_stat.write_date, f32_stat.write_time);
            stat_out->access_time = file_fat_datetime_to_unix(f32_stat.access_date, 0);
            return true;
        }
        case FS_TYPE_NTFS: {
            ntfs_stat_t nstat;
            if (!ntfs_stat(backend_path, &nstat)) {
                return false;
            }
            stat_out->size = nstat.file_size;
            stat_out->is_dir = nstat.is_dir;
            stat_out->attr = nstat.attr;
            stat_out->create_time = file_ntfs_time_to_unix(nstat.create_time);
            stat_out->modify_time = file_ntfs_time_to_unix(nstat.modify_time);
            stat_out->access_time = file_ntfs_time_to_unix(nstat.access_time);
            return true;
        }
        case FS_TYPE_EXTFS: {
            extfs_stat_t estat;
            if (!extfs_stat(backend_path, &estat)) {
                return false;
            }
            stat_out->size = estat.file_size;
            stat_out->is_dir = estat.is_dir;
            stat_out->attr = (estat.mode & 040000) ? 0x10 : 0x20;
            stat_out->create_time = estat.create_time;
            stat_out->modify_time = estat.modify_time;
            stat_out->access_time = estat.access_time;
            return true;
        }
        default:
            return false;
    }
}

bool file_set_attr(const char *path, uint8_t attr_mask, uint8_t attr_value)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (path == NULL) {
        return false;
    }
    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    if (!file_uac_allow(resolved, true) || !fs_perm_check(resolved, true, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:
            return fat32_set_attr(backend_path, attr_mask, attr_value);
        default:
            return false;
    }
}

bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    /* UAC 鏉冮檺妫€鏌ワ紙NULL 閽╁瓙=鏀捐锛?*/
    if (!file_uac_allow(resolved, false)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_NETFS:   return netfs_list_dir(backend_path, buffer, buffer_size);
        default:              return false;
    }
}

uint16_t file_root_entry_count(void)
{
    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_root_entry_count();
        case FS_TYPE_FAT16:   return fat16_root_entry_count();
        case FS_TYPE_ISO9660: return iso9660_root_entry_count();
        case FS_TYPE_NTFS:    return ntfs_root_entry_count();
        case FS_TYPE_EXTFS:   return extfs_root_entry_count();
        case FS_TYPE_NETFS:   return 0;
        default:              return 0;
    }
}

/* ============================================================
 *  鍒濆鍖栧拰鐘舵€?
 * ============================================================ */

bool file_init(void)
{
    int32_t i;

    fs_cache_init();

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        g_mount_points[i].mounted = false;
        g_mount_points[i].fs_type = FS_TYPE_NONE;
        g_mount_points[i].path[0] = '\0';
        g_mount_points[i].partition = -1;
        g_mount_points[i].blockdev_index = -1;
    }
    g_mount_count = 0;
    g_pending_mount_partition = -1;
    g_probe_blockdev = -1;
    g_pending_blockdev = -1;
    g_current_fs = FS_TYPE_NONE;
    g_current_mount_path[0] = '\0';
    g_current_mount_partition = -1;
    g_current_blockdev = -1;

    return true;
}

const char *file_backend_name(void)
{
    return fs_type_to_name(g_current_fs);
}

/* ============================================================
 *  纾佺洏绌洪棿鏌ヨ
 * ============================================================ */

bool file_disk_space(const char *path, disk_space_t *info)
{
    char resolved[PATH_MAX_LEN];
    int32_t mount_idx;
    const char *relative_path;

    if (info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));

    if (!path_resolve(PATH_ROOT, path, resolved, PATH_MAX_LEN)) {
        return false;
    }
    mount_idx = find_mount_point(resolved);
    if (mount_idx < 0) {
        return false;
    }
    relative_path = resolved;
    if (!switch_to_mount(mount_idx, &relative_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:
            info->total_bytes = fat32_total_bytes(&info->free_bytes, &info->cluster_size);
            break;
        case FS_TYPE_FAT16:
            info->total_bytes = fat16_total_bytes(&info->free_bytes, &info->cluster_size);
            break;
        case FS_TYPE_ISO9660: {
            const iso9660_info_t *iso = iso9660_info();

            if (!iso->present || iso->block_size == 0) {
                return false;
            }
            info->cluster_size = iso->block_size;
            info->total_bytes = (uint64_t) iso->total_blocks * iso->block_size;
            info->free_bytes = 0;
            break;
        }
        case FS_TYPE_NTFS: {
            const ntfs_info_t *ntfs = ntfs_info();

            if (!ntfs->present || ntfs->bytes_per_sector == 0) {
                return false;
            }
            info->cluster_size = ntfs->cluster_size;
            info->total_bytes = ntfs->total_sectors * ntfs->bytes_per_sector;
            info->free_bytes = ntfs_free_bytes();
            break;
        }
        case FS_TYPE_EXTFS: {
            const extfs_info_t *ext = extfs_info();

            if (!ext->present || ext->block_size == 0) {
                return false;
            }
            info->cluster_size = ext->block_size;
            info->total_bytes = (uint64_t) ext->blocks_count * ext->block_size;
            info->free_bytes = (uint64_t) ext->free_blocks * ext->block_size;
            break;
        }
        default:
            return false;
    }
    return info->total_bytes > 0;
}


/* ============================================================
 *  鑷姩鎺㈡祴鎸傝浇锛堝吋瀹规棫琛屼负锛?
 * ============================================================ */

static bool file_partition_type_matches(uint8_t partition_type, fs_type_t fs)
{
    if (partition_type == 0) {
        return false;
    }
    switch (fs) {
        case FS_TYPE_FAT16:
            return partition_type == 0x04 || partition_type == 0x06 ||
                   partition_type == 0x0E;
        case FS_TYPE_FAT32:
            return partition_type == 0x0B || partition_type == 0x0C ||
                   partition_type == 0xEF;
        case FS_TYPE_NTFS:
            return partition_type == 0x07;
        case FS_TYPE_EXTFS:
            return partition_type == 0x83;
        default:
            return true;
    }
}

static bool file_ata_gpt_read_sector(uint64_t lba, void *buf)
{
    return file_ata_read_sector((uint32_t) lba, buf);
}

static int g_gpt_blockdev_index = 0;
static bool file_blockdev_gpt_read_sector(uint64_t lba, void *buf)
{
    return blockdev_read_sector(g_gpt_blockdev_index, lba, buf);
}

static uint32_t file_collect_gpt_candidates(gpt_read_sector_fn read_sec,
                                            file_volume_candidate_t *candidates,
                                            uint32_t capacity)
{
    gpt_header_t header;
    gpt_part_entry_t entries[FILE_MAX_VOLUME_CANDIDATES];
    int gpt_count;
    uint32_t count = 0;

    if (candidates == NULL || capacity == 0) {
        return 0;
    }
    if (!gpt_read_header(read_sec, &header)) {
        return 0;
    }
    gpt_count = gpt_read_partitions(read_sec, &header, entries,
                                    FILE_MAX_VOLUME_CANDIDATES);
    for (int i = 0; i < gpt_count && count < capacity; i++) {
        if (gpt_partition_fs_type(&entries[i]) == FS_TYPE_NONE ||
            entries[i].first_lba == 0 ||
            entries[i].first_lba > 0x7FFFFFFFU) {
            continue;
        }
        candidates[count].lba = (int32_t) entries[i].first_lba;
        candidates[count].type = 0;
        count++;
    }
    return count;
}
__attribute__((unused)) static uint32_t file_collect_volume_candidates(file_volume_candidate_t *candidates,
                                               uint32_t capacity)
{
    uint8_t sector[512];
    uint32_t count = 0;

    if (candidates == NULL || capacity == 0 ||
        !file_ata_read_sector(0, sector) ||
        file_read_le16(sector + 510) != 0xAA55) {
        if (candidates != NULL && capacity > 0) {
            candidates[0].lba = -1;
            candidates[0].type = 0;
        }
        return 1;
    }

    if (gpt_detect(sector)) {
        uint32_t gpt_count = file_collect_gpt_candidates(file_ata_gpt_read_sector,
                                                         candidates, capacity);
        if (gpt_count == 0) {
            candidates[0].lba = -1;
            candidates[0].type = 0;
            return 1;
        }
        return gpt_count;
    }

    for (uint8_t i = 0; i < FILE_MBR_PARTITIONS && count < capacity; i++) {
        uint8_t *entry = sector + 446 + (uint32_t) i * 16;
        uint8_t type = entry[4];
        uint32_t lba = file_read_le32(entry + 8);
        uint32_t sectors = file_read_le32(entry + 12);

        if (type == 0 || lba == 0 || sectors == 0 || lba > 0x7FFFFFFFU) {
            continue;
        }
        candidates[count].lba = (int32_t) lba;
        candidates[count].type = type;
        count++;
    }

    if (count == 0) {
        candidates[0].lba = -1;
        candidates[0].type = 0;
        return 1;
    }
    return count;
}

static uint32_t file_collect_volume_candidates_from_device(int dev_idx,
                                                           file_volume_candidate_t *candidates,
                                                           uint32_t capacity)
{
    uint8_t sector[512];
    uint32_t count = 0;
    bool read_ok;
    bool sig_ok;

    if (candidates == NULL || capacity == 0) {
        return 1;
    }

    /* 挂载失败最常见的形态是"什么都挂不上"，而失败原因在日志里完全不可见。
     * 这里把三个关键判据打出来：底层扇区读是否成功、0xAA55 引导签名是否
     * 存在、以及后面走的是 GPT 还是 MBR 分支。 */
    read_ok = blockdev_read_sector(dev_idx, 0, sector);
    sig_ok = read_ok && file_read_le16(sector + 510) == 0xAA55;
    kernel_log_hex_u32("fs: blkdev idx ", (uint32_t) dev_idx);
    log_write_bool_event("fs: blkdev read lba0", read_ok);
    log_write_bool_event("fs: blkdev signature 55AA", sig_ok);

    if (!sig_ok) {
        candidates[0].lba = -1;
        candidates[0].type = 0;
        return 1;
    }

    if (gpt_detect(sector)) {
        uint32_t gpt_count;
        log_write("fs: blkdev has GPT header");
        g_gpt_blockdev_index = dev_idx;
        gpt_count = file_collect_gpt_candidates(file_blockdev_gpt_read_sector,
                                                 candidates, capacity);
        if (gpt_count == 0) {
            candidates[0].lba = -1;
            candidates[0].type = 0;
            return 1;
        }
        return gpt_count;
    }

    for (uint8_t i = 0; i < FILE_MBR_PARTITIONS && count < capacity; i++) {
        uint8_t *entry = sector + 446 + (uint32_t) i * 16;
        uint8_t type = entry[4];
        uint32_t lba = file_read_le32(entry + 8);
        uint32_t sectors = file_read_le32(entry + 12);

        if (type == 0 || lba == 0 || sectors == 0 || lba > 0x7FFFFFFFU) {
            continue;
        }
        candidates[count].lba = (int32_t) lba;
        candidates[count].type = type;
        count++;
    }

    if (count == 0) {
        /* 0xAA55 存在但没有可用分区项：典型的"裸卷"（例如 mkfat32 直接生成
         * 的 FAT32 镜像）。此时回落到 lba = -1，由 fs 驱动从 LBA 0 读 BPB。 */
        log_write("fs: blkdev no MBR partition entries (raw volume)");
        candidates[0].lba = -1;
        candidates[0].type = 0;
        return 1;
    }
    kernel_log_hex_u32("fs: blkdev MBR candidates ", count);
    return count;
}

static bool file_try_mount_drive(char drive, const char *fs_name, int32_t partition)
{
    char mount_path[MAX_MOUNT_PATH];

    return file_mount_path_for_drive(drive, mount_path) &&
           file_mount(mount_path, fs_name, partition);
}

static bool file_try_mount_candidate(char drive, const file_volume_candidate_t *candidate)
{
    static const struct {
        fs_type_t type;
        const char *name;
    } fs_order[] = {
        { FS_TYPE_FAT32, "fat32" },
        { FS_TYPE_FAT16, "fat16" },
        { FS_TYPE_NTFS,  "ntfs"  },
        { FS_TYPE_EXTFS, "extfs" },
    };

    for (uint32_t i = 0; i < sizeof(fs_order) / sizeof(fs_order[0]); i++) {
        if (candidate->type != 0 &&
            !file_partition_type_matches(candidate->type, fs_order[i].type)) {
            continue;
        }
        if (file_try_mount_drive(drive, fs_order[i].name, candidate->lba)) {
            return true;
        }
    }
    return false;
}

/* 把整块设备当作"裸卷"挂载（卷起始于 LBA 0，由 fs 驱动自己读 BPB）。
 * 仅在没有分区表、或分区候选全部挂不上时使用。 */
static bool file_try_mount_raw_volume(char drive)
{
    static const char * const fs_order[] = { "fat32", "fat16", "ntfs", "extfs" };

    for (uint32_t i = 0; i < sizeof(fs_order) / sizeof(fs_order[0]); i++) {
        if (file_try_mount_drive(drive, fs_order[i], -1)) {
            return true;
        }
    }
    return false;
}

bool file_auto_mount(void)
{
    file_volume_candidate_t candidates[FILE_MAX_VOLUME_CANDIDATES];
    uint32_t candidate_count;
    char drive = FILE_AUTO_DRIVE_FIRST;
    bool mounted = false;
    int dev_count;

    (void) blockdev_init();

    if (file_try_mount_drive(drive, "iso9660", -1)) {
        if (file_exists(PATH_ROOT "INSTALL.FLG")) {
            mounted = true;
            drive++;
        } else {
            file_umount(PATH_ROOT);
        }
    }

    dev_count = blockdev_count();
    kernel_log_hex_u32("fs: blockdev count ", (uint32_t) dev_count);
    for (int dev_idx = 0; dev_idx < dev_count && drive <= FILE_AUTO_DRIVE_LAST; dev_idx++) {
        const blockdev_t *dev = blockdev_get(dev_idx);
        bool dev_mounted = false;

        if (dev == NULL || dev->type == BLOCKDEV_CDROM) {
            continue;
        }
        log_write_event("fs: blockdev", dev->name);
        /* 告诉 fs 驱动该从这块设备读写；随后 file_mount() 会把它记进挂载点，
         * 供后续切换挂载点时重新初始化复用。 */
        g_probe_blockdev = dev_idx;

        candidate_count = file_collect_volume_candidates_from_device(
            dev_idx, candidates,
            sizeof(candidates) / sizeof(candidates[0]));

        for (uint32_t i = 0; i < candidate_count && drive <= FILE_AUTO_DRIVE_LAST; i++) {
            if (file_try_mount_candidate(drive, &candidates[i])) {
                mounted = true;
                dev_mounted = true;
                drive++;
            }
        }

        /* 裸卷回退。
         *
         * 本项目的镜像（hd.img / hd_uefi.img）是**不带分区表**的裸 FAT32 卷：
         * 引导扇区本身就是 VBR，BPB 直接位于 LBA 0。而 VBR 的 446..509 字节
         * 按 MBR 布局看正好是"分区表"的位置，实际却是引导代码 —— 其中经常
         * 出现非零的 type/lba/sectors，会被上面的分区解析当成一个合法分区项，
         * 于是去挂载一个根本不存在的分区并失败，导致整个文件系统挂不上、
         * 驱动签名校验拿不到 kernel.exe 而 BSOD。
         *
         * 因此：只有分区候选一个都没挂上时，才回落到"整盘 LBA 0"。放在分区
         * 之后，正常分区盘的行为完全不变，也不会重复挂载同一个卷。 */
        if (!dev_mounted && drive <= FILE_AUTO_DRIVE_LAST &&
            file_try_mount_raw_volume(drive)) {
            log_write("fs: mounted raw volume (no partition table)");
            mounted = true;
            drive++;
        }
    }

    /* 设备探测结束，后续的显式挂载不再绑定块设备提示。 */
    g_probe_blockdev = -1;

    if (drive <= FILE_AUTO_DRIVE_LAST && file_try_mount_drive(drive, "iso9660", -1)) {
        mounted = true;
    }

    log_write_bool_event("fs: auto mount", mounted);
    return mounted;
}

/* ============================================================
 *  鐩樼淇℃伅鏌ヨ API
 * ============================================================ */

static char drive_upper(char drive)
{
    if (drive >= 'a' && drive <= 'z') {
        return drive - 'a' + 'A';
    }
    return drive;
}

bool file_get_drive_info(char drive, drive_info_t *info)
{
    char mount_path[8];
    uint32_t i;
    char upper_drive;

    if (info == NULL) {
        return false;
    }

    memset(info, 0, sizeof(drive_info_t));
    upper_drive = drive_upper(drive);
    info->drive = upper_drive;

    mount_path[0] = upper_drive;
    mount_path[1] = ':';
    mount_path[2] = PATH_SEPARATOR;
    mount_path[3] = '\0';

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
            info->mounted = true;
            info->fs_type = g_mount_points[i].fs_type;

            disk_space_t space;
            if (file_disk_space(mount_path, &space)) {
                info->total_bytes = space.total_bytes;
                info->free_bytes = space.free_bytes;
            }
            return true;
        }
    }

    return false;
}

bool file_drive_mounted(char drive)
{
    drive_info_t info;
    return file_get_drive_info(drive, &info) && info.mounted;
}

int32_t file_get_mounted_drives(char *drives, uint32_t max_drives)
{
    uint32_t i;
    uint32_t count = 0;

    if (drives == NULL || max_drives == 0) {
        return 0;
    }

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted && g_mount_points[i].path[1] == ':') {
            if (count < max_drives) {
                drives[count] = g_mount_points[i].path[0];
                count++;
            }
        }
    }

    return (int32_t) count;
}

/* ============================================================
 *  鏍煎紡鍖?/ 妫€鏌?
 * ============================================================ */

bool file_format_drive(char drive, const char *fs_type)
{
    char mount_path[MAX_MOUNT_PATH];
    char upper;
    int32_t i;
    int32_t found = -1;
    int32_t volume_lba;
    uint8_t mbr[512];
    uint64_t sector_count = 0;

    (void) fs_type;

    upper = drive_upper(drive);
    if (upper < 'A' || upper > 'Z') {
        return false;
    }

    mount_path[0] = upper;
    mount_path[1] = ':';
    mount_path[2] = PATH_SEPARATOR;
    mount_path[3] = '\0';

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return false;
    }

    volume_lba = g_mount_points[found].partition;
    if (volume_lba < 0) {
        return false;
    }

    if (!file_ata_read_sector(0, mbr)) {
        return false;
    }
    for (uint8_t p = 0; p < FILE_MBR_PARTITIONS; p++) {
        uint8_t *entry = mbr + 446 + (uint32_t) p * 16;
        uint32_t lba = file_read_le32(entry + 8);
        uint32_t cnt = file_read_le32(entry + 12);
        if (lba == (uint32_t) volume_lba && cnt != 0U) {
            sector_count = cnt;
            break;
        }
    }
    if (sector_count == 0U) {
        return false;
    }

    return fat32_format((uint32_t) volume_lba, sector_count, 512U);
}

bool file_chkdsk(const char *path, uint32_t *errors_out, uint32_t *fixed_out,
                 uint32_t *files_out, uint64_t *kb_out, uint32_t *serial_out)
{
    char resolved[PATH_MAX_LEN];
    char backend[PATH_MAX_LEN];
    fat32_chkdsk_report_t report;

    if (!resolve_path(path, resolved, backend)) {
        return false;
    }
    if (g_current_fs != FS_TYPE_FAT32) {
        return false;
    }
    if (!fat32_chkdsk(&report)) {
        return false;
    }

    if (errors_out != NULL) { *errors_out = report.errors; }
    if (fixed_out != NULL)  { *fixed_out = report.fixed; }
    if (files_out != NULL)  { *files_out = report.files; }
    if (kb_out != NULL)     { *kb_out = report.total_kb; }
    if (serial_out != NULL) { *serial_out = report.volume_serial; }
    return true;
}

/* ============================================================
 *  绗﹀彿閾炬帴鍏叡 API
 * ============================================================ */

bool file_symlink(const char *target, const char *linkpath)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (target == NULL || linkpath == NULL) {
        return false;
    }
    if (!resolve_path(linkpath, resolved, backend_path)) {
        return false;
    }
    switch (g_current_fs) {
        case FS_TYPE_FAT32:  return fat32_symlink(target, backend_path);
        default:             return false;
    }
}

int32_t file_readlink(const char *path, char *buffer, uint32_t size)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (buffer == NULL || size == 0 || !resolve_path(path, resolved, backend_path)) {
        return -1;
    }
    switch (g_current_fs) {
        case FS_TYPE_FAT32:  return fat32_read_symlink(backend_path, buffer, size);
        case FS_TYPE_EXTFS:  return extfs_read_symlink(backend_path, buffer, size);
        default:             return -1;
    }
}

bool file_is_symlink(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }
    return file_backend_is_symlink(backend_path);
}

/* ============================================================
 *  纾佺洏閰嶉锛坧er-drive锛屽唴瀛樹腑淇濆瓨锛?
 * ============================================================ */

static disk_quota_t g_quotas[26];

bool file_set_quota(char drive, uint64_t limit_bytes)
{
    char upper = drive_upper(drive);

    if (upper < 'A' || upper > 'Z') {
        return false;
    }
    g_quotas[upper - 'A'].drive = upper;
    g_quotas[upper - 'A'].limit_bytes = limit_bytes;
    return true;
}

bool file_get_quota(char drive, disk_quota_t *info)
{
    char upper = drive_upper(drive);
    char mount_path[8];
    disk_space_t space;

    if (info == NULL || upper < 'A' || upper > 'Z') {
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->drive = upper;
    info->limit_bytes = g_quotas[upper - 'A'].limit_bytes;

    mount_path[0] = upper;
    mount_path[1] = ':';
    mount_path[2] = PATH_SEPARATOR;
    mount_path[3] = '\0';
    if (file_disk_space(mount_path, &space)) {
        info->used_bytes = (space.total_bytes > space.free_bytes)
                           ? space.total_bytes - space.free_bytes : 0;
    }
    return true;
}

bool file_check_quota(char drive, uint64_t write_size)
{
    char upper = drive_upper(drive);
    disk_quota_t info;

    if (upper < 'A' || upper > 'Z') {
        return true;
    }
    if (g_quotas[upper - 'A'].limit_bytes == 0U) {
        return true;
    }
    if (!file_get_quota(upper, &info)) {
        return true;
    }
    return info.used_bytes + write_size <= info.limit_bytes;
}

/* ============================================================
 *  鍥炴敹绔?(Recycle Bin)
 * ============================================================ */

#define FILE_RECYCLE_DIR_NAME  "$RECYCLE.BIN"
#define FILE_RECYCLE_BUF_SIZE  65536U

static uint8_t g_recycle_buf[FILE_RECYCLE_BUF_SIZE];

const char *file_recycle_path(char drive)
{
    static char path[32];
    char upper = drive_upper(drive);
    uint32_t i = 3;
    const char *name = FILE_RECYCLE_DIR_NAME;

    path[0] = upper;
    path[1] = ':';
    path[2] = PATH_SEPARATOR;
    while (*name != '\0' && i + 1U < sizeof(path)) {
        path[i++] = *name++;
    }
    path[i] = '\0';
    return path;
}

static const char *file_basename_of(const char *path)
{
    const char *base = path;

    while (*path != '\0') {
        if (*path == PATH_SEPARATOR) {
            base = path + 1;
        }
        path++;
    }
    return base;
}

static void file_join_path(char *out, uint32_t out_size,
                           const char *dir, const char *name)
{
    uint32_t len;

    out[0] = '\0';
    if (dir == NULL || name == NULL || out_size == 0U) {
        return;
    }
    strlcpy(out, dir, out_size);
    len = (uint32_t) strlen(out);
    if (len > 3U && out[len - 1U] != PATH_SEPARATOR && len + 1U < out_size) {
        out[len++] = PATH_SEPARATOR;
        out[len] = '\0';
    }
    if (len + (uint32_t) strlen(name) + 1U < out_size) {
        strcpy(out + len, name);
    }
}

bool file_send_to_recycle_bin(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend[PATH_MAX_LEN];
    char recycle_dir[PATH_MAX_LEN];
    char candidate[PATH_MAX_LEN];
    char final_dest[PATH_MAX_LEN];
    const char *base;
    const char *dot;
    char stem[40];
    const char *ext = "";
    int32_t size;

    if (!resolve_path(path, resolved, backend)) {
        return false;
    }
    if (file_is_dir(resolved)) {
        return false;
    }

    strcpy(recycle_dir, file_recycle_path(resolved[0]));
    if (!file_exists(recycle_dir)) {
        file_mkdir(recycle_dir);
    }
    if (!file_is_dir(recycle_dir)) {
        return false;
    }

    base = file_basename_of(resolved);
    dot = strrchr(base, '.');
    if (dot != NULL && dot != base) {
        uint32_t stem_len = (uint32_t) (dot - base);
        if (stem_len >= sizeof(stem)) {
            stem_len = sizeof(stem) - 1U;
        }
        memcpy(stem, base, stem_len);
        stem[stem_len] = '\0';
        ext = dot;
    } else {
        strlcpy(stem, base, sizeof(stem));
    }

    file_join_path(final_dest, sizeof(final_dest), recycle_dir, base);
    if (!file_exists(final_dest)) {
        strcpy(candidate, final_dest);
    } else {
        uint32_t suffix = 1;
        bool got = false;
        while (suffix <= 999U) {
            char name[64];
            uint32_t n = (uint32_t) strlen(stem);
            uint32_t j = 0;

            memcpy(name, stem, n);
            name[n++] = '(';
            {
                char tmp[8];
                uint32_t t = 0;
                uint32_t s = suffix;
                if (s == 0U) {
                    tmp[t++] = '0';
                }
                while (s > 0U && t < sizeof(tmp)) {
                    tmp[t++] = (char) ('0' + (s % 10U));
                    s /= 10U;
                }
                while (t > 0) {
                    name[n++] = tmp[--t];
                }
            }
            name[n++] = ')';
            while (ext[j] != '\0' && n + 1U < sizeof(name)) {
                name[n++] = ext[j++];
            }
            name[n] = '\0';
            file_join_path(candidate, sizeof(candidate), recycle_dir, name);
            if (!file_exists(candidate)) {
                got = true;
                break;
            }
            suffix++;
        }
        if (!got) {
            return false;
        }
    }

    size = file_size(resolved);
    if (size <= 0 || (uint32_t) size > FILE_RECYCLE_BUF_SIZE) {
        return false;
    }
    if (file_read(resolved, g_recycle_buf, (uint32_t) size) != size) {
        return false;
    }
    if (file_write(candidate, g_recycle_buf, (uint32_t) size) != size) {
        return false;
    }
    if (!file_delete(resolved)) {
        file_delete(candidate);
        return false;
    }
    file_recycle_record(file_basename_of(candidate), resolved, (uint32_t) size);
    file_recycle_maintenance(resolved[0]);
    return true;
}

bool file_restore_from_recycle_bin(const char *recycled_path, const char *original_path)
{
    int32_t size;

    if (recycled_path == NULL || original_path == NULL) {
        return false;
    }
    if (file_exists(original_path)) {
        return false;
    }
    size = file_size(recycled_path);
    if (size <= 0 || (uint32_t) size > FILE_RECYCLE_BUF_SIZE) {
        return false;
    }
    if (file_read(recycled_path, g_recycle_buf, (uint32_t) size) != size) {
        return false;
    }
    if (file_write(original_path, g_recycle_buf, (uint32_t) size) != size) {
        return false;
    }
    if (!file_delete(recycled_path)) {
        return false;
    }
    return true;
}

static void file_walk_recycle(char drive,
                              void (*fn)(const char *full, bool is_dir, void *ctx),
                              void *ctx)
{
    static char buffer[2048];
    char recycle_dir[PATH_MAX_LEN];
    uint32_t i = 0;

    strcpy(recycle_dir, file_recycle_path(drive));
    if (!file_is_dir(recycle_dir)) {
        return;
    }
    if (!file_list_dir(recycle_dir, buffer, sizeof(buffer))) {
        return;
    }
    while (buffer[i] != '\0') {
        uint32_t start = i;
        char name[64];
        char full[PATH_MAX_LEN];
        uint32_t len;
        bool is_dir;

        while (buffer[i] != '\0' && buffer[i] != '\n') {
            i++;
        }
        len = i - start;
        if (len > 0U) {
            if (len >= sizeof(name)) {
                len = sizeof(name) - 1U;
            }
            memcpy(name, buffer + start, len);
            name[len] = '\0';
            is_dir = (name[len - 1U] == PATH_SEPARATOR);
            if (is_dir) {
                name[len - 1U] = '\0';
            }
            if (name[0] != '\0') {
                file_join_path(full, sizeof(full), recycle_dir, name);
                fn(full, is_dir, ctx);
            }
        }
        if (buffer[i] == '\n') {
            i++;
        }
    }
}

static void file_empty_recycle_entry(const char *full, bool is_dir, void *ctx)
{
    (void) ctx;
    if (is_dir) {
        file_rmdir(full);
    } else {
        file_delete(full);
    }
}

bool file_empty_recycle_bin(char drive)
{
    file_walk_recycle(drive, file_empty_recycle_entry, NULL);
    return true;
}

static void file_accumulate_size(const char *full, bool is_dir, void *ctx)
{
    uint64_t *total = (uint64_t *) ctx;
    int32_t s;

    (void) is_dir;
    s = file_size(full);
    if (s > 0) {
        *total += (uint64_t) s;
    }
}

uint64_t file_recycle_bin_size(char drive)
{
    uint64_t total = 0;

    file_walk_recycle(drive, file_accumulate_size, &total);
    return total;
}

/* ============================================================
 *  缃戠粶椹卞姩鍣ㄦ槧灏?(net use / \\server\share)
 * ============================================================ */

typedef struct {
    bool used;
    char host[64];
    uint16_t port;
    char share[64];
} net_mapping_t;

static net_mapping_t g_net_map[26];

static bool file_net_already_mapped(void)
{
    int32_t i;

    for (i = 0; i < 26; i++) {
        if (g_net_map[i].used) {
            return true;
        }
    }
    return false;
}

bool file_map_network_drive(char drive, const char *server, uint16_t port, const char *share)
{
    char upper = drive_upper(drive);
    char mount_path[8];
    int32_t i;
    int32_t empty_idx = -1;

    if (upper < 'A' || upper > 'Z' ||
        server == NULL || server[0] == '\0' ||
        share == NULL || share[0] == '\0') {
        return false;
    }
    if (g_net_map[upper - 'A'].used) {
        return false;
    }
    if (file_net_already_mapped()) {
        return false;
    }

    mount_path[0] = upper;
    mount_path[1] = ':';
    mount_path[2] = PATH_SEPARATOR;
    mount_path[3] = '\0';

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
            return false;
        }
        if (!g_mount_points[i].mounted && empty_idx < 0) {
            empty_idx = i;
        }
    }
    if (empty_idx < 0) {
        return false;
    }

    if (!netfs_init(server, port, share)) {
        return false;
    }

    strlcpy(g_mount_points[empty_idx].path, mount_path,
            sizeof(g_mount_points[empty_idx].path));
    g_mount_points[empty_idx].fs_type = FS_TYPE_NETFS;
    g_mount_points[empty_idx].partition = -1;
    g_mount_points[empty_idx].mounted = true;
    g_mount_count++;

    g_net_map[upper - 'A'].used = true;
    strlcpy(g_net_map[upper - 'A'].host, server,
            sizeof(g_net_map[upper - 'A'].host));
    g_net_map[upper - 'A'].port = (port == 0u) ? NETFS_DEFAULT_PORT : port;
    strlcpy(g_net_map[upper - 'A'].share, share,
            sizeof(g_net_map[upper - 'A'].share));

    fs_cache_invalidate_all();
    return true;
}

bool file_unmap_network_drive(char drive)
{
    char upper = drive_upper(drive);
    char mount_path[8];
    int32_t i;
    bool removed = false;

    if (upper < 'A' || upper > 'Z') {
        return false;
    }
    if (!g_net_map[upper - 'A'].used) {
        return false;
    }

    mount_path[0] = upper;
    mount_path[1] = ':';
    mount_path[2] = PATH_SEPARATOR;
    mount_path[3] = '\0';

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            g_mount_points[i].fs_type == FS_TYPE_NETFS &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
            if (strcmp(g_current_mount_path, mount_path) == 0) {
                g_current_fs = FS_TYPE_NONE;
                g_current_mount_path[0] = '\0';
                g_current_mount_partition = -1;
            }
            g_mount_points[i].mounted = false;
            g_mount_points[i].fs_type = FS_TYPE_NONE;
            g_mount_points[i].path[0] = '\0';
            g_mount_count--;
            removed = true;
            break;
        }
    }

    g_net_map[upper - 'A'].used = false;
    g_net_map[upper - 'A'].host[0] = '\0';
    g_net_map[upper - 'A'].share[0] = '\0';
    g_net_map[upper - 'A'].port = 0;

    netfs_disconnect();
    fs_cache_invalidate_all();
    return removed;
}

bool file_get_network_mapping(char drive, char *host_out, uint16_t *port_out, char *share_out)
{
    char upper = drive_upper(drive);

    if (upper < 'A' || upper > 'Z' || !g_net_map[upper - 'A'].used) {
        return false;
    }
    if (host_out != NULL) {
        strlcpy(host_out, g_net_map[upper - 'A'].host, 64);
    }
    if (port_out != NULL) {
        *port_out = g_net_map[upper - 'A'].port;
    }
    if (share_out != NULL) {
        strlcpy(share_out, g_net_map[upper - 'A'].share, 64);
    }
    return true;
}

/* ============================================================
 *  One-click raw partition backup (VFS layer)
 *
 *  The backup app streams the whole mounted FAT32 volume to/from a raw
 *  image file in chunks, so a multi-MB image never has to be staged in
 *  RAM.  Source read / restore write go straight to the volume's raw
 *  sectors (volume-relative LBA), while the image file is built with the
 *  append-only streaming writer.
 * ============================================================ */

int64_t file_backup_begin(const char *image_path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];
    uint32_t start_lba;
    uint64_t total;
    uint32_t sector_size;
    uint32_t cluster_sectors;

    if (g_current_fs != FS_TYPE_FAT32 || image_path == NULL) {
        return -1;
    }
    if (!file_follow_links(image_path, resolved, backend_path)) {
        return -2;
    }
    if (fat32_append_open(backend_path) < 0) {
        return -3;
    }
    if (!fat32_volume_geometry(&start_lba, &total, &sector_size, &cluster_sectors)) {
        fat32_append_close();
        return -4;
    }
    return (int64_t) total;
}

int32_t file_backup_read_src(uint32_t rel_lba, uint32_t sectors, void *buf)
{
    if (g_current_fs != FS_TYPE_FAT32 || buf == NULL || sectors == 0) {
        return -1;
    }
    return fat32_read_volume(rel_lba, sectors, buf) ? 0 : -2;
}

int32_t file_backup_append(const void *buf, uint32_t len)
{
    if (g_current_fs != FS_TYPE_FAT32 || buf == NULL || len == 0) {
        return -1;
    }
    return fat32_append_write(buf, len);
}

int32_t file_backup_end(void)
{
    if (g_current_fs != FS_TYPE_FAT32) {
        return -1;
    }
    return fat32_append_close();
}

int64_t file_backup_restore_begin(const char *image_path)
{
    int32_t sz;

    if (g_current_fs != FS_TYPE_FAT32 || image_path == NULL) {
        return -1;
    }
    sz = file_size(image_path);
    return sz < 0 ? -2 : (int64_t) sz;
}

int32_t file_backup_read_img(const char *image_path, uint32_t offset, void *buf, uint32_t len)
{
    if (g_current_fs != FS_TYPE_FAT32 || image_path == NULL || buf == NULL) {
        return -1;
    }
    return file_read_at(image_path, offset, buf, len);
}

int32_t file_backup_write_src(uint32_t rel_lba, uint32_t sectors, const void *buf)
{
    if (g_current_fs != FS_TYPE_FAT32 || buf == NULL || sectors == 0) {
        return -1;
    }
    return fat32_write_volume(rel_lba, sectors, buf) ? 0 : -2;
}

int32_t file_backup_restore_end(void)
{
    return 0;
}

/* ============================================================
 *  鍥炴敹绔欑储寮?(recycle.idx): records original path, delete
 *  time (days), and size for every file sent to the Recycle Bin.
 *
 *  Also implements the policy:
 *    - size cap (default 64 MB, registry RecycleBin/MaxSizeMB):
 *      when exceeded, oldest entries are physically deleted.
 *    - age cap (default 30 days, registry RecycleBin/MaxAgeDays):
 *      entries older than this are purged.
 * ============================================================ */

#define RECYCLE_INDEX_MAX       128
#define RECYCLE_DEFAULT_MAX_MB  64U
#define RECYCLE_DEFAULT_AGE_DAYS 30U

typedef struct {
    char     recycled_name[40];
    char     original_path[260];
    uint64_t days;
    uint32_t size;
} recycle_entry_t;

static uint8_t g_recycle_index_buf[RECYCLE_INDEX_MAX * sizeof(recycle_entry_t)];

static void recycle_index_path(char *out, uint32_t out_size)
{
    strlcpy(out, "C:\\Monios\\System\\recycle.idx", out_size);
}

static uint64_t recycle_now_days(void)
{
    cmos_time_t t;
    uint32_t y, m, d, days = 0;
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

    cmos_read_time(&t);
    y = t.year;
    m = t.month;
    d = t.day;
    if (y < 2026U) y = 2026U;
    if (m < 1U) m = 1U;
    if (m > 12U) m = 12U;
    if (d < 1U) d = 1U;
    for (uint32_t yy = 2026U; yy < y; yy++) {
        days += (((yy % 4U) == 0U) ? 366U : 365U);
    }
    for (uint32_t mm = 1U; mm < m; mm++) {
        uint32_t dim = mdays[mm - 1U];
        if (mm == 2U && ((y % 4U) == 0U)) {
            dim = 29U;
        }
        days += dim;
    }
    days += (d - 1U);
    return days;
}

static uint32_t recycle_max_size_mb(void)
{
    const char *v = registry_get("RecycleBin/MaxSizeMB");
    uint32_t mb = RECYCLE_DEFAULT_MAX_MB;

    if (v != NULL && *v >= '0' && *v <= '9') {
        uint32_t n = 0;
        while (*v >= '0' && *v <= '9') {
            n = n * 10U + (uint32_t)(*v - '0');
            v++;
        }
        if (n > 0U && n <= 4096U) {
            mb = n;
        }
    }
    return mb;
}

static uint32_t recycle_max_age_days(void)
{
    const char *v = registry_get("RecycleBin/MaxAgeDays");
    uint32_t days = RECYCLE_DEFAULT_AGE_DAYS;

    if (v != NULL && *v >= '0' && *v <= '9') {
        uint32_t n = 0;
        while (*v >= '0' && *v <= '9') {
            n = n * 10U + (uint32_t)(*v - '0');
            v++;
        }
        if (n > 0U && n <= 3650U) {
            days = n;
        }
    }
    return days;
}

static int32_t recycle_index_load(recycle_entry_t *entries)
{
    char idx[PATH_MAX_LEN];
    int32_t n;
    int32_t count;

    recycle_index_path(idx, sizeof(idx));
    if (!file_exists(idx)) {
        return 0;
    }
    n = file_read(idx, g_recycle_index_buf, sizeof(g_recycle_index_buf));
    if (n <= 0) {
        return 0;
    }
    count = n / (int32_t) sizeof(recycle_entry_t);
    if (count > RECYCLE_INDEX_MAX) {
        count = RECYCLE_INDEX_MAX;
    }
    memcpy(entries, g_recycle_index_buf, (uint32_t) count * sizeof(recycle_entry_t));
    return count;
}

static bool recycle_index_save(const recycle_entry_t *entries, int32_t count)
{
    char idx[PATH_MAX_LEN];
    uint32_t bytes;

    recycle_index_path(idx, sizeof(idx));
    if (count <= 0) {
        file_write(idx, "", 0);
        return true;
    }
    bytes = (uint32_t) count * sizeof(recycle_entry_t);
    return file_write(idx, entries, bytes) == (int32_t) bytes;
}

void file_recycle_record(const char *recycled_name, const char *original_path, uint32_t size)
{
    recycle_entry_t entries[RECYCLE_INDEX_MAX];
    int32_t count = recycle_index_load(entries);

    if (count >= RECYCLE_INDEX_MAX) {
        /* drop oldest (front) if full */
        memmove(entries, entries + 1, (RECYCLE_INDEX_MAX - 1U) * sizeof(recycle_entry_t));
        count = RECYCLE_INDEX_MAX - 1;
    }
    memset(&entries[count], 0, sizeof(recycle_entry_t));
    strlcpy(entries[count].recycled_name, recycled_name, sizeof(entries[count].recycled_name));
    strlcpy(entries[count].original_path, original_path, sizeof(entries[count].original_path));
    entries[count].days = recycle_now_days();
    entries[count].size = size;
    recycle_index_save(entries, count + 1);
}

/* physically delete one recycled file by its in-bin name */
static void recycle_delete_file(char drive, const char *recycled_name)
{
    char full[PATH_MAX_LEN];
    file_join_path(full, sizeof(full), file_recycle_path(drive), recycled_name);
    file_delete(full);
}

void file_recycle_maintenance(char drive)
{
    recycle_entry_t entries[RECYCLE_INDEX_MAX];
    int32_t count = recycle_index_load(entries);
    uint64_t now_days = recycle_now_days();
    uint32_t max_bytes = (uint64_t) recycle_max_size_mb() * 1024U * 1024U;
    uint32_t max_age = recycle_max_age_days();
    int32_t w = 0;

    if (count <= 0) {
        return;
    }

    /* age-based purge + compute live total, compact in place */
    uint64_t live_total = 0;
    for (int32_t r = 0; r < count; r++) {
        bool expired = (now_days >= entries[r].days &&
                       (uint32_t)(now_days - entries[r].days) > max_age);
        if (expired) {
            recycle_delete_file(drive, entries[r].recycled_name);
            continue;
        }
        entries[w++] = entries[r];
        live_total += entries[r].size;
    }
    count = w;

    /* size cap: delete oldest (lowest days) first */
    while (live_total > max_bytes && count > 0) {
        int32_t oldest = 0;
        for (int32_t i = 1; i < count; i++) {
            if (entries[i].days < entries[oldest].days) {
                oldest = i;
            }
        }
        recycle_delete_file(drive, entries[oldest].recycled_name);
        live_total -= entries[oldest].size;
        entries[oldest] = entries[count - 1];
        count--;
    }

    recycle_index_save(entries, count);
}

int32_t file_recycle_restore_by_name(const char *recycled_name)
{
    recycle_entry_t entries[RECYCLE_INDEX_MAX];
    int32_t count = recycle_index_load(entries);
    char bin_path[PATH_MAX_LEN];
    char recycled_full[PATH_MAX_LEN];
    int32_t idx = -1;

    for (int32_t i = 0; i < count; i++) {
        if (strcmp(entries[i].recycled_name, recycled_name) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;   /* not in index */
    }

    strcpy(bin_path, file_recycle_path(entries[idx].original_path[0]));
    file_join_path(recycled_full, sizeof(recycled_full), bin_path, recycled_name);
    if (!file_exists(recycled_full)) {
        return -2;
    }

    /* restore to original path; on conflict, add (1)/(2)... suffix */
    {
        char target[PATH_MAX_LEN];
        strlcpy(target, entries[idx].original_path, sizeof(target));
        if (file_exists(target)) {
            const char *base = file_basename_of(target);
            char *dot = strrchr(base, '.');
            char stem[40];
            const char *ext = "";
            uint32_t n;
            if (dot != NULL && dot != base) {
                uint32_t sl = (uint32_t)(dot - base);
                if (sl >= sizeof(stem)) sl = sizeof(stem) - 1U;
                memcpy(stem, base, sl);
                stem[sl] = '\0';
                ext = dot;
            } else {
                strlcpy(stem, base, sizeof(stem));
            }
            n = (uint32_t)(base - target);
            for (uint32_t s = 1; s <= 999U; s++) {
                char nb[64];
                uint32_t nn = 0;
                char tmp[8]; uint32_t tt = 0; uint32_t ss = s;
                memcpy(nb, stem, strlen(stem)); nn = (uint32_t)strlen(stem);
                nb[nn++] = '(';
                if (ss == 0U) tmp[tt++] = '0';
                while (ss > 0U && tt < sizeof(tmp)) tmp[tt++] = (char)('0' + (ss % 10U)), ss /= 10U;
                while (tt > 0) nb[nn++] = tmp[--tt];
                nb[nn++] = ')';
                for (const char *e = ext; *e != '\0' && nn + 1U < sizeof(nb); ) nb[nn++] = *e++;
                nb[nn] = '\0';
                memcpy(target + n, nb, (uint32_t)strlen(nb) + 1U);
                if (!file_exists(target)) break;
            }
        }
        if (!file_restore_from_recycle_bin(recycled_full, target)) {
            return -3;
        }
    }

    /* drop entry from index */
    {
        recycle_entry_t entries2[RECYCLE_INDEX_MAX];
        int32_t c2 = recycle_index_load(entries2);
        int32_t w2 = 0;
        for (int32_t i = 0; i < c2; i++) {
            if (strcmp(entries2[i].recycled_name, recycled_name) != 0) {
                entries2[w2++] = entries2[i];
            }
        }
        recycle_index_save(entries2, w2);
    }
    return 0;
}

uint32_t file_recycle_list(char drive, char *out, uint32_t out_size)
{
    (void)drive;
    recycle_entry_t entries[RECYCLE_INDEX_MAX];
    int32_t count = recycle_index_load(entries);
    uint32_t pos = 0;

    out[0] = '\0';
    for (int32_t i = 0; i < count; i++) {
        uint32_t kb = entries[i].size / 1024U;
        char line[320];
        uint32_t lp = 0;

        line[0] = '\0';
        /* index */
        {
            char num[8]; uint32_t nn = 0; uint32_t v = (uint32_t)i;
            if (v == 0U) num[nn++] = '0';
            while (v > 0U && nn < sizeof(num)) num[nn++] = (char)('0' + (v % 10U)), v /= 10U;
            while (nn > 0U) line[lp++] = num[--nn];
        }
        line[lp++] = ' '; line[lp++] = ' '; line[lp++] = ' '; line[lp++] = ' ';
        for (const char *s = entries[i].recycled_name; *s != '\0' && lp < sizeof(line) - 1U; ) line[lp++] = *s++;
        line[lp++] = ' '; line[lp++] = ' ';
        {
            char num[12]; uint32_t nn = 0; uint32_t v = kb;
            if (v == 0U) num[nn++] = '0';
            while (v > 0U && nn < sizeof(num)) num[nn++] = (char)('0' + (v % 10U)), v /= 10U;
            while (nn > 0U) line[lp++] = num[--nn];
        }
        line[lp++] = ' '; line[lp++] = 'K'; line[lp++] = 'B'; line[lp++] = ' ';
        line[lp++] = '<'; line[lp++] = '-'; line[lp++] = ' ';
        for (const char *s = entries[i].original_path; *s != '\0' && lp < sizeof(line) - 2U; ) line[lp++] = *s++;
        line[lp++] = '\n';
        line[lp] = '\0';

        if (pos + lp + 1U >= out_size) break;
        strcpy(out + pos, line);
        pos += lp;
    }
    return pos;
}
