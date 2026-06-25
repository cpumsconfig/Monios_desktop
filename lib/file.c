#include "file.h"
#include "extfs.h"
#include "fat16.h"
#include "fat32.h"
#include "fs_cache.h"
#include "iso9660.h"
#include "ntfs.h"
#include "stddef.h"
#include "string.h"

/* ============================================================
 *  挂载点管理
 * ============================================================ */

static mount_point_t g_mount_points[MAX_MOUNT_POINTS];
static int32_t g_mount_count = 0;

/* 当前激活的文件系统（用于全局操作） */
static fs_type_t g_current_fs = FS_TYPE_NONE;
static char g_current_mount_path[MAX_MOUNT_PATH] = "/";

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

static int32_t find_mount_point(const char *path)
{
    int32_t best_idx = -1;
    uint32_t best_len = 0;
    int32_t i;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (!g_mount_points[i].mounted) {
            continue;
        }

        uint32_t mp_len = strlen(g_mount_points[i].path);
        if (mp_len == 0) {
            continue;
        }
        if (mp_len == 1 && g_mount_points[i].path[0] == '/') {
            if (path[0] == '/' && mp_len > best_len) {
                best_len = mp_len;
                best_idx = i;
            }
            continue;
        }

        /* 检查路径是否以挂载点开头 */
        if (strncmp(path, g_mount_points[i].path, mp_len) == 0) {
            /* 确保挂载点路径后面是 '/' 或者正好结束 */
            if (path[mp_len] == '\0' || path[mp_len] == '/') {
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
    uint32_t mp_len;

    if (mount_idx < 0 || mount_idx >= MAX_MOUNT_POINTS) {
        return false;
    }

    if (!g_mount_points[mount_idx].mounted) {
        return false;
    }

    /* 如果当前已经是这个文件系统，直接返回 */
    if (g_current_fs == g_mount_points[mount_idx].fs_type) {
        /* 计算相对路径 */
        mp_len = strlen(g_mount_points[mount_idx].path);
        if (path[mp_len] == '/') {
            *relative_path = path + mp_len;
        } else if (path[mp_len] == '\0') {
            *relative_path = "/";
        } else {
            *relative_path = path + mp_len;
        }
        return true;
    }

    /* 切换文件系统（重新初始化） */
    if (!fs_init_by_type(g_mount_points[mount_idx].fs_type)) {
        return false;
    }

    g_current_fs = g_mount_points[mount_idx].fs_type;
    strcpy(g_current_mount_path, g_mount_points[mount_idx].path);

    /* 计算相对路径 */
    mp_len = strlen(g_mount_points[mount_idx].path);
    if (path[mp_len] == '/') {
        *relative_path = path + mp_len;
    } else if (path[mp_len] == '\0') {
        *relative_path = "/";
    } else {
        *relative_path = path + mp_len;
    }

    return true;
}

/* ============================================================
 *  动态挂载接口
 * ============================================================ */

bool file_mount(const char *mount_path, const char *fs_type, int32_t partition)
{
    fs_type_t type;
    int32_t i;
    int32_t empty_idx = -1;

    if (mount_path == NULL || mount_path[0] == '\0' || fs_type == NULL) {
        return false;
    }

    type = fs_type_from_name(fs_type);
    if (type == FS_TYPE_NONE) {
        return false;
    }

    /* 检查挂载点是否已存在 */
    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
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
    if (!fs_init_by_type(type)) {
        return false;
    }

    /* 添加到挂载点表 */
    strcpy(g_mount_points[empty_idx].path, mount_path);
    g_mount_points[empty_idx].fs_type = type;
    g_mount_points[empty_idx].partition = partition;
    g_mount_points[empty_idx].mounted = true;
    g_mount_count++;

    /* 如果是第一个挂载点，设为当前文件系统 */
    if (g_mount_count == 1 || strcmp(mount_path, "/") == 0) {
        g_current_fs = type;
        strcpy(g_current_mount_path, mount_path);
    }

    return true;
}

bool file_umount(const char *mount_path)
{
    int32_t i;

    if (mount_path == NULL || mount_path[0] == '\0') {
        return false;
    }

    for (i = 0; i < MAX_MOUNT_POINTS; i++) {
        if (g_mount_points[i].mounted &&
            strcmp(g_mount_points[i].path, mount_path) == 0) {
            /* 卸载 */
            g_mount_points[i].mounted = false;
            g_mount_points[i].fs_type = FS_TYPE_NONE;
            g_mount_points[i].path[0] = '\0';
            g_mount_count--;

            /* 如果卸载的是当前文件系统，切换到另一个 */
            if (strcmp(g_current_mount_path, mount_path) == 0) {
                if (g_mount_count > 0) {
                    /* 找到第一个挂载的 */
                    int32_t j;
                    for (j = 0; j < MAX_MOUNT_POINTS; j++) {
                        if (g_mount_points[j].mounted) {
                            fs_init_by_type(g_mount_points[j].fs_type);
                            g_current_fs = g_mount_points[j].fs_type;
                            strcpy(g_current_mount_path, g_mount_points[j].path);
                            break;
                        }
                    }
                } else {
                    g_current_fs = FS_TYPE_NONE;
                    g_current_mount_path[0] = '\0';
                }
            }

            return true;
        }
    }

    return false;
}

int32_t file_mount_count(void)
{
    return g_mount_count;
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

static bool resolve_path(const char **path)
{
    int32_t mount_idx = find_mount_point(*path);
    if (mount_idx < 0) {
        return false;
    }
    return switch_to_mount(mount_idx, path);
}

bool file_exists(const char *path)
{
    const char *rel_path = path;
    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_exists(rel_path);
        case FS_TYPE_FAT16:   return fat16_exists(rel_path);
        case FS_TYPE_ISO9660: return iso9660_exists(rel_path);
        case FS_TYPE_NTFS:    return ntfs_exists(rel_path);
        case FS_TYPE_EXTFS:   return extfs_exists(rel_path);
        default:              return false;
    }
}

bool file_is_dir(const char *path)
{
    const char *rel_path = path;
    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_is_dir(rel_path);
        case FS_TYPE_FAT16:   return fat16_is_dir(rel_path);
        case FS_TYPE_ISO9660: return iso9660_is_dir(rel_path);
        case FS_TYPE_NTFS:    return ntfs_is_dir(rel_path);
        case FS_TYPE_EXTFS:   return extfs_is_dir(rel_path);
        default:              return false;
    }
}

int32_t file_size(const char *path)
{
    const char *rel_path = path;
    if (!resolve_path(&rel_path)) {
        return -1;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_file_size(rel_path);
        case FS_TYPE_FAT16:   return fat16_file_size(rel_path);
        case FS_TYPE_ISO9660: return iso9660_file_size(rel_path);
        case FS_TYPE_NTFS:    return ntfs_file_size(rel_path);
        case FS_TYPE_EXTFS:   return extfs_file_size(rel_path);
        default:              return -1;
    }
}

/* ============================================================
 *  内部读取回调（用于缓存）
 * ============================================================ */

static int32_t cached_read_at(const char *path, uint32_t offset,
                               void *buffer, uint32_t buffer_size)
{
    /* 注意：这里假设调用前已经 resolve_path 了 */
    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_read_file_at(path, offset, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_read_file_at(path, offset, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_read_file_at(path, offset, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_read_file_at(path, offset, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_read_file_at(path, offset, buffer, buffer_size);
        default:              return -1;
    }
}

int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    const char *rel_path = path;
    if (!resolve_path(&rel_path)) {
        return -1;
    }

    /* 注意：缓存的 key 应该包含挂载点，这里简化处理 */
    return fs_cache_read_at(rel_path, offset, buffer, buffer_size, cached_read_at);
}

int32_t file_read(const char *path, void *buffer, uint32_t buffer_size)
{
    return file_read_at(path, 0, buffer, buffer_size);
}

int32_t file_write(const char *path, const void *buffer, uint32_t size)
{
    const char *rel_path = path;
    int32_t written = -1;

    if (!resolve_path(&rel_path)) {
        return -1;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   written = fat32_write_file(rel_path, buffer, size); break;
        case FS_TYPE_FAT16:   written = fat16_write_file(rel_path, buffer, size); break;
        case FS_TYPE_ISO9660: written = iso9660_write_file(rel_path, buffer, size); break;
        case FS_TYPE_NTFS:    written = ntfs_write_file(rel_path, buffer, size); break;
        case FS_TYPE_EXTFS:   written = extfs_write_file(rel_path, buffer, size); break;
        default:              written = -1; break;
    }

    if (written >= 0) {
        fs_cache_invalidate_path(rel_path);
    }
    return written;
}

bool file_delete(const char *path)
{
    const char *rel_path = path;
    bool ok = false;

    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_delete(rel_path); break;
        case FS_TYPE_FAT16:   ok = fat16_delete(rel_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_delete(rel_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_delete(rel_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_delete(rel_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_path(rel_path);
    }
    return ok;
}

bool file_mkdir(const char *path)
{
    const char *rel_path = path;
    bool ok = false;

    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_mkdir(rel_path); break;
        case FS_TYPE_FAT16:   ok = fat16_mkdir(rel_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_mkdir(rel_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_mkdir(rel_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_mkdir(rel_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_rmdir(const char *path)
{
    const char *rel_path = path;
    bool ok = false;

    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   ok = fat32_rmdir(rel_path); break;
        case FS_TYPE_FAT16:   ok = fat16_rmdir(rel_path); break;
        case FS_TYPE_ISO9660: ok = iso9660_rmdir(rel_path); break;
        case FS_TYPE_NTFS:    ok = ntfs_rmdir(rel_path); break;
        case FS_TYPE_EXTFS:   ok = extfs_rmdir(rel_path); break;
        default:              ok = false; break;
    }

    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    const char *rel_path = path;
    if (!resolve_path(&rel_path)) {
        return false;
    }

    switch (g_current_fs) {
        case FS_TYPE_FAT32:   return fat32_list_dir(rel_path, buffer, buffer_size);
        case FS_TYPE_FAT16:   return fat16_list_dir(rel_path, buffer, buffer_size);
        case FS_TYPE_ISO9660: return iso9660_list_dir(rel_path, buffer, buffer_size);
        case FS_TYPE_NTFS:    return ntfs_list_dir(rel_path, buffer, buffer_size);
        case FS_TYPE_EXTFS:   return extfs_list_dir(rel_path, buffer, buffer_size);
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
    g_current_fs = FS_TYPE_NONE;
    g_current_mount_path[0] = '\0';

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
    if (file_mount("/", "fat32", -1)) {
        return true;
    }
    if (file_mount("/", "fat16", -1)) {
        return true;
    }
    if (file_mount("/", "iso9660", -1)) {
        return true;
    }
    if (file_mount("/", "ntfs", -1)) {
        return true;
    }
    if (file_mount("/", "extfs", -1)) {
        return true;
    }
    return false;
}
