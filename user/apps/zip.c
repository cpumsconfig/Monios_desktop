#include "stdio.h"
#include "stddef.h"
#include "string.h"
#include "appsys.h"
#include "zip.h"
#include "crc32.h"

/*
 * zip.exe - Monios ZIP archive tool (Task 14).
 *
 *   zip -c archive.zip file1 file2 dir1/...   create archive (stored)
 *   zip -x archive.zip [output_dir]           extract archive
 *   zip -l archive.zip                       list contents
 *   zip -v ...                               verbose (per-file details)
 *
 * Backed by lib/zip.c (pure C, no malloc) which supports ZIP reading of
 * store (method 0) and DEFLATE (method 8) archives, and writing of store
 * archives.  The Monios application layer exposes whole-file I/O
 * (app_file_read / app_file_write), so everything is staged in static
 * buffers below.
 */

#define ZIP_ARCHIVE_BUF   (2u * 1024u * 1024u)
#define ZIP_STAGE_BUF     (256u * 1024u)

static uint8_t g_archive[ZIP_ARCHIVE_BUF];  /* archive image (read or build) */
static uint8_t g_out[ZIP_ARCHIVE_BUF];      /* built archive */
static uint8_t g_stage[ZIP_STAGE_BUF];      /* per-file staging buffer */
static char    g_list[4096];                /* directory listing buffer */

static int g_verbose;

/* mingw emits a stack-probe call; all buffers are static so it is unused. */
void ___chkstk_ms(void) { }

/* ---------------- small helpers ---------------- */

static void puts_s(const char *text)
{
    fputs(text);
    fputs("\r\n");
}

static void usage(void)
{
    puts_s("zip - Monios archive tool");
    puts_s("usage:");
    puts_s("  zip -c <archive.zip> <file|dir>...   create archive");
    puts_s("  zip -x <archive.zip> [output_dir]    extract archive");
    puts_s("  zip -l <archive.zip>                 list contents");
    puts_s("  zip -v ...                           verbose mode");
}

/* basename of a Monios path (backslash or forward slash) */
static const char *path_base(const char *path)
{
    const char *base = path;
    const char *p = path;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
        p++;
    }
    return base;
}

/* ensure every component of a Monios path exists as a directory */
static void mkdir_p(const char *path)
{
    char tmp[PATH_MAX_LEN];
    uint32_t i = 0;
    uint32_t n = (uint32_t) strlen(path);

    if (n >= sizeof(tmp)) {
        return;
    }
    memcpy(tmp, path, n + 1);

    /* skip drive prefix "X:\" */
    if (n >= 3 && tmp[1] == ':' && (tmp[2] == '\\' || tmp[2] == '/')) {
        i = 3;
    }
    for (; i < n; i++) {
        if (tmp[i] == '\\' || tmp[i] == '/') {
            char save = tmp[i];
            tmp[i] = '\0';
            if (tmp[0] && !(tmp[0] >= 'A' && tmp[0] <= 'Z' && tmp[1] == ':')) {
                app_file_mkdir(tmp);
            } else if (i >= 3) {
                app_file_mkdir(tmp);
            }
            tmp[i] = save;
        }
    }
    app_file_mkdir(tmp);
}

/* convert a ZIP name ('/'-separated) + output dir into a Monios path */
static void to_monios_path(char *dst, uint32_t cap,
                           const char *outdir, const char *zipname)
{
    uint32_t i = 0;
    while (*outdir && i + 1 < cap) {
        dst[i++] = *outdir++;
    }
    if (i > 0 && dst[i - 1] != '\\' && dst[i - 1] != '/') {
        if (i + 1 < cap) {
            dst[i++] = '\\';
        }
    }
    while (*zipname && i + 1 < cap) {
        char c = *zipname++;
        if (c == '/') {
            c = '\\';
        }
        dst[i++] = c;
    }
    dst[i] = '\0';
}

/* ---------------- create ---------------- */

static int add_one_file(zip_writer_t *w, const char *fs_path, const char *arc_name)
{
    int sz = app_file_size(fs_path);
    if (sz <= 0) {
        fputs("  skip (empty or missing): ");
        fputs(fs_path);
        fputs("\r\n");
        return 0;
    }
    if ((uint32_t) sz > sizeof(g_stage)) {
        fputs("  skip (too large for stage buffer): ");
        fputs(fs_path);
        fputs("\r\n");
        return 0;
    }
    if (app_file_read(fs_path, g_stage, (uint32_t) sz) != sz) {
        fputs("  read failed: ");
        fputs(fs_path);
        fputs("\r\n");
        return -1;
    }
    if (zip_writer_add_file(w, arc_name, g_stage, (uint32_t) sz) < 0) {
        puts_s("  archive buffer full");
        return -1;
    }
    if (g_verbose) {
        fputs("  + ");
        fputs(arc_name);
        fputs(" (");
        print_uint((uint32_t) sz);
        fputs(" bytes)\r\n");
    }
    return 0;
}

