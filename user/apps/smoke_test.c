/*
 * smoke_test.c - Monios user-space persistence smoke test.
 *
 * This program is the in-kernel counterpart to tests/persistence_test.py.
 * The Monios serial port is occupied by the GDB stub and does not currently
 * accept an interactive shell, so file persistence cannot be driven by typing
 * `echo ... > C:\...` over serial from the host. Instead this app, when launched
 * by the shell (e.g. placed on the boot disk and started at the end of boot),
 * exercises the file system end-to-end:
 *
 *   1. write  C:\smoke_test.txt  with a known magic payload
 *   2. read it back and verify the bytes match
 *   3. print SMOKE_PERSIST_RESULT=PASS / =FAIL to the console (and app log)
 *
 * persistence_test.py boots the VM twice; the second boot's serial log is
 * expected to contain SMOKE_PERSIST_RESULT=PASS, proving the file survived
 * a power cycle.
 *
 * Build integration (optional, requires Makefile wiring not done here):
 *   add this app to the user-app image and autolaunch it, or run it from the
 *   shell once C: is mounted.
 */

#include "appsys.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"

#define SMOKE_PATH   "C:\\smoke_test.txt"
#define SMOKE_MAGIC  "MONIOS_SMOKE_OK\n"

int main(int argc, char **argv)
{
    const char *payload = SMOKE_MAGIC;
    uint32_t want = (uint32_t) (sizeof(SMOKE_MAGIC) - 1);
    char buf[128];
    int32_t n;

    (void) argc;
    (void) argv;

    printf("smoke_test: writing %s\r\n", SMOKE_PATH);

    n = app_file_write(SMOKE_PATH, payload, want);
    if (n != (int32_t) want) {
        printf("smoke_test: write failed (n=%d, want=%u)\r\n", (int) n, want);
        printf("SMOKE_PERSIST_RESULT=FAIL\r\n");
        app_log("smoke_test: write failed");
        return 1;
    }

    if (!app_file_exists(SMOKE_PATH)) {
        printf("smoke_test: file missing after write\r\n");
        printf("SMOKE_PERSIST_RESULT=FAIL\r\n");
        return 1;
    }

    memset(buf, 0, sizeof(buf));
    n = app_file_read(SMOKE_PATH, buf, sizeof(buf) - 1);
    if (n != (int32_t) want || memcmp(buf, payload, want) != 0) {
        printf("smoke_test: verify failed (n=%d)\r\n", (int) n);
        printf("SMOKE_PERSIST_RESULT=FAIL\r\n");
        app_log("smoke_test: content mismatch");
        return 1;
    }

    printf("smoke_test: content verified (%d bytes)\r\n", (int) n);
    printf("SMOKE_PERSIST_RESULT=PASS\r\n");
    app_log("smoke_test: persistence OK");
    return 0;
}
