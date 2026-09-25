/*
 * stubs.c — host-side (Windows / MinGW) stub implementations for the
 * kernel- and FS-driver dependencies referenced by lib/file.c and
 * lib/path.c. This lets the VFS layer be compiled and unit-tested on the
 * host without booting Monios.
 *
 * Only the minimum needed to link and exercise the VFS routing / mount
 * table / cache-key / disk-space logic is provided.
 */

#include "common.h"
#include "blockdev.h"
#include "cmos.h"
#include "extfs.h"
#include "fat16.h"
#include "fat32.h"
#include "fs_cache.h"
#include "gpt.h"
#include "iso9660.h"
#include "netfs.h"
#include "ntfs.h"
#include "path.h"
#include "string.h"

#include "stub_values.h"

/* Host CRT allocation entry points.  Avoid including the host stdlib headers,
 * which conflict with Monios' freestanding integer typedefs. */
void *malloc(unsigned long long size);
void free(void *ptr);

/* ============================================================
 *  Minimal libc / string routines (Monios's own string.h ABI)
 * ============================================================ */
void *memset(void *dst_, uint8_t value, uint64_t size)
{
    uint8_t *d = (uint8_t *) dst_;
    while (size--) {
        *d++ = value;
    }
    return dst_;
}

void *memcpy(void *dst_, const void *src_, uint64_t size)
{
    uint8_t *d = (uint8_t *) dst_;
    const uint8_t *s = (const uint8_t *) src_;
    while (size--) {
        *d++ = *s++;
    }
    return dst_;
}

void *memmove(void *dst_, const void *src_, uint64_t size)
{
    uint8_t *d = (uint8_t *) dst_;
    const uint8_t *s = (const uint8_t *) src_;
    if (d < s) {
        while (size--) {
            *d++ = *s++;
        }
    } else {
        d += size;
        s += size;
        while (size--) {
            *--d = *--s;
        }
    }
    return dst_;
}

int memcmp(const void *a_, const void *b_, uint64_t size)
{
    const uint8_t *a = (const uint8_t *) a_;
    const uint8_t *b = (const uint8_t *) b_;
    while (size--) {
        if (*a != *b) {
            return (int) *a - (int) *b;
        }
        a++;
        b++;
    }
    return 0;
}

char *strcpy(char *dst_, const char *src_)
{
    char *r = dst_;
    while ((*dst_++ = *src_++) != '\0') {
    }
    return r;
}

char *strncpy(char *dst_, const char *src_, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n && src_[i] != '\0'; i++) {
        dst_[i] = src_[i];
    }
    for (; i < n; i++) {
        dst_[i] = '\0';
    }
    return dst_;
}

uint64_t strlcpy(char *dst_, const char *src_, uint64_t size)
{
    uint64_t i = 0;

    while (i + 1 < size && src_[i] != '\0') {
        dst_[i] = src_[i];
        i++;
    }
    if (size > 0) {
        dst_[i] = '\0';
    }
    while (src_[i] != '\0') {
        i++;
    }
    return i;
}

uint64_t strlen(const char *str)
{
    uint64_t n = 0;
    while (str[n] != '\0') {
        n++;
    }
    return n;
}

int8_t strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int8_t) ((uint8_t) *a - (uint8_t) *b);
}

int8_t strncmp(const char *a, const char *b, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return (int8_t) ((uint8_t) a[i] - (uint8_t) b[i]);
        }
        if (a[i] == '\0') {
            break;
        }
    }
    return 0;
}

int8_t strcasecmp(const char *a, const char *b)
{
    while (*a != '\0') {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char) (ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char) (cb - 'A' + 'a');
        }
        if (ca != cb) {
            return (int8_t) ((uint8_t) ca - (uint8_t) cb);
        }
        a++;
        b++;
    }
    return 0;
}

char *strchr(const char *str, const uint8_t ch)
{
    while (*str != '\0') {
        if ((uint8_t) *str == ch) {
            return (char *) str;
        }
        str++;
    }
    if (ch == '\0') {
        return (char *) str;
    }
    return (char *) 0;
}

char *strrchr(const char *str, int ch)
{
    const char *last = (const char *) 0;
    while (*str != '\0') {
        if (*str == ch) {
            last = str;
        }
        str++;
    }
    if (ch == '\0') {
        return (char *) str;
    }
    return (char *) last;
}

char *strcat(char *dst_, const char *src_)
{
    char *r = dst_;
    while (*dst_ != '\0') {
        dst_++;
    }
    while ((*dst_++ = *src_++) != '\0') {
    }
    return r;
}

/* ============================================================
 *  x86 I/O port stubs (no hardware on host)
 * ============================================================ */
