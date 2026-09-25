/*
 * test_vfs.c — host-side (Windows / MinGW) regression tests for the Monios
 * VFS layer: lib/path.c (path resolution) and lib/file.c (mount table,
 * drive routing, disk space, cache key, drive query API).
 *
 * Build:  see build_test.bat   (gcc -I ../include -I . -o test_vfs.exe test_vfs.c stubs.c)
 *
 * The code under test is pulled in by #include so that file.c's file-private
 * (static) helpers such as find_mount_point / file_build_cache_key are
 * reachable directly from these tests. Kernel / FS-driver dependencies are
 * supplied by stubs.c.
 */

/* Avoid host <stdio.h> (it would pull host stdint/stdbool and clash with the
 * Monios headers below). We only need printf, which MinGW resolves from msvcrt. */
int printf(const char *fmt, ...);

#include "common.h"
#include "file.h"
#include "fs_cache.h"
#include "ntfs.h"
#include "path.h"
#include "string.h"

#include "stub_values.h"

/* ---- code under test (same translation unit => statics reachable) ---- */
#include "../lib/path.c"
#include "../lib/file.c"

/* ============================================================
 *  Tiny test harness
 * ============================================================ */
static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (cond) {                                                        \
            g_pass++;                                                      \
        } else {                                                           \
            g_fail++;                                                      \
            printf("    FAIL %s  (%s:%d)\n", #cond, __FILE__, __LINE__);   \
        }                                                                  \
    } while (0)

static void section(const char *name)
{
    printf("\n== %s ==\n", name);
}

/* ============================================================
 *  Path resolution tests
 * ============================================================ */
static void expect_resolve(const char *base, const char *input,
                           const char *expected, bool expect_ok)
{
    char out[PATH_MAX_LEN];
    bool ok = path_resolve(base, input, out, sizeof(out));

    if (ok != expect_ok) {
        g_fail++;
        printf("    FAIL path_resolve(base=\"%s\", input=\"%s\"): ok=%d expected=%d\n",
               base ? base : "(null)", input, (int) ok, (int) expect_ok);
        return;
    }
    if (expect_ok && strcmp(out, expected) != 0) {
        g_fail++;
        printf("    FAIL path_resolve(base=\"%s\", input=\"%s\"): got=\"%s\" expected=\"%s\"\n",
               base ? base : "(null)", input, out, expected);
        return;
    }
    g_pass++;
}

static void test_path_resolve(void)
{
    section("path_resolve");

    expect_resolve((const char *) 0, "C:\\foo\\bar", "C:\\foo\\bar", true);
    expect_resolve((const char *) 0, "c:\\foo", "C:\\foo", true);          /* lowercase drive -> upper */
    expect_resolve((const char *) 0, "C:/foo", (const char *) 0, false);   /* '/' rejected */
    expect_resolve("C:\\dir", "C:/foo", (const char *) 0, false);         /* '/' in base rejected */
    expect_resolve("C:\\dir", "file.txt", "C:\\dir\\file.txt", true);      /* relative join */
    expect_resolve((const char *) 0, "C:\\foo\\.\\bar", "C:\\foo\\bar", true);
    expect_resolve((const char *) 0, "C:\\foo\\..\\bar", "C:\\bar", true);
    expect_resolve((const char *) 0, "C:\\..\\foo", "C:\\foo", true);       /* root '..' clamped */
    expect_resolve((const char *) 0, "D:\\test", "D:\\test", true);       /* multi drive */
    expect_resolve((const char *) 0, "C:\\foo\\", "C:\\foo", true);        /* trailing sep trimmed */
    expect_resolve((const char *) 0, "", "C:\\", true);                   /* empty input -> root */
    expect_resolve((const char *) 0, (const char *) 0, (const char *) 0, false); /* NULL input */

    /* small output buffer rejected */
    {
        char small[4];
        CHECK(path_resolve((const char *) 0, "C:\\x", small, sizeof(small)) == false);
    }
}

static void test_path_is_absolute(void)
{
    section("path_is_absolute");
    CHECK(path_is_absolute("C:\\foo") == true);
    CHECK(path_is_absolute("\\foo") == true);
    CHECK(path_is_absolute("foo") == false);
    CHECK(path_is_absolute((const char *) 0) == false);
}

/* ============================================================
 *  Mount table management
 * ============================================================ */
