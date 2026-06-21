#include "file.h"
#include "fat16.h"
#include "fat32.h"

typedef enum {
    FILE_BACKEND_NONE = 0,
    FILE_BACKEND_FAT16,
    FILE_BACKEND_FAT32
} file_backend_t;

static file_backend_t g_file_backend;

bool file_init(void)
{
    if (fat32_init()) {
        g_file_backend = FILE_BACKEND_FAT32;
        return true;
    }
    if (fat16_init()) {
        g_file_backend = FILE_BACKEND_FAT16;
        return true;
    }
    g_file_backend = FILE_BACKEND_NONE;
    return false;
}

bool file_exists(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_exists(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_exists(path);
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
    return 0;
}

int32_t file_read(const char *path, void *buffer, uint32_t buffer_size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_read_file(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_read_file(path, buffer, buffer_size);
    }
    return -1;
}

int32_t file_read_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_read_file_at(path, offset, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_read_file_at(path, offset, buffer, buffer_size);
    }
    return -1;
}

int32_t file_write(const char *path, const void *buffer, uint32_t size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_write_file(path, buffer, size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_write_file(path, buffer, size);
    }
    return -1;
}

bool file_delete(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_delete(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_delete(path);
    }
    return false;
}

bool file_mkdir(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_mkdir(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_mkdir(path);
    }
    return false;
}

bool file_rmdir(const char *path)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_rmdir(path);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_rmdir(path);
    }
    return false;
}

bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{
    if (g_file_backend == FILE_BACKEND_FAT32) {
        return fat32_list_dir(path, buffer, buffer_size);
    }
    if (g_file_backend == FILE_BACKEND_FAT16) {
        return fat16_list_dir(path, buffer, buffer_size);
    }
    return false;
}