void outb(uint16_t port, uint8_t value) { (void) port; (void) value; }
void outw(uint16_t port, uint16_t value) { (void) port; (void) value; }
void outl(uint16_t port, uint32_t value) { (void) port; (void) value; }
uint8_t inb(uint16_t port) { (void) port; return 0; }
uint16_t inw(uint16_t port) { (void) port; return 0; }
uint32_t inl(uint16_t port) { (void) port; return 0; }
void io_wait(void) { }

/* ============================================================
 *  Block device stubs — report no disk so auto-mount scans bail out
 * ============================================================ */
bool blockdev_init(void) { return true; }
int blockdev_count(void) { return 0; }
const blockdev_t *blockdev_get(int index) { (void) index; return (const blockdev_t *) 0; }
bool blockdev_read_sector(int dev_index, uint64_t lba, void *buffer)
{
    (void) dev_index; (void) lba; (void) buffer;
    return false;
}
bool blockdev_read_sectors(int dev_index, uint64_t lba, uint32_t count, void *buffer)
{
    (void) dev_index; (void) lba; (void) count; (void) buffer;
    return false;
}

/* ============================================================
 *  FS cache stubs — bypass the cache and call the loader directly
 * ============================================================ */
static fs_cache_info_t g_fs_cache_info;
void test_set_dirty_count(uint32_t count) { g_fs_cache_info.dirty_count = count; }

void fs_cache_init(void)
{
    g_fs_cache_info.enabled = true;
    g_fs_cache_info.slots = 0;
    g_fs_cache_info.block_size = 0;
    g_fs_cache_info.hits = 0;
    g_fs_cache_info.misses = 0;
    g_fs_cache_info.fills = 0;
    g_fs_cache_info.invalidations = 0;
    g_fs_cache_info.status[0] = '\0';
}

int32_t fs_cache_read_at(const char *path, uint32_t offset, void *buffer,
                         uint32_t size, fs_cache_loader_t loader)
{
    g_fs_cache_info.misses++;
    return loader(path, offset, buffer, size);
}

void fs_cache_invalidate_path(const char *path) { (void) path; g_fs_cache_info.invalidations++; }
void fs_cache_invalidate_all(void) { g_fs_cache_info.invalidations++; }
const fs_cache_info_t *fs_cache_info(void) { return &g_fs_cache_info; }
const char *fs_cache_status(void) { return "stub"; }

/* ============================================================
 *  FAT32 stubs
 * ============================================================ */
bool fat32_init(void) { return true; }
uint16_t fat32_root_entry_count(void) { return 0; }
bool fat32_exists(const char *path) { (void) path; return false; }
bool fat32_is_dir(const char *path) { (void) path; return false; }
int32_t fat32_file_size(const char *path) { (void) path; return -1; }
int32_t fat32_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return 0; }
int32_t fat32_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return 0; }
int32_t fat32_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool fat32_delete(const char *path) { (void) path; return false; }
bool fat32_mkdir(const char *path) { (void) path; return false; }
bool fat32_rmdir(const char *path) { (void) path; return false; }
bool fat32_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool fat32_list_root(char *buffer, uint32_t bs) { (void) buffer; (void) bs; return false; }

/* FAT32 symbolic-link stubs (content-magic based; unused by host VFS tests) */
bool fat32_symlink(const char *target, const char *linkpath)
{ (void) target; (void) linkpath; return false; }
bool fat32_is_symlink(const char *path) { (void) path; return false; }
int32_t fat32_read_symlink(const char *path, char *target, uint32_t ts)
{ (void) path; (void) target; (void) ts; return -1; }

/* FAT32 format / chkdsk stubs (batch-1 VFS entry points; unused by host tests) */
bool fat32_format(uint32_t volume_lba, uint64_t sector_count, uint32_t sector_size)
{ (void) volume_lba; (void) sector_count; (void) sector_size; return false; }
bool fat32_chkdsk(fat32_chkdsk_report_t *report) { (void) report; return false; }

uint64_t fat32_total_bytes(uint64_t *free_out, uint32_t *cluster_size_out)
{
    if (free_out != 0) {
        *free_out = STUB_FAT32_FREE;
    }
    if (cluster_size_out != 0) {
        *cluster_size_out = STUB_FAT32_CLUSTER;
    }
    return STUB_FAT32_TOTAL;
}

/* ============================================================
 *  FAT16 stubs
 * ============================================================ */
bool fat16_init(void) { return true; }
uint16_t fat16_root_entry_count(void) { return 0; }
bool fat16_exists(const char *path) { (void) path; return false; }
bool fat16_is_dir(const char *path) { (void) path; return false; }
int32_t fat16_file_size(const char *path) { (void) path; return -1; }
int32_t fat16_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return 0; }
int32_t fat16_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return 0; }
int32_t fat16_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool fat16_delete(const char *path) { (void) path; return false; }
bool fat16_mkdir(const char *path) { (void) path; return false; }
bool fat16_rmdir(const char *path) { (void) path; return false; }
bool fat16_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool fat16_list_root(char *buffer, uint32_t bs) { (void) buffer; (void) bs; return false; }

