#include "common.h"
#include "fs_perm.h"
#include "pcb.h"
#include "file.h"
#include "path.h"
#include "string.h"

/*
 * File system permissions.
 *
 * FAT32 has no native Unix permission bits, so we store them in a
 * kernel-side hash table keyed by canonical path.  Default is 0755
 * root:root for everything, which keeps all existing tests passing.
 */

#define FS_PERM_TABLE_SIZE  128
#define FS_PERM_DEFAULT_MODE  0755

/* On-disk permission database (one text record per line):
 *     path|mode_dec|uid_dec|gid_dec
 * e.g. C:\Monios\Users\test\secret.txt|384|1000|1000
 * '|' is used as the field separator because paths may contain '\'.
 *
 * The database is stored encrypted on disk to prevent a user from directly
 * editing perm.db to elevate privileges.  Format:
 *   [0..3]  magic "MPE1" (MoniOS Permission Encrypted v1)
 *   [4..7]  uint32 big-endian plaintext length
 *   [8..]   XOR-encrypted ciphertext (rotating 16-byte key)
 * Old plaintext databases (no magic) are detected on load and transparently
 * migrated to encrypted format. */
#define PERM_DB_PATH  "/Monios/System/perm.db"
#define PERM_DB_MAGIC  "MPE1"
#define PERM_DB_HEADER_SIZE 8U
#define PERM_DB_KEY_SIZE 16U

typedef struct {
    bool     used;
    uint32_t path_hash;
    char     path[128];
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
} fs_perm_entry_t;

static fs_perm_entry_t g_perm_table[FS_PERM_TABLE_SIZE];
static bool g_perm_initialised;

/* Set while fs_perm_save()/fs_perm_load() touch the disk via file_read /
 * file_write.  Those calls funnel back through fs_perm_check() inside
 * lib/file.c, so the check must be bypassed while we are doing the
 * load/save itself (otherwise we recurse and/or check a half-loaded table). */
static bool g_perm_saving;

void fs_perm_init(void)
{
    memset(g_perm_table, 0, sizeof(g_perm_table));
    fs_perm_load();
    g_perm_initialised = true;
}

static uint32_t fs_perm_djb2(const char *s)
{
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (uint8_t)*s++;
    return h;
}

static fs_perm_entry_t *fs_perm_find(const char *path)
{
    uint32_t h = fs_perm_djb2(path);
    for (uint32_t i = 0; i < FS_PERM_TABLE_SIZE; i++) {
        if (g_perm_table[i].used &&
            g_perm_table[i].path_hash == h &&
            strcmp(g_perm_table[i].path, path) == 0) {
            return &g_perm_table[i];
        }
    }
    return NULL;
}

/* A truncated permission key would never match the original path, silently
 * falling back to world-readable defaults. Reject it before publishing it. */
static bool fs_perm_valid_path(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;
    for (uint32_t i = 0; i < sizeof(g_perm_table[0].path); i++) {
        if (path[i] == '\0') return true;
        if (path[i] == '|' || path[i] == '\n' || path[i] == '\r') return false;
    }
    return false;
}

bool fs_perm_get(const char *path, uint32_t *mode_out, uint32_t *uid_out, uint32_t *gid_out)
{
    char canonical[128];
    if (!fs_perm_valid_path(path)) return false;
    if (!path_resolve(PATH_ROOT, path, canonical, sizeof(canonical))) return false;
    path = canonical;
    fs_perm_entry_t *e = fs_perm_find(path);
    if (e == NULL) {
        if (mode_out) *mode_out = FS_PERM_DEFAULT_MODE;
        if (uid_out)  *uid_out = 0;
        if (gid_out)  *gid_out = 0;
        return true;
    }
    if (mode_out) *mode_out = e->mode;
    if (uid_out)  *uid_out = e->uid;
    if (gid_out)  *gid_out = e->gid;
    return true;
}

bool fs_perm_set(const char *path, uint32_t mode, uint32_t uid, uint32_t gid)
{
    char canonical[128];
    if (!fs_perm_valid_path(path)) return false;
    if (!path_resolve(PATH_ROOT, path, canonical, sizeof(canonical))) return false;
    path = canonical;
    fs_perm_entry_t *e = fs_perm_find(path);
    if (e == NULL) {
        for (uint32_t i = 0; i < FS_PERM_TABLE_SIZE; i++) {
            if (!g_perm_table[i].used) {
                e = &g_perm_table[i];
                e->used = true;
                e->path_hash = fs_perm_djb2(path);
                strlcpy(e->path, path, sizeof(e->path));
                break;
            }
        }
        if (e == NULL) return false;
    }
    e->mode = mode & 07777;
    e->uid = uid;
    e->gid = gid;
    /* Persist to disk.  Skip while the loader is repopulating the table
     * from disk (g_perm_saving) to avoid rewriting the DB on every row. */
    if (!g_perm_saving) {
        fs_perm_save();
    }
    return true;
}

