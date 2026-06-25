#ifndef _FAT32_H_
#define _FAT32_H_

#include "stdbool.h"
#include "stdint.h"

bool fat32_init(void);
uint16_t fat32_root_entry_count(void);
bool fat32_exists(const char *path);
bool fat32_is_dir(const char *path);
int32_t fat32_file_size(const char *path);
int32_t fat32_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t fat32_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t fat32_write_file(const char *path, const void *buffer, uint32_t size);
bool fat32_delete(const char *path);
bool fat32_mkdir(const char *path);
bool fat32_rmdir(const char *path);
bool fat32_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool fat32_list_root(char *buffer, uint32_t buffer_size);

#endif
