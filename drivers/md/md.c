#include "md.h"
#include "common.h"
#include "storage_ext.h"

static md_info_t g_md_info;
static md_array_info_t g_md_arrays[MD_MAX_RAID_DEVICES];
static md_disk_info_t g_md_disks[MD_MAX_RAID_DEVICES][MD_MAX_DEVICES];

void md_init(void)
{
    memset(&g_md_info, 0, sizeof(g_md_info));
    memset(g_md_arrays, 0, sizeof(g_md_arrays));
    memset(g_md_disks, 0, sizeof(g_md_disks));
    g_md_info.present = true;
    g_md_info.device_count = 0;
    g_md_info.array_count = 0;
    g_md_info.total_size_mb = 0;
    g_md_info.resync_progress = 0;
    g_md_info.resync_active = false;
    strcpy(g_md_info.status, "md: subsystem initialized");
}

bool md_is_present(void)
{
    return g_md_info.present;
}

int32_t md_create_array(const char *name, int32_t level, uint8_t raid_disks, uint32_t chunk_size)
{
    uint8_t index;

    if (name == NULL || raid_disks == 0 || raid_disks > MD_MAX_DEVICES) {
        return -1;
    }
    if (g_md_info.array_count >= MD_MAX_RAID_DEVICES) {
        strcpy(g_md_info.status, "md: too many arrays");
        return -1;
    }

    index = g_md_info.array_count;
    memset(&g_md_arrays[index], 0, sizeof(md_array_info_t));
    g_md_arrays[index].raid_level = level;
    g_md_arrays[index].state = MD_STATE_CLEAR;
    g_md_arrays[index].raid_disks = raid_disks;
    g_md_arrays[index].num_disks = 0;
    g_md_arrays[index].chunk_size_kb = chunk_size;
    g_md_arrays[index].stripe_size = chunk_size * 1024;
    strncpy(g_md_arrays[index].name, name, 31);
    g_md_arrays[index].name[31] = '\0';

    g_md_info.array_count++;
    strcpy(g_md_info.status, "md: array created");
    return (int32_t) index;
}

int32_t md_add_disk(uint8_t array_index, const char *device, uint8_t state)
{
    md_array_info_t *array;
    md_disk_info_t *disk;
    uint8_t slot;

    if (device == NULL || array_index >= g_md_info.array_count) {
        return -1;
    }

    array = &g_md_arrays[array_index];
    if (array->num_disks >= MD_MAX_DEVICES) {
        strcpy(g_md_info.status, "md: too many disks");
        return -1;
    }

    slot = array->num_disks;
    disk = &g_md_disks[array_index][slot];
    memset(disk, 0, sizeof(md_disk_info_t));
    disk->state = state;
    disk->slot = slot;
    disk->size_sectors = 0;
    disk->recovery_offset = 0;
    strncpy(disk->device_name, device, 31);
    disk->device_name[31] = '\0';

    array->num_disks++;
    strcpy(g_md_info.status, "md: disk added");
    return (int32_t) slot;
}

int32_t md_remove_disk(uint8_t array_index, uint8_t slot)
{
    md_array_info_t *array;

    if (array_index >= g_md_info.array_count) {
        return -1;
    }

    array = &g_md_arrays[array_index];
    if (slot >= array->num_disks) {
        strcpy(g_md_info.status, "md: invalid slot");
        return -1;
    }

    /* Mark disk as faulty and remove from array */
    g_md_disks[array_index][slot].state = MD_DISK_FAULTY;
    /* TODO: compact the disk array */

    strcpy(g_md_info.status, "md: disk removed");
    return 0;
}

int32_t md_start_array(uint8_t array_index)
{
    md_array_info_t *array;

    if (array_index >= g_md_info.array_count) {
        return -1;
    }

    array = &g_md_arrays[array_index];
    if (array->num_disks < array->raid_disks) {
        strcpy(g_md_info.status, "md: not enough disks");
        return -1;
    }

    array->state = MD_STATE_ACTIVE;
    strcpy(g_md_info.status, "md: array started");
    return 0;
}

int32_t md_stop_array(uint8_t array_index)
{
    if (array_index >= g_md_info.array_count) {
        return -1;
    }

    g_md_arrays[array_index].state = MD_STATE_INACTIVE;
    strcpy(g_md_info.status, "md: array stopped");
    return 0;
}

int32_t md_get_array_info(uint8_t array_index, md_array_info_t *info)
{
    if (info == NULL || array_index >= g_md_info.array_count) {
        return -1;
    }

    memcpy(info, &g_md_arrays[array_index], sizeof(md_array_info_t));
    return 0;
}

int32_t md_get_disk_info(uint8_t array_index, uint8_t slot, md_disk_info_t *info)
{
    if (info == NULL || array_index >= g_md_info.array_count) {
        return -1;
    }
    if (slot >= g_md_arrays[array_index].num_disks) {
        return -1;
    }

    memcpy(info, &g_md_disks[array_index][slot], sizeof(md_disk_info_t));
    return 0;
}

int32_t md_read(uint8_t array_index, uint64_t offset, uint8_t *buf, uint32_t len)
{
    (void) array_index;
    (void) offset;
    (void) buf;
    (void) len;
    if (array_index >= g_md_info.array_count) {
        return -1;
    }
    if (buf == NULL || len == 0) {
        return -1;
    }
    /* TODO: implement RAID read */
    strcpy(g_md_info.status, "md: read stub");
    return -1;
}

int32_t md_write(uint8_t array_index, uint64_t offset, const uint8_t *buf, uint32_t len)
{
    (void) array_index;
    (void) offset;
    (void) buf;
    (void) len;
    if (array_index >= g_md_info.array_count) {
        return -1;
    }
    if (buf == NULL || len == 0) {
        return -1;
    }
    /* TODO: implement RAID write */
    strcpy(g_md_info.status, "md: write stub");
    return -1;
}

const md_info_t *md_info(void)
{
    return &g_md_info;
}

const char *md_status(void)
{
    return g_md_info.status;
}