bool fs_perm_check(const char *path, bool want_write, bool want_exec)
{
    pcb_t *cur = pcb_get_current();
    uint32_t mode, uid, gid;
    uint32_t euid, egid;
    uint32_t shift;

    if (g_perm_saving) return true;  /* internal load/save path bypass */
    if (cur == NULL) return true;  /* kernel context always allowed */
    euid = cur->euid;
    egid = cur->egid;
    if (euid == 0) return true;    /* root always allowed */

    if (!fs_perm_get(path, &mode, &uid, &gid)) return false;

    if (uid == euid) {
        shift = 6;  /* owner */
    } else if (gid == egid) {
        shift = 3;  /* group */
    } else {
        shift = 0;  /* other */
    }

    if (want_write && !(mode & (FS_PERM_OTH_W << shift))) return false;
    if (want_exec  && !(mode & (FS_PERM_OTH_X << shift))) return false;
    /* Read is implied for existing files; check explicitly if needed. */
    if (!want_write && !want_exec && !(mode & (FS_PERM_OTH_R << shift))) return false;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Disk persistence: /Monios/System/perm.db (encrypted)             */
/* ------------------------------------------------------------------ */

/* Rotating XOR key — derived from a fixed seed plus the byte offset so
 * that repeating plaintext bytes do not produce repeating ciphertext.
 * This is not military-grade encryption, but it prevents casual editing
 * of perm.db to elevate privileges (the stated threat model). */
static const uint8_t g_perm_key[PERM_DB_KEY_SIZE] = {
    0x4D, 0x6F, 0x6E, 0x69, 0x4F, 0x53, 0x50, 0x65,
    0x72, 0x6D, 0x44, 0x42, 0x4B, 0x65, 0x79, 0x31
};

static void perm_crypt(uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++) {
        data[i] ^= g_perm_key[i % PERM_DB_KEY_SIZE];
        /* Additional offset-based scrambling so identical blocks at
         * different offsets produce different ciphertext. */
        data[i] ^= (uint8_t)((i >> 3) & 0xFF);
    }
}

static void perm_write_u32_be(uint8_t *buf, uint32_t value)
{
    buf[0] = (uint8_t)((value >> 24) & 0xFF);
    buf[1] = (uint8_t)((value >> 16) & 0xFF);
    buf[2] = (uint8_t)((value >> 8) & 0xFF);
    buf[3] = (uint8_t)(value & 0xFF);
}

static uint32_t perm_read_u32_be(const uint8_t *buf)
{
    return ((uint32_t) buf[0] << 24) |
           ((uint32_t) buf[1] << 16) |
           ((uint32_t) buf[2] << 8) |
           ((uint32_t) buf[3]);
}

/* Tiny bounded emitters (freestanding, no snprintf). */
static void perm_emit_char(char **pp, uint32_t *remain, char c)
{
    if (*remain > 0) {
        **pp = c;
        (*pp)++;
        (*remain)--;
    }
}

static void perm_emit_str(char **pp, uint32_t *remain, const char *s)
{
    while (*s != '\0') {
        perm_emit_char(pp, remain, *s);
        s++;
    }
}

static void perm_emit_uint(char **pp, uint32_t *remain, uint32_t v)
{
    char tmp[12];
    uint32_t n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v != 0 && n < sizeof(tmp)) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (n > 0) {
        n--;
        perm_emit_char(pp, remain, tmp[n]);
    }
}

/* Parse a non-terminated run of decimal digits, advancing *pp. */
static uint32_t perm_parse_uint(const char **pp)
{
    const char *p = *pp;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint32_t)(*p - '0');
        p++;
    }
    *pp = p;
    return v;
}

void fs_perm_save(void)
{
    static uint8_t buf[24576 + PERM_DB_HEADER_SIZE];
    uint8_t *plain_start;
    char *p;
    uint32_t remain;
    uint32_t plain_len;
    uint32_t i;

    g_perm_saving = true;
    /* Leave room for the 8-byte encrypted header at the front. */
    plain_start = &buf[PERM_DB_HEADER_SIZE];
    p = (char *) plain_start;
    remain = sizeof(buf) - PERM_DB_HEADER_SIZE;

    for (i = 0; i < FS_PERM_TABLE_SIZE; i++) {
        fs_perm_entry_t *e = &g_perm_table[i];
        if (!e->used) continue;
        perm_emit_str(&p, &remain, e->path);
        perm_emit_char(&p, &remain, '|');
        perm_emit_uint(&p, &remain, e->mode);
        perm_emit_char(&p, &remain, '|');
        perm_emit_uint(&p, &remain, e->uid);
        perm_emit_char(&p, &remain, '|');
        perm_emit_uint(&p, &remain, e->gid);
        perm_emit_char(&p, &remain, '\n');
    }
    plain_len = (uint32_t) ((uint8_t *) p - plain_start);

    /* Write header: magic + plaintext length (big-endian). */
    buf[0] = (uint8_t) PERM_DB_MAGIC[0];
    buf[1] = (uint8_t) PERM_DB_MAGIC[1];
    buf[2] = (uint8_t) PERM_DB_MAGIC[2];
    buf[3] = (uint8_t) PERM_DB_MAGIC[3];
    perm_write_u32_be(&buf[4], plain_len);

    /* Encrypt the plaintext in place. */
    perm_crypt(plain_start, plain_len);

    file_write(PERM_DB_PATH, buf, PERM_DB_HEADER_SIZE + plain_len);
    g_perm_saving = false;
}

