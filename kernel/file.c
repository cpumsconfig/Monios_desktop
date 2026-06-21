#include "file.h"
#include "extfs.h"
#include "fat16.h"
#include "fat32.h"
#include "fs_cache.h"
#include "iso9660.h"
#include "ntfs.h"

typedef enum {
    FILE_BACKEND_NONE = 0,
    FILE_BACKEND_FAT16,
    FILE_BACKEND_FAT32,
    FILE_BACKEND_ISO9660,
    FILE_BACKEND_NTFS,
    FILE_BACKEND_EXTFS
} file_backend_t;

static file_backend_t g_file_backend;

static int32_t file_read_at_backend(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_read_file_at(path, offset, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_read_file_at(path, offset, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_read_file_at(path, offset, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_read_file_at(path, offset, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_read_file_at(path, offset, buffer, buffer_size);
    }
    return -1;
}

bool file_init(void)
{
    fs_cache_init();
    if (fat32_init()) {
        g_file_backend = FILE_BACKEND_FAT32;
        return true;
    }
    if (fat16_init()) {
        g_file_backend = FILE_BACKEND_FAT16;
        return true;
    }
    if (iso9660_init()) {
        g_file_backend = FILE_BACKEND_ISO9660;
        return true;
    }
    if (ntfs_init()) {
        g_file_backend = FILE_BACKEND_NTFS;
        return true;
    }
    if (extfs_init()) {
        g_file_backend = FILE_BACKEND_EXTFS;
        return true;
    }
    g_file_backend = FILE_BACKEND_NONE;
    return false;
}

const char *file_backend_name(void)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return "fat32";
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return "fat16";
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return "iso9660";
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return "ntfs";
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return "extfs";
    }
    return "none";
}

bool file_exists(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_exists(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_exists(path);
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_exists(path);
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_exists(path);
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_exists(path);
    }
    return false;
}

bool file_is_dir(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_is_dir(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_is_dir(path);
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_is_dir(path);
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_is_dir(path);
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_is_dir(path);
    }
    return false;
}

int32_t file_size(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_file_size(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_file_size(path);
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_file_size(path);
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_file_size(path);
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_file_size(path);
    }
    return -1;
}

uint16_t file_root_entry_count(void)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_root_entry_count();
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_root_entry_count();
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_root_entry_count();
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_root_entry_count();
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_root_entry_count();
    }
    return 0;
}

int32_t file_read(const char *path, void *buffer, uint32_t buffer_size)
{
    return fs_cache_read_at(path, 0, buffer, buffer_size, file_read_at_backend);
}

int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    return fs_cache_read_at(path, offset, buffer, buffer_size, file_read_at_backend);
}

int32_t file_write(const char *path, const void *buffer, uint32_t size)
{
    int32_t written = -1;

    if (g_file_backend == FILE_BACKEND_FAT32) {
        written = fat32_write_file(path, buffer, size);
    } else if (g_file_backend == FILE_BACKEND_FAT16) {
        written = fat16_write_file(path, buffer, size);
    } else if (g_file_backend == FILE_BACKEND_ISO9660) {
        written = iso9660_write_file(path, buffer, size);
    } else if (g_file_backend == FILE_BACKEND_NTFS) {
        written = ntfs_write_file(path, buffer, size);
    } else if (g_file_backend == FILE_BACKEND_EXTFS) {
        written = extfs_write_file(path, buffer, size);
    }
    if (written >= 0) {
        fs_cache_invalidate_path(path);
    }
    return written;
}

bool file_delete(const char *path)
{
    bool ok = false;

    if (g_file_backend == FILE_BACKEND_FAT32) {
        ok = fat32_delete(path);
    } else if (g_file_backend == FILE_BACKEND_FAT16) {
        ok = fat16_delete(path);
    } else if (g_file_backend == FILE_BACKEND_ISO9660) {
        ok = iso9660_delete(path);
    } else if (g_file_backend == FILE_BACKEND_NTFS) {
        ok = ntfs_delete(path);
    } else if (g_file_backend == FILE_BACKEND_EXTFS) {
        ok = extfs_delete(path);
    }
    if (ok) {
        fs_cache_invalidate_path(path);
    }
    return ok;
}

bool file_mkdir(const char *path)
{
    bool ok = false;

    if (g_file_backend == FILE_BACKEND_FAT32) {
        ok = fat32_mkdir(path);
    } else if (g_file_backend == FILE_BACKEND_FAT16) {
        ok = fat16_mkdir(path);
    } else if (g_file_backend == FILE_BACKEND_ISO9660) {
        ok = iso9660_mkdir(path);
    } else if (g_file_backend == FILE_BACKEND_NTFS) {
        ok = ntfs_mkdir(path);
    } else if (g_file_backend == FILE_BACKEND_EXTFS) {
        ok = extfs_mkdir(path);
    }
    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_rmdir(const char *path)
{
    bool ok = false;

    if (g_file_backend == FILE_BACKEND_FAT32) {
        ok = fat32_rmdir(path);
    } else if (g_file_backend == FILE_BACKEND_FAT16) {
        ok = fat16_rmdir(path);
    } else if (g_file_backend == FILE_BACKEND_ISO9660) {
        ok = iso9660_rmdir(path);
    } else if (g_file_backend == FILE_BACKEND_NTFS) {
        ok = ntfs_rmdir(path);
    } else if (g_file_backend == FILE_BACKEND_EXTFS) {
        ok = extfs_rmdir(path);
    }
    if (ok) {
        fs_cache_invalidate_all();
    }
    return ok;
}

bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_list_dir(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_list_dir(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_ISO9660) {
        return iso9660_list_dir(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_NTFS) {
        return ntfs_list_dir(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_EXTFS) {
        return extfs_list_dir(path, buffer, buffer_size);
    }
    return false;
}
