/*
 * pkgmgr.c - MoniOS third-party package manager (feature 33).
 *
 * A user-mode application.  Packages are simple tar-style archives:
 * a manifest header followed by file entries.  Installed apps live under
 *   C:\Monios\Apps\<name>\
 * and are registered in
 *   C:\Monios\System\Config\packages.db
 * The repository base URL is read from
 *   C:\Monios\System\Config\pkgrepo.cfg
 *
 * Usage:
 *   pkgmgr list
 *   pkgmgr install <name>
 *   pkgmgr update  <name>
 *   pkgmgr remove  <name>
 *   pkgmgr query   <name>
 *
 * Build:
 *   x86_64-w64-mingw32-gcc user/apps/pkgmgr.c \
 *       -I user/lib -I include -o C:\Monios\System\pkgmgr.exe
 */
#include "appsys.h"
#include "console_dll.h"
#include <stdio.h>
#include <string.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

#define PKG_ROOT      "C:\\Monios\\Apps"
#define PKG_CFG_DIR   "C:\\Monios\\System\\Config"
#define PKG_REPO_CFG  "C:\\Monios\\System\\Config\\pkgrepo.cfg"
#define PKG_DB        "C:\\Monios\\System\\Config\\packages.db"
#define PKG_LINE_MAX  256

/* ── tiny string helpers (no snprintf in this libc) ─────────── */
static void catn(char *out, uint32_t cap, const char *a, const char *b,
                 const char *c, const char *d)
{
    uint32_t i = 0;
    const char *parts[4] = { a, b, c, d };
    for (int p = 0; p < 4; p++) {
        const char *s = parts[p];
        if (s == NULL) continue;
        while (*s && i + 1 < cap) out[i++] = *s++;
    }
    out[i] = '\0';
}

/* ── Helpers ─────────────────────────────────────────────────── */
static void read_repo_url(char *out, uint32_t cap)
{
    int n = app_file_read(PKG_REPO_CFG, out, cap - 1);
    if (n <= 0) { out[0] = '\0'; return; }
    out[n] = '\0';
    uint32_t i = 0; while (out[i] && out[i] != '\r' && out[i] != '\n') i++;
    out[i] = '\0';
}

static void build_app_dir(const char *name, char *out, uint32_t cap)
{
    catn(out, cap, PKG_ROOT, "\\", name, NULL);
}

/* ── list: dump packages.db ──────────────────────────────────── */
static int cmd_list(void)
{
    char db[8192];
    int n = app_file_read(PKG_DB, db, sizeof(db) - 1);
    if (n <= 0) { printf("(no packages installed)\n"); return 0; }
    db[n] = '\0';
    printf("%-16s %-12s %s\n", "NAME", "VERSION", "PATH");
    printf("---------------------------------------------\n");
    uint32_t i = 0;
    while (i < (uint32_t)n) {
        uint32_t start = i;
        while (i < (uint32_t)n && db[i] != '\n') i++;
        uint32_t len = i - start;
        if (len > 0 && db[start + len - 1] == '\r') len--;
        char line[PKG_LINE_MAX];
        uint32_t c = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, db + start, c); line[c] = '\0';
        for (uint32_t k = 0; k < c; k++) if (line[k] == '\t') line[k] = ' ';
        printf("%s\n", line);
        i++;
    }
    return 0;
}

