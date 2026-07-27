#ifndef _INSTALLER_H_
#define _INSTALLER_H_

#include "stdbool.h"
#include "stdint.h"

#define INSTALLER_CALL_BOOT_MEDIA     0U
#define INSTALLER_CALL_WRITE_DISK     1U
#define INSTALLER_CALL_REBOOT         2U
#define INSTALLER_CALL_WRITE_TARGET   3U
#define INSTALLER_CALL_LIST_TARGETS    4U
#define INSTALLER_CALL_WRITE_BUFFER    5U
#define INSTALLER_CALL_COPY_TARGET     6U
#define INSTALLER_CALL_READ_MEDIA      7U
#define INSTALLER_CALL_STAT_MEDIA      8U

#define INSTALLER_MAX_TARGETS         6U
#define INSTALLER_WRITE_BUFFER_MAX    4096U
#define INSTALLER_TARGET_WRITE_MAX    4096U
#define INSTALLER_MEDIA_READ_MAX      4096U
#define INSTALLER_COPY_TARGET_MAX     (64U * 1024U * 1024U)

#define INSTALLER_TARGET_KIND_DISK     0U
#define INSTALLER_TARGET_KIND_PART     1U
#define INSTALLER_TARGET_KIND_UEFI_ESP 2U
#define INSTALLER_TARGET_WHOLE_DISK    0xFFU

typedef struct {
    uint8_t kind;
    uint8_t partition_index;
    uint8_t active;
    uint8_t partition_type;
    uint32_t start_lba;
    uint32_t sector_count;
} installer_target_info_t;

typedef struct {
    uint8_t disk_present;
    uint8_t reserved[3];
    uint32_t disk_sector_count;
    char disk_model[41];
    uint32_t target_count;
    installer_target_info_t targets[INSTALLER_MAX_TARGETS];
} installer_target_list_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    uint32_t disk_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} installer_write_request_t;

typedef struct {
    const void *data;
    uint32_t disk_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} installer_buffer_write_request_t;

typedef struct {
    const char *target_path;
    const void *data;
    uint32_t target_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} installer_target_write_request_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    const char *target_path;
    uint32_t target_lba;
    uint32_t byte_count;
    uint32_t bytes_written;
} installer_target_copy_request_t;

typedef struct {
    const char *source_path;
    uint32_t source_offset;
    void *data;
    uint32_t byte_count;
    uint32_t bytes_read;
} installer_media_read_request_t;

typedef struct {
    const char *source_path;
    int32_t file_size;
    uint8_t exists;
    uint8_t reserved[3];
} installer_media_stat_request_t;

bool installer_boot_media_present(void);
int32_t installer_write_file_to_disk(const char *source_path,
                                     uint32_t source_offset,
                                     uint32_t disk_lba,
                                     uint32_t byte_count);
int32_t installer_write_buffer_to_disk(const void *data, uint32_t disk_lba, uint32_t byte_count);
int32_t installer_write_target_file(const char *target_path,
                                    const void *data,
                                    uint32_t target_lba,
                                    uint32_t byte_count);
int32_t installer_copy_file_to_target(const char *source_path,
                                      uint32_t source_offset,
                                      uint32_t byte_count,
                                      const char *target_path,
                                      uint32_t target_lba);
int32_t installer_read_media_file(const char *source_path,
                                  uint32_t source_offset,
                                  void *data,
                                  uint32_t byte_count);
int32_t installer_media_file_size(const char *source_path);
int32_t installer_list_targets(installer_target_list_t *list, uint32_t list_size);
void installer_request_reboot(void);

#endif
