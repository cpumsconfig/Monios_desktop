#ifndef _FS_PERM_H_
#define _FS_PERM_H_

#include "stdbool.h"
#include "stdint.h"

/* Unix-style permission bits. */
#define FS_PERM_OTH_X  0001
#define FS_PERM_OTH_W  0002
#define FS_PERM_OTH_R  0004
#define FS_PERM_GRP_X  0010
#define FS_PERM_GRP_W  0020
#define FS_PERM_GRP_R  0040
#define FS_PERM_USR_X  0100
#define FS_PERM_USR_W  0200
#define FS_PERM_USR_R  0400
#define FS_PERM_ALL    0777

/* File permission syscall handlers. */
int32_t sys_chmod(const char *path, uint32_t mode);
int32_t sys_chown(const char *path, uint32_t uid, uint32_t gid);

/* Permission checking for file operations.
 *   fs_perm_check(path, want_write, want_exec) -> true if current process
 *   has the requested permission.  Root (euid==0) always passes. */
bool fs_perm_check(const char *path, bool want_write, bool want_exec);

/* Initialise the file-permission subsystem (loads /etc/passwd-equivalent). */
void fs_perm_init(void);

/* Persist the in-memory permission table to /Monios/System/perm.db and
 * reload it on boot.  Save is invoked automatically after fs_perm_set(). */
void fs_perm_save(void);
void fs_perm_load(void);

/* Get/set the stored mode/owner for a path.  Stored in a kernel-side
 * hash table keyed by path (FAT32 has no native Unix permissions). */
bool fs_perm_get(const char *path, uint32_t *mode_out, uint32_t *uid_out, uint32_t *gid_out);
bool fs_perm_set(const char *path, uint32_t mode, uint32_t uid, uint32_t gid);

#endif /* _FS_PERM_H_ */