/* ── install: fetch <repo>/<name>.pkg and unpack ─────────────── */
static int cmd_install(const char *name)
{
    char repo[128]; read_repo_url(repo, sizeof(repo));
    if (repo[0] == '\0') {
        printf("error: no repo configured (%s)\n", PKG_REPO_CFG);
        return 1;
    }

    char url[192]; catn(url, sizeof(url), repo, "/", name, ".pkg");
    printf("fetching %s ...\n", url);

    char pkgbuf[16384];
    int n = app_http_get_url(url, pkgbuf, sizeof(pkgbuf) - 1);
    if (n <= 0) { printf("install: download failed (%d)\n", n); return 1; }

    char dir[128]; build_app_dir(name, dir, sizeof(dir));
    if (!app_file_exists(dir)) app_file_mkdir(dir);

    char exec_path[160]; catn(exec_path, sizeof(exec_path), dir, "\\", name, ".exe");

    /* Manifest line first; payload follows. */
    uint32_t off = 0;
    while (off < (uint32_t)n && pkgbuf[off] != '\n') off++;
    off++;
    int w = app_file_write(exec_path, pkgbuf + off, (uint32_t)n - off);
    printf("wrote %d bytes -> %s\n", w, exec_path);

    /* Register in packages.db */
    char line[PKG_LINE_MAX];
    catn(line, sizeof(line), name, "\t1.0\t", dir, "\t2026-09-23\n");
    uint32_t llen = (uint32_t)strlen(line);
    char db[8192];
    int dn = app_file_read(PKG_DB, db, sizeof(db) - 1);
    if (dn < 0) dn = 0;
    memcpy(db + dn, line, llen);
    app_file_write(PKG_DB, db, (uint32_t)dn + llen);
    printf("installed %s\n", name);
    return 0;
}

/* ── remove: delete app dir + db entry ───────────────────────── */
static int cmd_remove(const char *name)
{
    char dir[128]; build_app_dir(name, dir, sizeof(dir));
    char exe[160]; catn(exe, sizeof(exe), dir, "\\", name, ".exe");
    app_file_delete(exe);
    app_file_rmdir(dir);

    char db[8192];
    int dn = app_file_read(PKG_DB, db, sizeof(db) - 1);
    if (dn <= 0) { printf("no packages.db\n"); return 0; }
    char out[8192]; uint32_t olen = 0;
    uint32_t i = 0;
    uint32_t nlen = (uint32_t)strlen(name);
    while (i < (uint32_t)dn) {
        uint32_t start = i;
        while (i < (uint32_t)dn && db[i] != '\n') i++;
        uint32_t len = i - start;
        int is_ours = (len >= nlen &&
                       memcmp(db + start, name, nlen) == 0 &&
                       (db[start + nlen] == '\t' || db[start + nlen] == ' '));
        if (!is_ours && olen + len + 1 < sizeof(out)) {
            memcpy(out + olen, db + start, len); olen += len;
            out[olen++] = '\n';
        }
        i++;
    }
    app_file_write(PKG_DB, out, olen);
    printf("removed %s\n", name);
    return 0;
}

/* ── query ────────────────────────────────────────────────────── */
static int cmd_query(const char *name)
{
    char db[8192];
    int dn = app_file_read(PKG_DB, db, sizeof(db) - 1);
    if (dn <= 0) { printf("not installed\n"); return 1; }
    uint32_t nlen = (uint32_t)strlen(name);
    uint32_t i = 0;
    while (i < (uint32_t)dn) {
        uint32_t start = i;
        while (i < (uint32_t)dn && db[i] != '\n') i++;
        if (i - start >= nlen && memcmp(db + start, name, nlen) == 0) {
            uint32_t len = i - start;
            char line[PKG_LINE_MAX];
            uint32_t c = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, db + start, c); line[c] = '\0';
            printf("%s\n", line);
            return 0;
        }
        i++;
    }
    printf("%s: not installed\n", name);
    return 1;
}

int main(int argc, char **argv)
{
    console_set_title("MoniOS Package Manager");
    if (argc < 2) {
        printf("usage: pkgmgr <list|install|update|remove|query> [name]\n");
        return 1;
    }
    const char *cmd = argv[1];
    const char *name = argc > 2 ? argv[2] : NULL;

    if (strcmp(cmd, "list") == 0)             return cmd_list();
    if (strcmp(cmd, "install") == 0 && name)  return cmd_install(name);
    if (strcmp(cmd, "update") == 0 && name) {
        cmd_remove(name);
        return cmd_install(name);
    }
    if (strcmp(cmd, "remove") == 0 && name)   return cmd_remove(name);
    if (strcmp(cmd, "query") == 0 && name)     return cmd_query(name);

    printf("pkgmgr: bad command or missing name\n");
    return 1;
}
