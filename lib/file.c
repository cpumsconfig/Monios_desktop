#include "file.h"
#include "extfs.h"
#include "fat16.h"
#include "fat32.h"
#include "fs_cache.h"
#include "iso9660.h"
#include "ntfs.h"
#include "path.h"
#include "stddef.h"
#include "string.h"

/* ============================================================
 *  挂载点管理
 * ============================================================ */

static mount_point_t g_mount_points[MAX_MOUNT_POINTS];
static int32_t g_mount_count = 0;
static int32_t g_pending_mount_partition = -1;

/* 当前激活的文件系统（用于全局操作） */
static fs_type_t g_current_fs = FS_TYPE_NONE;
static char g_current_mount_path[MAX_MOUNT_PATH] = PATH_ROOT;
static int32_t g_current_mount_partition = -1;

#define FILE_CACHE_KEY_SEP ((char) 0x1F)
#define FILE_CACHE_KEY_MAX 512U

/* ============================================================
 *  辅助函数：文件系统类型名称转换
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
        default:              return "none";
    }
}

/* ============================================================
 *  辅助函数：初始化指定类型的文件系统
 * ============================================================ */

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
        default:
            return false;
    }
}

/* ============================================================
 *  辅助函数：路径匹配（最长前缀匹配）
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
        return;
    }
    g_current_fs = mount->fs_type;
    g_current_mount_partition = mount->partition;
    strlcpy(g_current_mount_path, mount->path, sizeof(g_current_mount_path));
}

static bool restore_current_mount(fs_type_t fs, int32_t partition, const char *mount_path)
{
    if (fs == FS_TYPE_NONE || mount_path == NULL || mount_path[0] == '\0') {
        return false;
    }
    g_pending_mount_partition = partition;
    if (!fs_init_by_type(fs)) {
        g_pending_mount_partition = -1;
        return false;
    }
    g_pending_mount_partition = -1;
    g_current_fs = fs;
    g_current_mount_partition = partition;
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

        /* 检查路径是否以挂载点开头 */
        if (path_len >= mp_len && strncmp(path, g_mount_points[i].path, mp_len) == 0) {
            /* 确保挂载点路径后面是 '/' 或者正好结束 */
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
 *  辅助函数：切换到指定挂载点的文件系统
 *  注意：由于文件系统使用全局变量，切换需要重新初始化
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

    /* 如果当前已经是这个文件系统，直接返回 */
    if (g_current_fs == mount->fs_type &&
        g_current_mount_partition == mount->partition) {
        /* 计算相对路径 */
        set_current_mount(mount);
        make_relative_path(path, mount->path, relative_path);
        return true;
    }

    /* 切换文件系统（重新初始化） */
    g_pending_mount_partition = mount->partition;
    if (!fs_init_by_type(mount->fs_type)) {
        g_pending_mount_partition = -1;
        return false;
    }
    g_pending_mount_partition = -1;

    set_current_mount(mount);

    /* 计算相对路径 */
    make_relative_path(path, mount->path, relative_path);

    return true;
}

/* ============================================================
 *  动态挂载接口
 * ============================================================ */

