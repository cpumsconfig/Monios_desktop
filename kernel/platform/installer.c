#include "app_memory.h"
#include "common.h"
#include "exec.h"
#include "extfs.h"
#include "file.h"
#include "ide.h"
#include "installer.h"
#include "iso9660.h"
#include "fat16.h"
#include "fat32.h"
#include "ntfs.h"
#include "kernel.h"
#include "stddef.h"
#include "string.h"

#define ATA_DATA_PORT         0x1F0
#define ATA_SECTOR_COUNT_PORT 0x1F2
#define ATA_LBA_LOW_PORT      0x1F3
#define ATA_LBA_MID_PORT      0x1F4
#define ATA_LBA_HIGH_PORT     0x1F5
#define ATA_DRIVE_PORT        0x1F6
#define ATA_COMMAND_PORT      0x1F7
#define ATA_STATUS_PORT       0x1F7

#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_STATUS_BSY        0x80
#define ATA_STATUS_DRQ        0x08
#define ATA_WAIT_LIMIT        1000000U
#define INSTALLER_SECTOR_SIZE 512U
#define INSTALLER_BUFFER_SECTORS 256U
#define INSTALLER_DEFAULT_PARTITION_LBA 2048U

static uint8_t g_installer_disk_buffer[INSTALLER_SECTOR_SIZE * INSTALLER_BUFFER_SECTORS];

static uint16_t installer_read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t installer_read_le32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static bool installer_wait_not_busy(void)
{
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if ((inb(ATA_STATUS_PORT) & ATA_STATUS_BSY) == 0) {
            return true;
        }
    }
    return false;
}

static bool installer_wait_data_ready(void)
{
    if (!installer_wait_not_busy()) {
        return false;
    }
    for (uint32_t i = 0; i < ATA_WAIT_LIMIT; i++) {
        if ((inb(ATA_STATUS_PORT) & ATA_STATUS_DRQ) != 0) {
            return true;
        }
    }
    return false;
}

static bool installer_read_sector(uint32_t lba, void *buffer)
{
    uint16_t *dst = (uint16_t *) buffer;

    if (!installer_wait_not_busy()) {
        return false;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, 0x20);

    if (!installer_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < INSTALLER_SECTOR_SIZE / 2U; i++) {
        dst[i] = inw(ATA_DATA_PORT);
    }
    return true;
}

static bool installer_write_sector(uint32_t lba, const void *buffer)
{
    const uint16_t *src = (const uint16_t *) buffer;

    if (!installer_wait_not_busy()) {
        return false;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, 1);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);

    if (!installer_wait_data_ready()) {
        return false;
    }
    for (uint32_t i = 0; i < INSTALLER_SECTOR_SIZE / 2U; i++) {
        outw(ATA_DATA_PORT, src[i]);
    }
    return installer_wait_not_busy();
}

static bool installer_write_sectors(uint32_t lba, const void *buffer, uint32_t sector_count)
{
    const uint16_t *src = (const uint16_t *) buffer;

    if (sector_count == 0 || sector_count > 256U) {
        return false;
    }
    if (sector_count == 1) {
        return installer_write_sector(lba, buffer);
    }
    if (!installer_wait_not_busy()) {
        return false;
    }
    outb(ATA_DRIVE_PORT, (uint8_t) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT_PORT, sector_count == 256U ? 0 : (uint8_t) sector_count);
    outb(ATA_LBA_LOW_PORT, (uint8_t) (lba & 0xFF));
    outb(ATA_LBA_MID_PORT, (uint8_t) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH_PORT, (uint8_t) ((lba >> 16) & 0xFF));
    outb(ATA_COMMAND_PORT, ATA_CMD_WRITE_SECTORS);

    for (uint32_t sector = 0; sector < sector_count; sector++) {
        if (!installer_wait_data_ready()) {
            return false;
        }
        for (uint32_t i = 0; i < INSTALLER_SECTOR_SIZE / 2U; i++) {
            outw(ATA_DATA_PORT, *src++);
        }
    }
    return installer_wait_not_busy();
}

