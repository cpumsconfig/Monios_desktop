#ifndef _MD_H_
#define _MD_H_

#include "stdbool.h"
#include "stdint.h"

#define MD_MAX_DEVICES        16
#define MD_MAX_RAID_DEVICES   8

/* RAID levels */
#define MD_RAID0              0
#define MD_RAID1              1
#define MD_RAID4              4
#define MD_RAID5              5
#define MD_RAID6              6
#define MD_RAID10             10
#define MD_LINEAR             -1
#define MD_JBOD               -2

/* MD device states */
#define MD_STATE_CLEAR        0x00
#define MD_STATE_INACTIVE     0x01
#define MD_STATE_SUSPENDED    0x02
#define MD_STATE_READY        0x04
#define MD_STATE_ACTIVE       0x08
#define MD_STATE_WRITE_PENDING 0x10
#define MD_STATE_RESYNCING    0x20
#define MD_STATE_RECOVERING   0x40
#define MD_STATE_ERROR        0x80

/* MD component device states */
#define MD_DISK_FAULTY        0x01
#define MD_DISK_IN_SYNC       0x02
#define MD_DISK_SPARE         0x04
#define MD_DISK_WRITE_MOSTLY  0x08
#define MD_DISK_REPLACEMENT   0x10
#define MD_DISK_JOURNAL       0x20

typedef struct {
    bool present;
    uint8_t device_count;
    uint8_t array_count;
    uint32_t total_size_mb;
    uint32_t resync_progress;
    bool resync_active;
    char status[64];
} md_info_t;

typedef struct {
    int32_t raid_level;
    uint8_t state;
    uint8_t num_disks;
    uint8_t raid_disks;
    uint32_t chunk_size_kb;
    uint64_t size_sectors;
    uint64_t used_sectors;
    uint32_t stripe_size;
    char name[32];
    char uuid[16];
} md_array_info_t;

typedef struct {
    uint8_t state;
    uint8_t slot;
    uint64_t size_sectors;
    uint64_t recovery_offset;
    char device_name[32];
} md_disk_info_t;

void md_init(void);
bool md_is_present(void);
int32_t md_create_array(const char *name, int32_t level, uint8_t raid_disks, uint32_t chunk_size);
int32_t md_add_disk(uint8_t array_index, const char *device, uint8_t state);
int32_t md_remove_disk(uint8_t array_index, uint8_t slot);
int32_t md_start_array(uint8_t array_index);
int32_t md_stop_array(uint8_t array_index);
int32_t md_get_array_info(uint8_t array_index, md_array_info_t *info);
int32_t md_get_disk_info(uint8_t array_index, uint8_t slot, md_disk_info_t *info);
int32_t md_read(uint8_t array_index, uint64_t offset, uint8_t *buf, uint32_t len);
int32_t md_write(uint8_t array_index, uint64_t offset, const uint8_t *buf, uint32_t len);
const md_info_t *md_info(void);
const char *md_status(void);

#endif
