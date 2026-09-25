/*
 * backup.c - Monios x64 one-click raw partition backup / restore.
 *
 *   backup C:  D:\backup.img     back up the whole system (FAT32) volume
 *   backup /restore D:\backup.img  restore the volume from an image file
 *
 * The heavy lifting (raw sector I/O + streaming image write) is done in the
 * kernel behind SYS_BACKUP_CTL; this app only drives the chunk loop so it can
 * print a progress percentage to its own stdout.  Only 128KB of buffer is
 * touched at a time, so it fits comfortably in the 64MB RAM budget.
 */

#include "stdio.h"
#include "stddef.h"
#include "string.h"
#include "appsys.h"
#include "monios_dll.h"

#define BACKUP_CHUNK_SECTORS 256u          /* 128KB per chunk */
#define BACKUP_BUF_SIZE       (BACKUP_CHUNK_SECTORS * 512u)

/* op codes for SYS_BACKUP_CTL */
#define OP_BEGIN_BACKUP    1u
#define OP_READ_SRC        2u
#define OP_APPEND_IMG      3u
#define OP_END_BACKUP      4u
#define OP_BEGIN_RESTORE   5u
#define OP_READ_IMG        6u
#define OP_WRITE_SRC       7u
#define OP_END_RESTORE     8u

static uint8_t g_buf[BACKUP_BUF_SIZE];

static void print_progress(uint64_t done_bytes, uint64_t total_bytes)
{
    uint32_t pct;

    if (total_bytes == 0) {
        return;
    }
    pct = (uint32_t) ((done_bytes * 100ULL) / total_bytes);
    fputs("\r  [");
    /* crude bar */
    {
        uint32_t filled = pct / 5u;
        uint32_t i;
        for (i = 0; i < 20u; i++) {
            fputs(i < filled ? "#" : "-");
        }
    }
    fputs("] ");
    print_uint(pct);
    fputs("%   ");
    print_uint((uint32_t)(done_bytes / 1024u));
    fputs(" KB / ");
    print_uint((uint32_t)(total_bytes / 1024u));
    fputs(" KB");
}

static int do_backup(const char *image_path)
{
    int64_t total;
    uint64_t lba = 0;
    uint64_t total_bytes;

    fputs("Monios one-click backup\n");
    fputs("Source: system volume (C:)\n");
    fputs("Image : ");
    fputs(image_path);
    fputs("\n");

    total = (int64_t) app_backup_ctl(OP_BEGIN_BACKUP, (uint64_t) image_path, 0, 0, 0);
    if (total <= 0) {
        fputs("ERROR: cannot open image or read volume geometry (code ");
        print_int((int32_t) total);
        fputs(").\n");
        return 1;
    }
    total_bytes = (uint64_t) total * 512ULL;
    fputs("Volume size: ");
    print_uint((uint32_t) total);
    fputs(" sectors (");
    print_uint((uint32_t)(total_bytes / (1024u * 1024u)));
    fputs(" MB).\n\n");

    while (lba < (uint64_t) total) {
        uint32_t sectors = BACKUP_CHUNK_SECTORS;
        uint32_t rem = (uint32_t)((uint64_t) total - lba);
        int64_t r;
        int64_t w;

        if (sectors > rem) {
            sectors = rem;
        }
        r = (int64_t) app_backup_ctl(OP_READ_SRC, lba, (uint64_t) sectors,
                                     (uint64_t) g_buf, 0);
        if (r != 0) {
            fputs("\nERROR: reading source sector ");
            print_uint((uint32_t) lba);
            fputs("\n");
            app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
            return 1;
        }
        w = (int64_t) app_backup_ctl(OP_APPEND_IMG, (uint64_t) g_buf,
                                     (uint64_t)(sectors * 512u), 0, 0);
        if (w <= 0) {
            fputs("\nERROR: writing image file (disk full?)\n");
            app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
            return 1;
        }
        lba += sectors;
        print_progress(lba * 512ULL, total_bytes);
    }

    app_backup_ctl(OP_END_BACKUP, 0, 0, 0, 0);
    fputs("\n\nBackup complete. Image written to: ");
    fputs(image_path);
    fputs("\n");
    return 0;
}

