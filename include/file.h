#ifndef _FILE_H_
#define _FILE_H_

#include "stdbool.h"
#include "stdint.h"

bool file_init(void);
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
bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size);

#endif