static bool installer_privileged(void)
{
    const exec_launch_info_t *info = exec_current_launch_info();

    return info != NULL &&
           info->privilege_level <= EXEC_PRIV_R2 &&
           info->program_path != NULL &&
           strcasecmp(info->program_path, "/SETUP.ELF") == 0 &&
           installer_boot_media_present();
}

static bool installer_lba_range_allowed(uint32_t disk_lba, uint32_t byte_count)
{
    installer_target_list_t list;
    uint32_t sectors;
    uint32_t end_lba;

    if (byte_count == 0 || (byte_count % INSTALLER_SECTOR_SIZE) != 0) {
        return false;
    }
    sectors = byte_count / INSTALLER_SECTOR_SIZE;
    end_lba = disk_lba + sectors;
    if (end_lba < disk_lba) {
        return false;
    }

    memset(&list, 0, sizeof(list));
    if (installer_list_targets(&list, sizeof(list)) < 0) {
        return false;
    }
    for (uint32_t i = 0; i < list.target_count && i < INSTALLER_MAX_TARGETS; i++) {
        const installer_target_info_t *target = &list.targets[i];
        uint32_t target_end = target->start_lba + target->sector_count;

        if (target->active == 0 || target->sector_count == 0 || target_end < target->start_lba) {
            continue;
        }
        if (disk_lba >= target->start_lba && end_lba <= target_end) {
            return true;
        }
    }
    return false;
}

static bool installer_partition_type_supported(uint8_t type)
{
    return type == 0x0B || type == 0x0C || type == 0x07 || type == 0x83 || type == 0xEF;
}

int32_t installer_list_targets(installer_target_list_t *list, uint32_t list_size)
{
    const ide_info_t *disk = ide_info();
    uint8_t mbr[INSTALLER_SECTOR_SIZE];
    uint32_t count = 0;

    if (!installer_privileged() || list == NULL || list_size < sizeof(*list)) {
        return -1;
    }
    memset(list, 0, sizeof(*list));
    if (disk == NULL || !disk->present) {
        return 0;
    }

    list->disk_present = 1;
    list->disk_sector_count = disk->sectors;
    if (disk->model[0] != '\0') {
        strcpy(list->disk_model, disk->model);
    } else {
        strcpy(list->disk_model, "ATA primary master");
    }

    if (disk->sectors > INSTALLER_DEFAULT_PARTITION_LBA + 4096U) {
        list->targets[count].kind = INSTALLER_TARGET_KIND_UEFI_ESP;
        list->targets[count].partition_index = 0;
        list->targets[count].active = 1;
        list->targets[count].partition_type = 0xEF;
        list->targets[count].start_lba = INSTALLER_DEFAULT_PARTITION_LBA;
        list->targets[count].sector_count = disk->sectors - INSTALLER_DEFAULT_PARTITION_LBA;
        count++;
    }

    if (count < INSTALLER_MAX_TARGETS) {
        list->targets[count].kind = INSTALLER_TARGET_KIND_DISK;
        list->targets[count].partition_index = INSTALLER_TARGET_WHOLE_DISK;
        list->targets[count].active = 1;
        list->targets[count].partition_type = 0;
        list->targets[count].start_lba = 0;
        list->targets[count].sector_count = disk->sectors;
        count++;
    }

    if (installer_read_sector(0, mbr) && installer_read_le16(mbr + 510) == 0xAA55) {
        for (uint32_t i = 0; i < 4 && count < INSTALLER_MAX_TARGETS; i++) {
            uint8_t *entry = mbr + 446 + i * 16U;
            uint8_t type = entry[4];
            uint32_t start_lba = installer_read_le32(entry + 8);
            uint32_t sectors = installer_read_le32(entry + 12);

            if (type == 0 || start_lba == 0 || sectors == 0) {
                continue;
            }
            list->targets[count].kind = INSTALLER_TARGET_KIND_PART;
            list->targets[count].partition_index = (uint8_t) i;
            list->targets[count].active = (entry[0] == 0x80) ? 1 : 0;
            list->targets[count].partition_type = type;
            list->targets[count].start_lba = start_lba;
            list->targets[count].sector_count = sectors;
            if (!installer_partition_type_supported(type)) {
                list->targets[count].active = 0;
            }
            count++;
        }
    }

    list->target_count = count;
    return (int32_t) count;
}

