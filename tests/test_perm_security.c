int printf(const char *, ...);
#include "../kernel/syscall/fs_perm.c"
static pcb_t process;
pcb_t *pcb_get_current(void) { return &process; }
int32_t file_read(const char *p, void *b, uint32_t n)
{ (void)p; (void)b; (void)n; return -1; }
int32_t file_write(const char *p, const void *b, uint32_t n)
{ (void)p; (void)b; return (int32_t)n; }
bool file_exists(const char *p) { (void)p; return true; }
int main(void)
{
    char long_path[256]; uint32_t mode, uid, gid;
    fs_perm_init(); process.euid = 1000; process.egid = 1000;
    memset(long_path, 'a', sizeof(long_path)); long_path[255] = 0;
    if (fs_perm_set(long_path, 0600, 1000, 1000)) return 1;
    if (fs_perm_check(long_path, false, false)) return 2;
    if (fs_perm_set("file|0|0|0\n", 0600, 0, 0)) return 3;
    if (fs_perm_get(NULL, &mode, &uid, &gid)) return 4;
    if (sys_chmod(long_path, 0777) != -1) return 5;
    if (!fs_perm_set("private", 0600, 2000, 2000)) return 6;
    if (fs_perm_check("private", false, false)) return 7;
    if (fs_perm_check("c:\\private", false, false)) return 9;
    if (fs_perm_check("C:\\folder\\..\\private", false, false)) return 10;
    if (fs_perm_check("C:\\.\\private", false, false)) return 11;
    process.euid = 2000;
    if (!fs_perm_check("private", false, false)) return 8;
    if (!fs_perm_check("C:\\folder\\..\\private", false, false)) return 12;
    printf("Permission key rejection and owner checks: PASS\n");
    return 0;
}
