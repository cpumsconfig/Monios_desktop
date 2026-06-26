#include "md.h"
#include "common.h"
#include "kernel.h"

static md_info_t g_md_info;
static md_array_info_t g_md_arrays[MD_MAX_DEVICES];
static md_disk_info_t g_md_disks[MD_MAX_DEVICES][MD_MAX_RAID_DEVICES];

/* Initialize MD subsystem */
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

    strcpy(g_md_info.status, "md: initialized");
    log_write("md: RAID subsystem initialized");
}

/* Check if MD is present */
bool md_is_present(void)
{
    return g_md_info.present;
}

/* Create a new MD array */
int32_t md_create_array(const char *name, int32_t level, uint8_t raid_disks, uint32_t chunk_size)
{
    if (name == NULL || raid_disks == 0 || raid_disks > MD_MAX_RAID_DEVICES) {
        return -1;
    }

    if (g_md_info.array_count >= MD_MAX_DEVICES) {
        strcpy(g_md_info.status, "md: too many arrays");
        return -1;
    }

    uint8_t array_id = g_md_info.array_count;
    md_array_info_t *array = &g_md_arrays[array_id];

    memset(array, 0, sizeof(md_array_info_t));

    array->raid_level = level;
    array->state = MD_STATE_INACTIVE;
    array->num_disks = 0;
    array->raid_disks = raid_disks;
    array->chunk_size_kb = chunk_size ? chunk_size : 64; /* Default 64KB */
    array->size_sectors = 0;
    array->used_sectors = 0;
    array->stripe_size = 0;

    strncpy(array->name, name, sizeof(array->name) - 1);
    array->name[sizeof(array->name) - 1] = '\0';

    /* Initialize disk slots for this array */
    for (uint8_t i = 0; i < MD_MAX_RAID_DEVICES; i++) {
        g_md_disks[array_id][i].state = MD_STATE_CLEAR;
        g_md_disks[array_id][i].slot = i;
        g_md_disks[array_id][i].size_sectors = 0;
        g_md_disks[array_id][i].recovery_offset = 0;
        memset(g_md_disks[array_id][i].device_name, 0, sizeof(g_md_disks[array_id][i].device_name));
    }

    g_md_info.array_count++;

    strcpy(g_md_info.status, "md: array created");
    log_write("md: array created");
    return array_id;
}

/* Add a disk to an MD array */
int32_t md_add_disk(uint8_t array_index, const char *device, uint8_t state)
{
    if (device == NULL || array_index >= g_md_info.array_count) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];

    if (array->num_disks >= MD_MAX_RAID_DEVICES) {
        strcpy(g_md_info.status, "md: array full");
        return -1;
    }

    /* Find first empty slot */
    uint8_t slot = 0;
    for (; slot < MD_MAX_RAID_DEVICES; slot++) {
        if (g_md_disks[array_index][slot].state == MD_STATE_CLEAR) {
            break;
        }
    }

    if (slot >= MD_MAX_RAID_DEVICES) {
        return -1;
    }

    md_disk_info_t *disk = &g_md_disks[array_index][slot];

    disk->state = state;
    disk->slot = slot;
    disk->size_sectors = 0;
    disk->recovery_offset = 0;

    strncpy(disk->device_name, device, sizeof(disk->device_name) - 1);
    disk->device_name[sizeof(disk->device_name) - 1] = '\0';

    array->num_disks++;

    strcpy(g_md_info.status, "md: disk added");
    return slot;
}

/* Remove a disk from an MD array */
int32_t md_remove_disk(uint8_t array_index, uint8_t slot)
{
    if (array_index >= g_md_info.array_count || slot >= MD_MAX_RAID_DEVICES) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];
    md_disk_info_t *disk = &g_md_disks[array_index][slot];

    if (disk->state == MD_STATE_CLEAR) {
        strcpy(g_md_info.status, "md: slot already empty");
        return -1;
    }

    /* Mark disk as faulty and clear it */
    disk->state = MD_STATE_CLEAR;
    disk->size_sectors = 0;
    disk->recovery_offset = 0;
    memset(disk->device_name, 0, sizeof(disk->device_name));

    if (array->num_disks > 0) {
        array->num_disks--;
    }

    /* Compact slots - move subsequent disks up */
    for (uint8_t i = slot; i < MD_MAX_RAID_DEVICES - 1; i++) {
        if (g_md_disks[array_index][i].state == MD_STATE_CLEAR &&
            g_md_disks[array_index][i + 1].state != MD_STATE_CLEAR) {
            memcpy(&g_md_disks[array_index][i], &g_md_disks[array_index][i + 1], sizeof(md_disk_info_t));
            g_md_disks[array_index][i].slot = i;
            memset(&g_md_disks[array_index][i + 1], 0, sizeof(md_disk_info_t));
            g_md_disks[array_index][i + 1].state = MD_STATE_CLEAR;
            g_md_disks[array_index][i + 1].slot = (uint8_t)(i + 1);
        }
    }

    strcpy(g_md_info.status, "md: disk removed");
    return 0;
}