static void test_mount_table(void)
{
    section("mount table");

    CHECK(file_init() == true);
    CHECK(file_mount_count() == 0);

    CHECK(file_mount("C:\\", "fat32", 0) == true);
    CHECK(file_mount_count() == 1);

    CHECK(file_mount("C:\\", "fat32", 0) == false);   /* duplicate path */
    CHECK(file_mount_count() == 1);

    CHECK(file_mount("D:\\", "ntfs", 100) == true);
    CHECK(file_mount_count() == 2);

    {
        mount_point_t mi;
        CHECK(file_get_mount_info(0, &mi) == true);
        CHECK(strcmp(mi.path, "C:\\") == 0);
        CHECK(mi.fs_type == FS_TYPE_FAT32);
        CHECK(mi.partition == 0);

        CHECK(file_get_mount_info(1, &mi) == true);
        CHECK(strcmp(mi.path, "D:\\") == 0);
        CHECK(mi.fs_type == FS_TYPE_NTFS);
        CHECK(mi.partition == 100);

        CHECK(file_get_mount_info(2, &mi) == false);  /* out of range */
        CHECK(file_get_mount_info(0, (mount_point_t *) 0) == false);
    }

    CHECK(file_umount("D:\\") == true);
    CHECK(file_mount_count() == 1);
    CHECK(file_umount("D:\\") == false);           /* already gone */
    CHECK(file_umount("Z:\\") == false);          /* never mounted */
}

static void test_mount_table_full(void)
{
    section("mount table full (MAX_MOUNT_POINTS=8)");

    CHECK(file_init() == true);
    /* drives C..J => 8 mounts fill the table */
    {
        char drive;
        for (drive = 'C'; drive <= 'J'; drive++) {
            char mp[8];
            mp[0] = drive; mp[1] = ':'; mp[2] = '\\'; mp[3] = '\0';
            CHECK(file_mount(mp, "fat32", 0) == true);
        }
    }
    CHECK(file_mount_count() == MAX_MOUNT_POINTS);
    CHECK(file_mount("K:\\", "fat32", 0) == false);   /* 9th rejected */
    CHECK(file_mount_count() == MAX_MOUNT_POINTS);
}

/* ============================================================
 *  Routing: find_mount_point (static, reached via same-TU include)
 * ============================================================ */
static void test_routing(void)
{
    section("drive routing / longest-prefix match");

    CHECK(file_init() == true);
    CHECK(file_mount("C:\\", "fat32", 0) == true);
    CHECK(file_mount("D:\\", "ntfs", 0) == true);
    CHECK(file_mount("C:\\subdir", "ntfs", 0) == true);   /* nested mount point */

    /* locate indices by walking the mount table */
    int32_t idx_croot = -1, idx_d = -1, idx_sub = -1;
    {
        int32_t i;
        mount_point_t mi;
        for (i = 0; file_get_mount_info(i, &mi); i++) {
            if (strcmp(mi.path, "C:\\") == 0)       { idx_croot = i; }
            else if (strcmp(mi.path, "D:\\") == 0) { idx_d = i; }
            else if (strcmp(mi.path, "C:\\subdir") == 0) { idx_sub = i; }
        }
    }
    CHECK(idx_croot >= 0);
    CHECK(idx_d >= 0);
    CHECK(idx_sub >= 0);

    CHECK(find_mount_point("C:\\file.txt") == idx_croot);
    CHECK(find_mount_point("D:\\dir\\file.txt") == idx_d);
    CHECK(find_mount_point("E:\\file.txt") == -1);                 /* unmounted drive */
    CHECK(find_mount_point("C:\\subdir\\file") == idx_sub);       /* longest prefix wins */
    CHECK(find_mount_point("C:\\other\\file") == idx_croot);      /* not under subdir */
    CHECK(find_mount_point((const char *) 0) == -1);
    CHECK(find_mount_point("") == -1);
}

/* ============================================================
 *  Disk space
 * ============================================================ */
static void test_disk_space(void)
{
    section("disk space");

    CHECK(file_init() == true);
    disk_space_t sp;

    CHECK(file_disk_space("E:\\", &sp) == false);   /* nothing mounted */

    CHECK(file_mount("C:\\", "fat32", 0) == true);
    CHECK(file_disk_space("C:\\", &sp) == true);
    CHECK(sp.total_bytes == STUB_FAT32_TOTAL);
    CHECK(sp.free_bytes == STUB_FAT32_FREE);
    CHECK(sp.cluster_size == STUB_FAT32_CLUSTER);

    CHECK(file_disk_space("C:\\dir\\file.txt", &sp) == true);   /* under C: routes to C: */
    CHECK(sp.total_bytes == STUB_FAT32_TOTAL);

    CHECK(file_mount("D:\\", "ntfs", 0) == true);
    CHECK(file_disk_space("D:\\", &sp) == true);
    /* ntfs: total = total_sectors * bytes_per_sector, free=0 */
    CHECK(sp.total_bytes == (uint64_t) STUB_NT2_TOTAL_SECTORS * STUB_NT2_BYTES_PER_SECTOR);
    CHECK(sp.free_bytes == 0);
    CHECK(sp.cluster_size == STUB_NT2_CLUSTER);

    CHECK(file_disk_space("E:\\", &sp) == false);   /* still unmounted */
    CHECK(file_disk_space("C:\\", (disk_space_t *) 0) == false);
}

/* ============================================================
 *  Cache key construction (static, reached via same-TU include)
 * ============================================================ */
