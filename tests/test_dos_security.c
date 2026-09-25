int printf(const char *, ...);
#include "../kernel/compat/dos.c"
static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static char captured[4];
void serial_write(const char *s) { CHECK(strlen(s) == 1); captured[0] = s[0]; }
void log_write(const char *s) { (void)s; }
int main(void)
{
    int32_t code;
    uint8_t com[] = {0xB8, 0x07, 0x4C, 0xCD, 0x21};
    uint8_t mz[80];
    dos_mz_header_t *h = (dos_mz_header_t *)mz;
    CHECK(DOS_RAM_BYTES == 640U * 1024U);
    CHECK(dos_exec_image(com, sizeof(com), 1, &code) == 7 && code == 7);
    CHECK(dos_exec_image(com, 0xFF01u, 1, &code) == -1 && code == -1);
    CHECK(dos_exec_image(com, sizeof(com), 0, &code) == -1);
    memset(mz, 0, sizeof(mz));
    h->e_magic = DOS_MZ_MAGIC; h->e_cparhdr = 4;
    mz[64] = 0xF4;
    CHECK(dos_exec_image(mz, sizeof(mz), 0, &code) == 0);
    h->e_cparhdr = 0xFFFF;
    CHECK(dos_exec_image(mz, sizeof(mz), 0, &code) == -1);
    h->e_cparhdr = 4; h->e_crlc = 1; h->e_lfarlc = 63;
    CHECK(dos_exec_image(mz, sizeof(mz), 0, &code) == -1);
    h->e_crlc = 0; h->e_lfarlc = 0; h->e_cs = 0xFFFF;
    CHECK(dos_exec_image(mz, sizeof(mz), 0, &code) == -1);
    g_terminated = 0; g_exit_code = 0;
    CHECK(dos_rw(DOS_RAM_BYTES - 1) == 0 && g_terminated && g_exit_code == -1);
    g_terminated = 0; g_exit_code = 0;
    dos_ww(DOS_RAM_BYTES, 123);
    CHECK(g_terminated && g_exit_code == -1);
    dos_console_putc('X'); CHECK(captured[0] == 'X');
    printf("DOS security: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