static int add_tree(zip_writer_t *w, const char *fs_path, const char *arc_prefix)
{
    char child_fs[PATH_MAX_LEN];
    char child_arc[PATH_MAX_LEN];
    uint32_t start = 0;
    uint32_t pl;

    if (app_file_list_dir(fs_path, g_list, sizeof(g_list)) <= 0) {
        return 0;
    }

    pl = (uint32_t) strlen(fs_path);
    for (uint32_t i = 0; g_list[i] != '\0'; i++) {
        if (g_list[i] != '\n') {
            continue;
        }
        uint32_t len = i - start;
        const char *name = g_list + start;
        uint32_t j = 0;

        start = i + 1;
        if (len == 0) {
            continue;
        }
        if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) {
            continue;
        }

        /* build fs child path */
        memcpy(child_fs, fs_path, pl);
        child_fs[pl] = '\0';
        if (pl > 0 && child_fs[pl - 1] != '\\' && child_fs[pl - 1] != '/') {
            child_fs[pl++] = '\\';
            child_fs[pl] = '\0';
        }
        while (j < len && pl + j + 1 < sizeof(child_fs)) {
            child_fs[pl + j] = name[j];
            j++;
        }
        child_fs[pl + j] = '\0';

        /* build archive name: arc_prefix + '/' + name */
        j = 0;
        while (arc_prefix[j] && j + 1 < sizeof(child_arc)) {
            child_arc[j] = arc_prefix[j];
            j++;
        }
        if (j > 0 && child_arc[j - 1] != '/') {
            child_arc[j++] = '/';
            child_arc[j] = '\0';
        }
        for (uint32_t k = 0; k < len && j + 1 < sizeof(child_arc); k++) {
            child_arc[j++] = name[k];
        }
        child_arc[j] = '\0';

        if (app_file_is_dir(child_fs)) {
            char dir_name[PATH_MAX_LEN];
            uint32_t d = 0;
            while (child_arc[d] && d + 2 < sizeof(dir_name)) {
                dir_name[d] = child_arc[d];
                d++;
            }
            dir_name[d++] = '/';
            dir_name[d] = '\0';
            zip_writer_add_file(w, dir_name, NULL, 0);
            add_tree(w, child_fs, child_arc);
        } else {
            add_one_file(w, child_fs, child_arc);
        }
    }
    return 0;
}

static int cmd_create(const char *archive, char **inputs, uint32_t n_inputs)
{
    zip_writer_t w;
    int total;

    zip_writer_init(&w, g_out, sizeof(g_out));
    puts_s("creating archive:");

    for (uint32_t i = 0; i < n_inputs; i++) {
        const char *path = inputs[i];
        const char *base = path_base(path);

        if (app_file_is_dir(path)) {
            char dir_name[PATH_MAX_LEN];
            uint32_t d = 0;
            while (base[d] && d + 2 < sizeof(dir_name)) {
                dir_name[d] = base[d];
                d++;
            }
            dir_name[d++] = '/';
            dir_name[d] = '\0';
            zip_writer_add_file(&w, dir_name, NULL, 0);
            add_tree(&w, path, base);
        } else if (app_file_exists(path)) {
            add_one_file(&w, path, base);
        } else {
            fputs("  not found: ");
            fputs(path);
            fputs("\r\n");
        }
    }

    total = zip_writer_finish(&w);
    if (total < 0) {
        puts_s("failed to finalize archive");
        return 1;
    }
    if (app_file_write(archive, g_out, (uint32_t) total) != (int32_t) total) {
        fputs("failed to write archive: ");
        fputs(archive);
        fputs("\r\n");
        return 1;
    }
    fputs("wrote ");
    fputs(archive);
    fputs(" with ");
    print_uint(w.count);
    fputs(" entries (");
    print_uint((uint32_t) total);
    fputs(" bytes)\r\n");
    return 0;
}

/* ---------------- list / extract ---------------- */