/* Parse one "path|mode|uid|gid" line (newline already stripped) and
 * load it into the table. */
static void perm_load_line(const char *line)
{
    char path[128];
    uint32_t i = 0;
    const char *p = line;
    uint32_t mode, uid, gid;

    while (*p != '\0' && *p != '|' && i + 1 < sizeof(path)) {
        path[i++] = *p;
        p++;
    }
    path[i] = '\0';
    if (*p != '|' || path[0] == '\0') return;
    p++;
    mode = perm_parse_uint(&p);
    if (*p != '|') return;
    p++;
    uid = perm_parse_uint(&p);
    if (*p != '|') return;
    p++;
    gid = perm_parse_uint(&p);
    /* fs_perm_set() skips its auto-save while g_perm_saving is set. */
    fs_perm_set(path, mode, uid, gid);
}

void fs_perm_load(void)
{
    static uint8_t buf[20480 + PERM_DB_HEADER_SIZE];
    int32_t size;
    const char *p;
    bool encrypted = false;
    uint32_t plain_len = 0;
    uint32_t data_offset = 0;

    g_perm_saving = true;
    size = file_read(PERM_DB_PATH, buf, sizeof(buf) - 1U);
    if (size > 0) {
        /* Detect encrypted format: magic "MPE1" at offset 0. */
        if (size >= (int32_t) PERM_DB_HEADER_SIZE &&
            buf[0] == (uint8_t) PERM_DB_MAGIC[0] &&
            buf[1] == (uint8_t) PERM_DB_MAGIC[1] &&
            buf[2] == (uint8_t) PERM_DB_MAGIC[2] &&
            buf[3] == (uint8_t) PERM_DB_MAGIC[3]) {
            encrypted = true;
            plain_len = perm_read_u32_be(&buf[4]);
            data_offset = PERM_DB_HEADER_SIZE;
            /* Clamp plain_len to the actual data available. */
            if (plain_len > (uint32_t) (size - PERM_DB_HEADER_SIZE)) {
                plain_len = (uint32_t) (size - PERM_DB_HEADER_SIZE);
            }
            /* Decrypt in place. */
            perm_crypt(&buf[data_offset], plain_len);
        } else {
            /* Legacy plaintext database: load as-is, then re-save encrypted. */
            plain_len = (uint32_t) size;
            data_offset = 0;
        }

        buf[data_offset + plain_len] = '\0';
        p = (const char *) &buf[data_offset];
        while (*p != '\0') {
            char line[200];
            uint32_t n = 0;
            while (*p != '\0' && *p != '\n' && n + 1 < sizeof(line)) {
                line[n++] = *p;
                p++;
            }
            line[n] = '\0';
            if (*p == '\n') p++;
            /* Strip trailing '\r' from CRLF line endings. */
            if (n > 0 && line[n - 1] == '\r') {
                line[n - 1] = '\0';
            }
            if (line[0] != '\0') {
                perm_load_line(line);
            }
        }

        /* Migrate legacy plaintext database to encrypted format. */
        if (!encrypted) {
            fs_perm_save();
        }
    }
    /* file_read returns <= 0 when the DB does not exist yet: silently
     * keep the default 0755 root:root table. */
    g_perm_saving = false;
}

int32_t sys_chmod(const char *path, uint32_t mode)
{
    pcb_t *cur = pcb_get_current();
    uint32_t owner_uid;
    uint32_t dummy;

    if (cur == NULL) return -1;
    if (!file_exists(path)) return -1;
    if (!fs_perm_get(path, &dummy, &owner_uid, &dummy)) return -1;
    /* Only owner or root can chmod. */
    if (cur->euid != 0 && cur->euid != owner_uid) return -1;
    return fs_perm_set(path, mode, owner_uid, dummy) ? 0 : -1;
}

int32_t sys_chown(const char *path, uint32_t uid, uint32_t gid)
{
    pcb_t *cur = pcb_get_current();
    uint32_t mode, owner_uid, owner_gid;

    if (cur == NULL) return -1;
    if (!file_exists(path)) return -1;
    /* Only root can chown. */
    if (cur->euid != 0) return -1;
    if (!fs_perm_get(path, &mode, &owner_uid, &owner_gid)) return -1;
    if (uid == (uint32_t)-1) uid = owner_uid;
    if (gid == (uint32_t)-1) gid = owner_gid;
    return fs_perm_set(path, mode, uid, gid) ? 0 : -1;
}
