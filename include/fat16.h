#ifndef _FAT16_H_
#define _FAT16_H_

#include "stdbool.h"
#include "stdint.h"

bool fat16_init(void);
uint16_t fat16_root_entry_count(void);
bool fat16_exists(const char *path);
bool fat16_is_dir(const char *path);
int32_t fat16_file_size(const char *path);
int32_t fat16_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t fat16_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t fat16_write_file(const char *path, const void *buffer, uint32_t size);
bool fat16_delete(const char *path);
bool fat16_mkdir(const char *path);
bool fat16_rmdir(const char *path);
bool fat16_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool fat16_list_root(char *buffer, uint32_t buffer_size);

#endif