static void test_cache_key(void)
{
    section("cache key");

    CHECK(file_init() == true);
    char key[128];

    /* before any mount, g_current_mount_path is empty => failure */
    CHECK(file_build_cache_key("rel", key, sizeof(key)) == false);

    CHECK(file_mount("C:\\", "fat32", 0) == true);
    CHECK(file_build_cache_key("foo/bar", key, sizeof(key)) == true);
    /* key = "C:\" + 0x1F + "foo/bar" */
    CHECK(key[0] == 'C');
    CHECK(key[1] == ':');
    CHECK(key[2] == '\\');
    CHECK((uint8_t) key[3] == 0x1FU);
    CHECK(strcmp(key + 4, "foo/bar") == 0);

    /* buffer too small */
    {
        char small[6];
        CHECK(file_build_cache_key("some/long/path", small, sizeof(small)) == false);
    }

    /* a different mount produces a different key prefix */
    CHECK(file_init() == true);
    CHECK(file_mount("D:\\", "ntfs", 0) == true);
    CHECK(file_build_cache_key("rel", key, sizeof(key)) == true);
    CHECK(key[0] == 'D');
    CHECK((uint8_t) key[3] == 0x1FU);
    CHECK(strcmp(key + 4, "rel") == 0);
}

/* ============================================================
 *  Drive query API
 * ============================================================ */
static void test_drive_api(void)
{
    section("drive query API");

    CHECK(file_init() == true);
    CHECK(file_mount("C:\\", "fat32", 0) == true);
    CHECK(file_mount("D:\\", "ntfs", 0) == true);

    drive_info_t di;
    CHECK(file_get_drive_info('C', &di) == true);
    CHECK(di.mounted == true);
    CHECK(di.drive == 'C');
    CHECK(di.fs_type == FS_TYPE_FAT32);
    CHECK(di.total_bytes == STUB_FAT32_TOTAL);

    CHECK(file_get_drive_info('d', &di) == true);    /* lowercase accepted */
    CHECK(di.drive == 'D');
    CHECK(di.mounted == true);
    CHECK(di.fs_type == FS_TYPE_NTFS);

    CHECK(file_get_drive_info('Z', &di) == false);   /* not mounted */
    CHECK(file_get_drive_info('C', (drive_info_t *) 0) == false);

    CHECK(file_drive_mounted('C') == true);
    CHECK(file_drive_mounted('c') == true);
    CHECK(file_drive_mounted('D') == true);
    CHECK(file_drive_mounted('X') == false);

    {
        char drv[26];
        int32_t n = file_get_mounted_drives(drv, sizeof(drv));
        CHECK(n == 2);
        CHECK((drv[0] == 'C' && drv[1] == 'D') ||
              (drv[0] == 'D' && drv[1] == 'C'));

        CHECK(file_get_mounted_drives((char *) 0, 26) == 0);
        CHECK(file_get_mounted_drives(drv, 0) == 0);
    }
}

/* ============================================================
 *  Entry point
 * ============================================================ */
static unsigned denied_checks;
static bool deny_mutation(const char *path, bool write)
{
    (void)path;
    if (write) { denied_checks++; return false; }
    return true;
}
static unsigned rename_checks;
static bool deny_rename_target(const char *path, bool write)
{
    (void)path;
    if (write) { rename_checks++; return rename_checks == 1; }
    return true;
}
static void test_mutation_authorization(void)
{
    section("VFS mutation authorization");
    denied_checks = 0;
    file_uac_set_hook(deny_mutation);
    CHECK(!file_set_attr("C:\\protected.txt", 1, 0));
    CHECK(denied_checks == 1);
    rename_checks = 0;
    file_uac_set_hook(deny_rename_target);
    CHECK(!file_rename("C:\\source.txt", "C:\\protected.txt"));
    CHECK(rename_checks == 2);
    file_uac_set_hook(NULL);
}
void test_set_dirty_count(uint32_t count);
void test_set_ext_sync_ok(bool ok);
static void test_sync_failure(void)
{
    int32_t mounted = file_mount_count();
    test_set_dirty_count(1);
    CHECK(!file_sync_all());
    CHECK(!file_umount("C:\\"));
    CHECK(file_mount_count() == mounted);
    CHECK(!file_unmount_all());
    CHECK(file_mount_count() == mounted);
    test_set_dirty_count(0);
    test_set_ext_sync_ok(false);
    CHECK(!file_sync_all());
    CHECK(!file_unmount_all());
    CHECK(file_mount_count() == mounted);
    test_set_ext_sync_ok(true);
    CHECK(file_sync_all());
    CHECK(file_unmount_all());
    CHECK(file_mount_count() == 0);
}
int main(void)
{
    printf("Monios VFS host regression test\n");
    printf("MAX_MOUNT_POINTS=%d  PATH_MAX_LEN=%d\n", MAX_MOUNT_POINTS, PATH_MAX_LEN);

    test_path_resolve();
    test_path_is_absolute();
    test_mount_table();
    test_mount_table_full();
    test_routing();
    test_disk_space();
    test_cache_key();
    test_drive_api();
    test_mutation_authorization();
    test_sync_failure();

    printf("\n----------------------------------------\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("----------------------------------------\n");

    return g_fail == 0 ? 0 : 1;
}