bool file_mount(const char *mount_path, const char *fs_type, int32_t partition)
{
    char normalized_path[MAX_MOUNT_PATH];
    char previous_mount_path[MAX_MOUNT_PATH];
    fs_type_t previous_fs = g_current_fs;
    int32_t previous_partition = g_current_mount_partition;
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
                       previous_mount_path[0] != '\0' &&
                       previous_fs == type;

    /* 检查挂载点是否已存在 */
    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, normalized_path) == 0) {
            /* 已挂载，返回失败 */
            return false;
        }
        if (!g_mount_points[i].mounted && empty_idx < 0) {
            empty_idx = i;
        }
    }

    if (empty_idx < 0) {
        /* 没有空闲的挂载点 */
        return false;
    }

    /* 尝试初始化文件系统 */
    /* 注意：这里暂时不处理分区号，使用默认的自动探测 */
    g_pending_mount_partition = partition;
    if (!fs_init_by_type(type)) {
        g_pending_mount_partition = -1;
        if (restore_previous) {
            (void) restore_current_mount(previous_fs, previous_partition, previous_mount_path);
        }
        return false;
    }
    g_pending_mount_partition = -1;

    /* 添加到挂载点表 */
    strlcpy(g_mount_points[empty_idx].path, normalized_path, sizeof(g_mount_points[empty_idx].path));
    g_mount_points[empty_idx].fs_type = type;
    g_mount_points[empty_idx].partition = partition;
    g_mount_points[empty_idx].mounted = true;
    g_mount_count++;

    /* 如果是第一个挂载点，设为当前文件系统 */
    if (g_mount_count == 1 || strcmp(normalized_path, PATH_ROOT) == 0) {
        set_current_mount(&g_mount_points[empty_idx]);
    } else if (restore_previous) {
        (void) restore_current_mount(previous_fs, previous_partition, previous_mount_path);
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
            /* 卸载 */
            g_mount_points[i].mounted = false;
            g_mount_points[i].fs_type = FS_TYPE_NONE;
            g_mount_points[i].path[0] = '\0';
            g_mount_count--;

            /* 如果卸载的是当前文件系统，切换到另一个 */
            if (strcmp(g_current_mount_path, normalized_path) == 0) {
                if (g_mount_count > 0) {
                    /* 找到第一个挂载的 */
                    int32_t j;
                    bool activated = false;
                    for (j = 0; j < MAX_MOUNT_POINTS; j++) {
                        if (g_mount_points[j].mounted) {
                            activated = restore_current_mount(g_mount_points[j].fs_type,
                                                              g_mount_points[j].partition,
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
 *  文件操作（带挂载点解析）
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

bool file_exists(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_exists(backend_path);
        case FS_TYPE_FAT16:   return fat16_exists(backend_path);
        case FS_TYPE_ISO9660: return iso9660_exists(backend_path);
        case FS_TYPE_NTFS:    return ntfs_exists(backend_path);
        case FS_TYPE_EXTFS:   return extfs_exists(backend_path);
        default:              return false;
    }
}

bool file_is_dir(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_is_dir(backend_path);
        case FS_TYPE_FAT16:   return fat16_is_dir(backend_path);
        case FS_TYPE_ISO9660: return iso9660_is_dir(backend_path);
        case FS_TYPE_NTFS:    return ntfs_is_dir(backend_path);
        case FS_TYPE_EXTFS:   return extfs_is_dir(backend_path);
        default:              return false;
    }
}

int32_t file_size(const char *path)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return -1;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_file_size(backend_path);
        case FS_TYPE_FAT16:   return fat16_file_size(backend_path);
        case FS_TYPE_ISO9660: return iso9660_file_size(backend_path);
        case FS_TYPE_NTFS:    return ntfs_file_size(backend_path);
        case FS_TYPE_EXTFS:   return extfs_file_size(backend_path);
        default:              return -1;
    }
}

/* ============================================================
 *  内部读取回调（用于缓存）
 * ============================================================ */

static int32_t cached_read_at(const char *path, uint32_t offset,
                               void *buffer, uint32_t buffer_size)
{
    const char *rel_path = file_cache_relative_path(path);

    /* 注意：这里假设调用前已经 resolve_path 了 */
    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_read_file_at(rel_path, offset, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_read_file_at(rel_path, offset, buffer, buffer_size);
        default:              return -1;
    }
}

int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    char cache_key[FILE_CACHE_KEY_MAX];
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return -1;
    }

    /* 注意：缓存的 key 应该包含挂载点，这里简化处理 */
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

    if (!resolve_path(path, resolved, backend_path)) {
        return -1;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   written = fat32_write_file(backend_path, buffer, size); break;
        case FS_TYPE_FAT16:   written = fat16_write_file(backend_path, buffer, size); break;
        case FS_TYPE_ISO9660: written = iso9660_write_file(backend_path, buffer, size); break;
        case FS_TYPE_NTFS:    written = ntfs_write_file(backend_path, buffer, size); break;
        case FS_TYPE_EXTFS:   written = extfs_write_file(backend_path, buffer, size); break;
        default:              written = -1; break;
    }

    if (written >= 0) {
        file_invalidate_cached_file(backend_path);
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

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_delete(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_delete(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_delete(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_delete(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_delete(backend_path); break;
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

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_mkdir(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_mkdir(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_mkdir(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_mkdir(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_mkdir(backend_path); break;
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

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_rmdir(backend_path); break;
        case FS_TYPE_FAT16:   ok = fat16_rmdir(backend_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_rmdir(backend_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_rmdir(backend_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_rmdir(backend_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    char resolved[PATH_MAX_LEN];
    char backend_path[PATH_MAX_LEN];

    if (!resolve_path(path, resolved, backend_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_list_dir(backend_path, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_list_dir(backend_path, buffer, buffer_size);
        default:              return false;
    }
}

uint16_t file_root_entry_count(void)
{
    /* 这个函数比较特殊，返回当前根目录的条目数 */
    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_root_entry_count();
        case FS_TYPE_FAT16:   return fat16_root_entry_count();
        case FS_TYPE_ISO9660: return iso9660_root_entry_count();
        case FS_TYPE_NTFS:    return ntfs_root_entry_count();
        case FS_TYPE_EXTFS:   return extfs_root_entry_count();
        default:              return 0;
    }
}

/* ============================================================
 *  初始化和状态
 * ============================================================ */

bool file_init(void)
{
    int32_t i;

    /* 初始化缓存 */
    fs_cache_init();

    /* 清空挂载点表 */
    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        g_mount_points[i].mounted = false;
        g_mount_points[i].fs_type = FS_TYPE_NONE;
        g_mount_points[i].path[0] = '\0';
        g_mount_points[i].partition = -1;
    }
    g_mount_count = 0;
    g_pending_mount_partition = -1;
    g_current_fs = FS_TYPE_NONE;
    g_current_mount_path[0] = '\0';
    g_current_mount_partition = -1;

    /* 注意：不再自动探测挂载，改为动态挂载 */
    return true;
}

const char *file_backend_name(void)
{
    return fs_type_to_name(g_current_fs);
}

/* ============================================================
 *  自动探测挂载（兼容旧行为）
 * ============================================================ */

bool file_auto_mount(void)
{
    /* 依次尝试各种文件系统，挂载到根目录 */
    if (file_mount(PATH_ROOT, "iso9660", -1)) {
        if (file_exists(PATH_ROOT "INSTALL.FLG")) {
            return true;
        }
        file_umount(PATH_ROOT);
    }

    if (file_mount(PATH_ROOT, "fat32", -1)) {
        return true;
    }
    if (file_mount(PATH_ROOT, "fat16", -1)) {
        return true;
    }
    if (file_mount(PATH_ROOT, "iso9660", -1)) {
        return true;
    }
    if (file_mount(PATH_ROOT, "ntfs", -1)) {
        return true;
    }
    if (file_mount(PATH_ROOT, "extfs", -1)) {
        return true;
    }
    return false;
}