static bool installer_mount_target_partition(uint32_t start_lba)
{
    return file_mount("/", "fat32", (int32_t) start_lba) ||
           file_mount("/", "fat16", (int32_t) start_lba) ||
           file_mount("/", "ntfs", (int32_t) start_lba) ||
           file_mount("/", "extfs", (int32_t) start_lba);
}

bool installer_boot_media_present(void)
{
    if (file_exists("/INSTALL.FLG") &&
        (file_exists("/SYSTEM_UEFI.IMG") || file_exists("/SYSTEM_MBR.IMG"))) {
        return true;
    }
    if (iso9660_init()) {
        return iso9660_exists("/INSTALL.FLG") &&
               (iso9660_exists("/SYSTEM_UEFI.IMG") || iso9660_exists("/SYSTEM_MBR.IMG"));
    }
    return false;
}

int32_t installer_write_file_to_disk(const char *source_path,
                                     uint32_t source_offset,
                                     uint32_t disk_lba,
                                     uint32_t byte_count)
{
    uint32_t sectors;
    uint32_t done = 0;

    if (!installer_privileged() || source_path == NULL ||
        byte_count == 0 || (byte_count % INSTALLER_SECTOR_SIZE) != 0) {
        return -1;
    }
    sectors = byte_count / INSTALLER_SECTOR_SIZE;
    if (!installer_lba_range_allowed(disk_lba, byte_count)) {
        return -1;
    }

    while (done < sectors) {
        uint32_t batch_sectors = sectors - done;
        uint32_t batch_bytes;
        int32_t read_bytes;

        if (batch_sectors > INSTALLER_BUFFER_SECTORS) {
            batch_sectors = INSTALLER_BUFFER_SECTORS;
        }
        batch_bytes = batch_sectors * INSTALLER_SECTOR_SIZE;
        read_bytes = file_read_at(source_path,
                                  source_offset + done * INSTALLER_SECTOR_SIZE,
                                  g_installer_disk_buffer,
                                  batch_bytes);
        if (read_bytes != (int32_t) batch_bytes && iso9660_init()) {
            read_bytes = iso9660_read_file_at(source_path,
                                              source_offset + done * INSTALLER_SECTOR_SIZE,
                                              g_installer_disk_buffer,
                                              batch_bytes);
        }
        if (read_bytes != (int32_t) batch_bytes) {
            return (int32_t) (done * INSTALLER_SECTOR_SIZE);
        }
        if (!installer_write_sectors(disk_lba + done, g_installer_disk_buffer, batch_sectors)) {
            return (int32_t) (done * INSTALLER_SECTOR_SIZE);
        }
        done += batch_sectors;
    }
    return (int32_t) byte_count;
}

int32_t installer_write_buffer_to_disk(const void *data, uint32_t disk_lba, uint32_t byte_count)
{
    if (!installer_privileged() || data == NULL ||
        byte_count == 0 || (byte_count % INSTALLER_SECTOR_SIZE) != 0) {
        return -1;
    }
    if (!installer_lba_range_allowed(disk_lba, byte_count)) {
        return -1;
    }
    if (!installer_write_sectors(disk_lba, data, byte_count / INSTALLER_SECTOR_SIZE)) {
        return 0;
    }
    return (int32_t) byte_count;
}

int32_t installer_write_target_file(const char *target_path,
                                    const void *data,
                                    uint32_t target_lba,
                                    uint32_t byte_count)
{
    if (!installer_privileged() || target_path == NULL || data == NULL || byte_count == 0) {
        return -1;
    }
    if (!installer_lba_range_allowed(target_lba, INSTALLER_SECTOR_SIZE)) {
        return -1;
    }
    file_init();
    if (!installer_mount_target_partition(target_lba)) {
        return -2;
    }
    return file_write(target_path, data, byte_count);
}

void installer_request_reboot(void)
{
    if (installer_privileged()) {
        kernel_request_reboot();
    }
}