static int do_restore(const char *image_path)
{
    int64_t size;
    uint64_t offset = 0;

    fputs("Monios restore\n");
    fputs("Image : ");
    fputs(image_path);
    fputs("\n");

    size = (int64_t) app_backup_ctl(OP_BEGIN_RESTORE, (uint64_t) image_path, 0, 0, 0);
    if (size <= 0) {
        fputs("ERROR: cannot open image file (code ");
        print_int((int32_t) size);
        fputs(").\n");
        return 1;
    }

    fputs("WARNING: this OVERWRITES the entire system volume with the image.\n");
    fputs("Type Y and press Enter to continue, anything else to abort: ");

    {
        const app_launch_info_t *launch = app_launch_info();
        char yes[4];
        int n = monios_handle_read(launch->stdin_handle, yes, sizeof(yes));
        if (n <= 0 || (yes[0] != 'y' && yes[0] != 'Y')) {
            fputs("\nRestore aborted.\n");
            return 1;
        }
    }

    fputs("\nRestoring...\n");
    while (offset < (uint64_t) size) {
        uint32_t want = BACKUP_BUF_SIZE;
        uint32_t rem = (uint32_t)((uint64_t) size - offset);
        int64_t got;
        int64_t w;
        uint32_t sectors;

        if (want > rem) {
            want = rem;
        }
        got = (int64_t) app_backup_ctl(OP_READ_IMG, (uint64_t) image_path,
                                       offset, (uint64_t) g_buf, (uint64_t) want);
        if (got <= 0) {
            fputs("\nERROR: reading image at offset ");
            print_uint((uint32_t) offset);
            fputs("\n");
            app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
            return 1;
        }
        sectors = (uint32_t) got / 512u;
        if (sectors > 0) {
            w = (int64_t) app_backup_ctl(OP_WRITE_SRC, offset / 512u,
                                         (uint64_t) sectors,
                                         (uint64_t) g_buf, 0);
            if (w != 0) {
                fputs("\nERROR: writing source volume at LBA ");
                print_uint((uint32_t)(offset / 512u));
                fputs("\n");
                app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
                return 1;
            }
        }
        offset += (uint64_t) got;
        print_progress(offset, (uint64_t) size);
    }

    app_backup_ctl(OP_END_RESTORE, 0, 0, 0, 0);
    fputs("\n\nRestore complete. Please REBOOT now.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    const char *image_path;
    bool restore = false;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "/restore") == 0 || strcmp(argv[i], "-r") == 0 ||
            strcmp(argv[i], "restore") == 0) {
            restore = true;
        } else if (strcmp(argv[i], "/?") == 0 || strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            fputs("Usage:\n");
            fputs("  backup C: <image.img>            back up system volume to image\n");
            fputs("  backup /restore <image.img>      restore system volume from image\n");
            return 0;
        }
    }

    /* need elevated privilege for raw sector access */
    if (!app_request_r2("One-click backup requires raw disk access")) {
        fputs("ERROR: elevation denied (need admin/R2).\n");
        return 1;
    }

    /* image path = last argument; ignore a leading drive spec like "C:" */
    image_path = NULL;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "/restore") == 0 || strcmp(a, "-r") == 0 ||
            strcmp(a, "restore") == 0 || strcmp(a, "/?") == 0 ||
            strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            continue;
        }
        if ((a[0] >= 'A' && a[0] <= 'Z' && a[1] == ':') ||
            (a[0] >= 'a' && a[0] <= 'z' && a[1] == ':')) {
            continue;   /* source drive specifier, e.g. "C:" */
        }
        image_path = a;
    }

    if (image_path == NULL) {
        fputs("Usage: backup C: <image.img>  |  backup /restore <image.img>\n");
        return 1;
    }

    if (restore) {
        return do_restore(image_path);
    }
    return do_backup(image_path);
}