static int cmd_list(const char *archive)
{
    zip_reader_t z;
    int n;

    n = app_file_read(archive, g_archive, sizeof(g_archive));
    if (n <= 0) {
        fputs("cannot read archive: ");
        fputs(archive);
        fputs("\r\n");
        return 1;
    }
    if (zip_reader_open(&z, g_archive, (uint32_t) n) < 0) {
        fputs("not a valid zip: ");
        fputs(archive);
        fputs("\r\n");
        return 1;
    }

    fputs("archive: "); fputs(archive); fputs("\r\n");
    puts_s("  name                        length     packed     method  crc");
    for (uint32_t i = 0; i < z.count; i++) {
        const zip_entry_t *e = &z.entries[i];
        fputs("  ");
        fputs(e->name);
        fputs("   ");
        print_uint(e->size_uncompressed);
        fputs("   ");
        print_uint(e->size_compressed);
        fputs("   ");
        if (e->method == ZIP_METHOD_STORE) {
            fputs("store ");
        } else {
            fputs("defl8");
        }
        fputs("   ");
        {
            char hex[9];
            static const char hc[] = "0123456789ABCDEF";
            uint32_t c = e->crc32;
            for (int k = 7; k >= 0; k--) {
                hex[k] = hc[c & 0xFu];
                c >>= 4;
            }
            hex[8] = '\0';
            fputs(hex);
        }
        fputs("\r\n");
    }
    fputs("total entries: ");
    print_uint(z.count);
    fputs("\r\n");
    return 0;
}

static int cmd_extract(const char *archive, const char *outdir)
{
    zip_reader_t z;
    int n;

    n = app_file_read(archive, g_archive, sizeof(g_archive));
    if (n <= 0) {
        fputs("cannot read archive: ");
        fputs(archive);
        fputs("\r\n");
        return 1;
    }
    if (zip_reader_open(&z, g_archive, (uint32_t) n) < 0) {
        fputs("not a valid zip: ");
        fputs(archive);
        fputs("\r\n");
        return 1;
    }

    if (outdir == NULL || outdir[0] == '\0') {
        outdir = ".";
    }
    mkdir_p(outdir);
    fputs("extracting to: ");
    fputs(outdir);
    fputs("\r\n");

    for (uint32_t i = 0; i < z.count; i++) {
        const zip_entry_t *e = &z.entries[i];
        char path[PATH_MAX_LEN];
        int got;

        to_monios_path(path, sizeof(path), outdir, e->name);

        if (e->is_directory) {
            app_file_mkdir(path);
            if (g_verbose) {
                fputs("  mkdir "); fputs(path); fputs("\r\n");
            }
            continue;
        }

        if (e->size_uncompressed > sizeof(g_stage)) {
            fputs("  skip (too large): ");
            fputs(e->name);
            fputs("\r\n");
            continue;
        }

        got = zip_reader_extract(&z, i, g_stage, sizeof(g_stage));
        if (got < 0) {
            fputs("  FAILED: ");
            fputs(e->name);
            fputs("\r\n");
            continue;
        }

        /* make sure parent dir exists */
        {
            char parent[PATH_MAX_LEN];
            uint32_t pl = (uint32_t) strlen(path);
            while (pl > 0 && path[pl - 1] != '\\' && path[pl - 1] != '/') {
                pl--;
            }
            if (pl > 0) {
                memcpy(parent, path, pl);
                parent[pl] = '\0';
                mkdir_p(parent);
            }
        }

        if (app_file_write(path, g_stage, (uint32_t) got) != got) {
            fputs("  write failed: ");
            fputs(path);
            fputs("\r\n");
            continue;
        }
        fputs("  + ");
        fputs(e->name);
        if (g_verbose) {
            fputs(" (");
            print_uint((uint32_t) got);
            fputs(" bytes, crc ok)");
        }
        fputs("\r\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd = NULL;
    const char *archive = NULL;
    const char *outdir = NULL;
    char *inputs[32];
    uint32_t n_inputs = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') {
            if (cmd == NULL) {
                /* first non-option should not happen if a command was given;
                 * treat it as archive */
                archive = a;
            } else if (cmd[1] == 'x') {
                outdir = a;
            } else if (cmd[1] == 'c') {
                if (n_inputs < 32) {
                    inputs[n_inputs++] = (char *) a;
                }
            }
            continue;
        }
        if (a[1] == 'v') {
            g_verbose = 1;
            continue;
        }
        cmd = a;
    }

    if (cmd == NULL || archive == NULL) {
        usage();
        return 1;
    }

    if (cmd[1] == 'l') {
        return cmd_list(archive);
    }
    if (cmd[1] == 'x') {
        return cmd_extract(archive, outdir);
    }
    if (cmd[1] == 'c') {
        if (n_inputs == 0) {
            usage();
            return 1;
        }
        return cmd_create(archive, inputs, n_inputs);
    }

    usage();
    return 1;
}
