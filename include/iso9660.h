#ifndef _ISO9660_H_
#define _ISO9660_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool present;
    bool ready;
    uint32_t total_blocks;
    uint32_t block_size;
    char volume_id[33];
    char publisher[129];
    char preparer[129];
    char application[129];
    uint32_t root_extent;
    uint32_t root_size;
    char status[64];
} iso9660_info_t;

bool iso9660_init(void);
uint16_t iso9660_root_entry_count(void);
bool iso9660_exists(const char *path);
bool iso9660_is_dir(const char *path);
int32_t iso9660_file_size(const char *path);
int32_t iso9660_read_file(const char *path, void *buffer, uint32_t buffer_size);
int32_t iso9660_read_file_at(const char *path, uint32_t offset, void *buffer, uint32_t buffer_size);
int32_t iso9660_write_file(const char *path, const void *buffer, uint32_t size);
bool iso9660_delete(const char *path);
bool iso9660_mkdir(const char *path);
bool iso9660_rmdir(const char *path);
bool iso9660_list_dir(const char *path, char *buffer, uint32_t buffer_size);
bool iso9660_list_root(char *buffer, uint32_t buffer_size);
const iso9660_info_t *iso9660_info(void);
const char *iso9660_status(void);

#endif