uint64_t fat16_total_bytes(uint64_t *free_out, uint32_t *cluster_size_out)
{
    if (free_out != 0) {
        *free_out = STUB_FAT16_FREE;
    }
    if (cluster_size_out != 0) {
        *cluster_size_out = STUB_FAT16_CLUSTER;
    }
    return STUB_FAT16_TOTAL;
}

/* ============================================================
 *  ISO9660 stubs
 * ============================================================ */
static iso9660_info_t g_iso_info;

bool iso9660_init(void)
{
    g_iso_info.present = true;
    g_iso_info.ready = true;
    g_iso_info.block_size = STUB_ISO_BLOCK_SIZE;
    g_iso_info.total_blocks = STUB_ISO_BLOCKS;
    return true;
}
uint16_t iso9660_root_entry_count(void) { return 0; }
bool iso9660_exists(const char *path) { (void) path; return false; }
bool iso9660_is_dir(const char *path) { (void) path; return false; }
int32_t iso9660_file_size(const char *path) { (void) path; return -1; }
int32_t iso9660_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return 0; }
int32_t iso9660_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return 0; }
int32_t iso9660_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool iso9660_delete(const char *path) { (void) path; return false; }
bool iso9660_mkdir(const char *path) { (void) path; return false; }
bool iso9660_rmdir(const char *path) { (void) path; return false; }
bool iso9660_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool iso9660_list_root(char *buffer, uint32_t bs) { (void) buffer; (void) bs; return false; }
const iso9660_info_t *iso9660_info(void) { return &g_iso_info; }
const char *iso9660_status(void) { return "stub"; }

/* ============================================================
 *  NTFS stubs
 * ============================================================ */
static ntfs_info_t g_ntfs_info;

bool ntfs_init(void)
{
    g_ntfs_info.present = true;
    g_ntfs_info.read_only = false;
    g_ntfs_info.bytes_per_sector = STUB_NT2_BYTES_PER_SECTOR;
    g_ntfs_info.total_sectors = STUB_NT2_TOTAL_SECTORS;
    g_ntfs_info.cluster_size = STUB_NT2_CLUSTER;
    return true;
}
uint16_t ntfs_root_entry_count(void) { return 0; }
bool ntfs_exists(const char *path) { (void) path; return false; }
bool ntfs_is_dir(const char *path) { (void) path; return false; }
int32_t ntfs_file_size(const char *path) { (void) path; return -1; }
int32_t ntfs_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return 0; }
int32_t ntfs_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return 0; }
int32_t ntfs_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool ntfs_delete(const char *path) { (void) path; return false; }
bool ntfs_mkdir(const char *path) { (void) path; return false; }
bool ntfs_rmdir(const char *path) { (void) path; return false; }
bool ntfs_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool ntfs_list_root(char *buffer, uint32_t bs) { (void) buffer; (void) bs; return false; }
const ntfs_info_t *ntfs_info(void) { return &g_ntfs_info; }
const char *ntfs_status(void) { return "stub"; }
uint64_t ntfs_free_bytes(void) { return 0; }

/* ============================================================
 *  EXTFS stubs
 * ============================================================ */
static extfs_info_t g_ext_info;

bool extfs_init(void)
{
    g_ext_info.present = true;
    g_ext_info.read_only = true;
    g_ext_info.block_size = STUB_EXT_BLOCK_SIZE;
    g_ext_info.blocks_count = STUB_EXT_BLOCKS;
    g_ext_info.free_blocks = STUB_EXT_FREE;
    return true;
}
static bool ext_sync_ok = true;
void test_set_ext_sync_ok(bool ok) { ext_sync_ok = ok; }
bool extfs_sync(void) { return ext_sync_ok; }
uint16_t extfs_root_entry_count(void) { return 0; }
bool extfs_exists(const char *path) { (void) path; return false; }
bool extfs_is_dir(const char *path) { (void) path; return false; }
bool extfs_is_symlink(const char *path) { (void) path; return false; }
int32_t extfs_file_size(const char *path) { (void) path; return -1; }
int32_t extfs_read_symlink(const char *path, char *target, uint32_t ts)
{ (void) path; (void) target; (void) ts; return -1; }
int32_t extfs_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return 0; }
int32_t extfs_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return 0; }
int32_t extfs_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool extfs_delete(const char *path) { (void) path; return false; }
bool extfs_mkdir(const char *path) { (void) path; return false; }
bool extfs_rmdir(const char *path) { (void) path; return false; }
bool extfs_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool extfs_list_root(char *buffer, uint32_t bs) { (void) buffer; (void) bs; return false; }
const extfs_info_t *extfs_info(void) { return &g_ext_info; }
const char *extfs_status(void) { return "stub"; }