/* Start an MD array */
int32_t md_start_array(uint8_t array_index)
{
    if (array_index >= g_md_info.array_count) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];

    if (array->state & MD_STATE_ACTIVE) {
        strcpy(g_md_info.status, "md: array already active");
        return -1;
    }

    if (array->num_disks == 0) {
        strcpy(g_md_info.status, "md: no disks in array");
        return -1;
    }

    /* Find smallest disk size to calculate array capacity */
    uint64_t min_size = 0xFFFFFFFFFFFFFFFFULL;
    uint8_t active_disks = 0;

    for (uint8_t i = 0; i < array->num_disks; i++) {
        if (g_md_disks[array_index][i].state & MD_DISK_IN_SYNC) {
            active_disks++;
            if (g_md_disks[array_index][i].size_sectors < min_size &&
                g_md_disks[array_index][i].size_sectors > 0) {
                min_size = g_md_disks[array_index][i].size_sectors;
            }
        }
    }

    if (min_size == 0xFFFFFFFFFFFFFFFFULL || min_size == 0) {
        min_size = 1024 * 1024 * 2; /* Default 1GB in sectors (512B) */
    }

    /* Calculate array size based on RAID level */
    switch (array->raid_level) {
    case MD_RAID0:
        array->size_sectors = min_size * array->num_disks;
        array->stripe_size = array->chunk_size_kb * array->num_disks * 2; /* sectors */
        break;
    case MD_RAID1:
        array->size_sectors = min_size;
        array->stripe_size = array->chunk_size_kb * 2;
        break;
    case MD_RAID4:
    case MD_RAID5:
        if (array->num_disks > 1) {
            array->size_sectors = min_size * (array->num_disks - 1);
            array->stripe_size = array->chunk_size_kb * (array->num_disks - 1) * 2;
        }
        break;
    case MD_RAID6:
        if (array->num_disks > 2) {
            array->size_sectors = min_size * (array->num_disks - 2);
            array->stripe_size = array->chunk_size_kb * (array->num_disks - 2) * 2;
        }
        break;
    case MD_RAID10:
        array->size_sectors = min_size * (array->num_disks / 2);
        array->stripe_size = array->chunk_size_kb * (array->num_disks / 2) * 2;
        break;
    case MD_LINEAR:
    case MD_JBOD:
        array->size_sectors = 0;
        for (uint8_t i = 0; i < array->num_disks; i++) {
            array->size_sectors += g_md_disks[array_index][i].size_sectors;
        }
        if (array->size_sectors == 0) {
            array->size_sectors = min_size * array->num_disks;
        }
        array->stripe_size = array->chunk_size_kb * 2;
        break;
    default:
        array->size_sectors = min_size;
        array->stripe_size = array->chunk_size_kb * 2;
        break;
    }

    array->state = MD_STATE_ACTIVE;

    /* Update total size */
    g_md_info.total_size_mb = (uint32_t)(array->size_sectors / 2048); /* sectors to MB */

    strcpy(g_md_info.status, "md: array started");
    log_write("md: array started");
    return 0;
}

/* Stop an MD array */
int32_t md_stop_array(uint8_t array_index)
{
    if (array_index >= g_md_info.array_count) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];

    if (!(array->state & MD_STATE_ACTIVE)) {
        strcpy(g_md_info.status, "md: array not active");
        return -1;
    }

    array->state = MD_STATE_INACTIVE;

    strcpy(g_md_info.status, "md: array stopped");
    return 0;
}

/* Get array information */
int32_t md_get_array_info(uint8_t array_index, md_array_info_t *info)
{
    if (info == NULL || array_index >= g_md_info.array_count) {
        return -1;
    }

    memcpy(info, &g_md_arrays[array_index], sizeof(md_array_info_t));
    return 0;
}

/* Get disk information */
int32_t md_get_disk_info(uint8_t array_index, uint8_t slot, md_disk_info_t *info)
{
    if (info == NULL || array_index >= g_md_info.array_count || slot >= MD_MAX_RAID_DEVICES) {
        return -1;
    }

    memcpy(info, &g_md_disks[array_index][slot], sizeof(md_disk_info_t));
    return 0;
}

/* Read from MD array */
int32_t md_read(uint8_t array_index, uint64_t offset, uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0 || array_index >= g_md_info.array_count) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];

    if (!(array->state & MD_STATE_ACTIVE)) {
        strcpy(g_md_info.status, "md: array not active");
        return -1;
    }

    if (offset + len > array->size_sectors * 512) {
        strcpy(g_md_info.status, "md: read beyond array size");
        return -1;
    }

    /*
     * In a real implementation, this would:
     * 1. Calculate stripe and disk based on RAID level
     * 2. Read from appropriate disk(s)
     * 3. Reconstruct data if needed (for parity RAID)
     *
     * For now, return zeros.
     */
    memset(buf, 0, len);

    strcpy(g_md_info.status, "md: read completed");
    return (int32_t)len;
}

/* Write to MD array */
int32_t md_write(uint8_t array_index, uint64_t offset, const uint8_t *buf, uint32_t len)
{
    if (buf == NULL || len == 0 || array_index >= g_md_info.array_count) {
        return -1;
    }

    md_array_info_t *array = &g_md_arrays[array_index];

    if (!(array->state & MD_STATE_ACTIVE)) {
        strcpy(g_md_info.status, "md: array not active");
        return -1;
    }

    if (offset + len > array->size_sectors * 512) {
        strcpy(g_md_info.status, "md: write beyond array size");
        return -1;
    }

    /*
     * In a real implementation, this would:
     * 1. Calculate stripe and disk based on RAID level
     * 2. Write to appropriate disk(s)
     * 3. Calculate and write parity if needed
     *
     * For now, just track used sectors.
     */
    uint64_t end = offset + len;
    if (end > array->used_sectors) {
        array->used_sectors = end;
    }

    strcpy(g_md_info.status, "md: write completed");
    return (int32_t)len;
}

const md_info_t *md_info(void)
{
    return &g_md_info;
}

const char *md_status(void)
{
    return g_md_info.status;
}