/* ============================================================
 *  GPT partition-table stubs (only referenced by file_auto_mount scans,
 *  which the host tests do not exercise; report "no GPT" so linking
 *  succeeds and auto-mount falls back to the MBR path).
 * ============================================================ */
bool gpt_detect(uint8_t sector0[512]) { (void) sector0; return false; }

bool gpt_read_header(gpt_read_sector_fn read_sector, gpt_header_t *header)
{
    (void) read_sector; (void) header;
    return false;
}

int gpt_read_partitions(gpt_read_sector_fn read_sector,
                        const gpt_header_t *header,
                        gpt_part_entry_t *entries,
                        int max_entries)
{
    (void) read_sector; (void) header; (void) entries; (void) max_entries;
    return 0;
}

fs_type_t gpt_partition_fs_type(const gpt_part_entry_t *entry)
{
    (void) entry;
    return FS_TYPE_NONE;
}

/* ============================================================
 *  NETFS stubs — the host has no TCP stack, so every MNFS
 *  operation fails. This keeps the VFS routing tests linkable
 *  while the FS_TYPE_NETFS dispatch paths are simply unreachable
 *  (no network drive can be mapped on the host).
 * ============================================================ */
bool netfs_init(const char *server, uint16_t port, const char *share)
{ (void) server; (void) port; (void) share; return false; }
bool netfs_exists(const char *path) { (void) path; return false; }
bool netfs_is_dir(const char *path) { (void) path; return false; }
int32_t netfs_file_size(const char *path) { (void) path; return -1; }
int32_t netfs_read_file(const char *path, void *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return -1; }
int32_t netfs_read_file_at(const char *path, uint32_t off, void *buffer, uint32_t bs)
{ (void) path; (void) off; (void) buffer; (void) bs; return -1; }
int32_t netfs_write_file(const char *path, const void *buffer, uint32_t size)
{ (void) path; (void) buffer; (void) size; return -1; }
bool netfs_delete(const char *path) { (void) path; return false; }
bool netfs_mkdir(const char *path) { (void) path; return false; }
bool netfs_rmdir(const char *path) { (void) path; return false; }
bool netfs_list_dir(const char *path, char *buffer, uint32_t bs)
{ (void) path; (void) buffer; (void) bs; return false; }
bool netfs_connected(void) { return false; }
void netfs_disconnect(void) { }

/* ============================================================
 *  Services added to the VFS after the original host harness.
 *  Keep these deliberately conservative: permission checks allow the test
 *  process, persistence helpers report unavailable, and allocation uses the
 *  host CRT so file_copy_regular can be exercised safely.
 * ============================================================ */
uint32_t fs_cache_flush(void) { return 0; }
void fat32_wal_mark_clean(void) { }
bool fs_perm_check(const char *path, bool want_write, bool want_exec)
{ (void) path; (void) want_write; (void) want_exec; return true; }

void *kmalloc(uint64_t size) { return malloc((unsigned long long) size); }
void kfree(void *ptr) { free(ptr); }

bool fat32_rename(const char *oldpath, const char *newpath)
{ (void) oldpath; (void) newpath; return false; }
bool ntfs_rename(const char *oldpath, const char *newpath)
{ (void) oldpath; (void) newpath; return false; }
bool extfs_rename(const char *oldpath, const char *newpath)
{ (void) oldpath; (void) newpath; return false; }

bool fat32_stat(const char *path, fat32_stat_t *out)
{ (void) path; (void) out; return false; }
bool ntfs_stat(const char *path, ntfs_stat_t *out)
{ (void) path; (void) out; return false; }
bool extfs_stat(const char *path, extfs_stat_t *out)
{ (void) path; (void) out; return false; }
bool fat32_set_attr(const char *path, uint8_t mask, uint8_t value)
{ (void) path; (void) mask; (void) value; return false; }

bool fat32_volume_geometry(uint32_t *start, uint64_t *total,
                           uint32_t *sector_size, uint32_t *cluster_sectors)
{ (void) start; (void) total; (void) sector_size; (void) cluster_sectors; return false; }
bool fat32_read_volume(uint32_t lba, uint32_t count, void *buffer)
{ (void) lba; (void) count; (void) buffer; return false; }
bool fat32_write_volume(uint32_t lba, uint32_t count, const void *buffer)
{ (void) lba; (void) count; (void) buffer; return false; }
int32_t fat32_append_open(const char *path) { (void) path; return -1; }
int32_t fat32_append_write(const void *buffer, uint32_t len)
{ (void) buffer; (void) len; return -1; }
int32_t fat32_append_close(void) { return -1; }

void cmos_read_time(cmos_time_t *out)
{ if (out != (cmos_time_t *) 0) memset(out, 0, sizeof(*out)); }
const char *registry_get(const char *key) { (void) key; return (const char *) 0; }
