/*
 * stubs_ext.c — extra host-side (Windows / MinGW) stubs for the *new*
 * regression test suites: test_net.c (ip/tcp/dns), test_fs.c (gpt/fat32)
 * and test_gui.c (osui component registry).
 *
 * This file is deliberately self-contained: each new test .exe links
 * <test>.c + stubs_ext.c only (NOT stubs.c, which belongs to the legacy
 * VFS suite and would collide on the fat32_/ntfs_/gpt_ symbols that the
 * new tests pull in by #including the real drivers).
 *
 * Only what the included kernel sources need to link and run on the host
 * is provided. Hardware I/O, the fake network cable and the fake file
 * layer are all in-memory so the pure protocol / parsing / registry logic
 * can be exercised deterministically.
 */

#include "common.h"
#include "cpu.h"
#include "file.h"
#include "ipv4.h"
#include "kernel.h"
#include "net.h"
#include "osui.h"
#include "path.h"
#include "string.h"

/* ============================================================
 *  Monios libc / string routines (same ABI as stubs.c)
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
 *  x86 I/O port stubs (no hardware on host). The FAT32 / EXT / NTFS
 *  drivers issue ATA commands through these; on the host they are
 *  no-ops, so on-disk reads return whatever the caller zeroed.
 *  The unit tests drive the *pure* helpers (BPB validation, name
 *  formatting, superblock parsing, ...) directly instead.
 * ============================================================ */
void outb(uint16_t port, uint8_t value) { (void) port; (void) value; }
void outw(uint16_t port, uint16_t value) { (void) port; (void) value; }
void outl(uint16_t port, uint32_t value) { (void) port; (void) value; }
uint8_t inb(uint16_t port) { (void) port; return 0xFF; }
uint16_t inw(uint16_t port) { (void) port; return 0xFFFF; }
uint32_t inl(uint16_t port) { (void) port; return 0xFFFFFFFF; }
void io_wait(void) { }

/* ============================================================
 *  CPU / timer stubs — deterministic, monotonically increasing
 *  so tcp_generate_seq is reproducible across runs.
 * ============================================================ */
static uint64_t g_fake_tsc;

uint64_t cpu_read_tsc(void)
{
    g_fake_tsc += 1000000ULL;
    return g_fake_tsc;
}

uint64_t timer_ticks(void)
{
    return g_fake_tsc >> 10;
}

uint32_t timer_hz(void) { return 1000U; }

/* ============================================================
 *  Kernel log sink — discarded on host (tests assert on return
 *  values / state, not on log output).
 * ============================================================ */
void log_write(const char *str) { (void) str; }
void log_write_event(const char *tag, const char *detail) { (void) tag; (void) detail; }
void log_write_bool_event(const char *tag, bool enabled) { (void) tag; (void) enabled; }
void serial_write(const char *str) { (void) str; }

/* ============================================================
 *  Fake network "cable". net_send_ipv4_packet / net_udp_send_to
 *  capture every outgoing datagram so the TCP/DNS tests can assert
 *  on what the stack put on the wire.
 * ============================================================ */
static uint8_t g_local_ip[4] = { 192, 168, 1, 50 };
static uint8_t g_dns_server[4] = { 8, 8, 8, 8 };

#define FAKE_NET_MAX_PKT 6
static uint8_t  g_tx_buf[FAKE_NET_MAX_PKT][1600];
static uint16_t g_tx_len[FAKE_NET_MAX_PKT];
static uint32_t g_tx_count;

const uint8_t *net_local_ip(void)
{
    return g_local_ip;
}

bool net_get_dns_ip(uint8_t out[4])
{
    out[0] = g_dns_server[0];
    out[1] = g_dns_server[1];
    out[2] = g_dns_server[2];
    out[3] = g_dns_server[3];
    return true;
}

uint32_t net_dns_server_count(void) { return 1U; }

bool net_get_dns_server(uint32_t index, uint8_t out[4])
{
    if (index != 0U || out == (uint8_t *) 0) return false;
    out[0] = g_dns_server[0]; out[1] = g_dns_server[1];
    out[2] = g_dns_server[2]; out[3] = g_dns_server[3];
    return true;
}

bool net_send_ipv4_packet(const uint8_t dst_ip[4], uint8_t proto,
                          const uint8_t *payload, uint16_t payload_len)
{
    (void) dst_ip; (void) proto;
    /* Rolling ring buffer: never reject a send, so long connection
     * sequences (which emit many ACKs) keep working on the host. */
    uint32_t slot = g_tx_count % FAKE_NET_MAX_PKT;
    if (payload_len > sizeof(g_tx_buf[0])) {
        payload_len = (uint16_t) sizeof(g_tx_buf[0]);
    }
    for (uint32_t i = 0; i < payload_len; i++) {
        g_tx_buf[slot][i] = payload[i];
    }
    g_tx_len[slot] = payload_len;
    g_tx_count++;
    return true;
}

bool net_udp_send_to(const uint8_t dst_ip[4], uint16_t src_port,
                     uint16_t dst_port, const uint8_t *payload, uint16_t payload_len)
{
    (void) dst_ip; (void) src_port; (void) dst_port;
    return net_send_ipv4_packet(dst_ip, 17, payload, payload_len);
}

bool net_udp_send(const char *dst_ip_text, uint16_t dst_port,
                  const uint8_t *payload, uint16_t payload_len)
{
    (void) dst_ip_text; (void) dst_port;
    return net_send_ipv4_packet(g_local_ip, 17, payload, payload_len);
}

void net_update(void) { }

/* ipv4.c is just a thin wrapper over ip_parse_ipv4; provide self-contained
 * versions here so test_fs.exe (which does not pull in net/ip.c) still links. */
bool ipv4_parse(const char *text, uint8_t out[4])
{
    uint32_t part = 0;
    uint32_t index = 0;
    bool have_digit = false;
    if (text == (const char *) 0 || text[0] == '\0') {
        return false;
    }
    while (*text != '\0') {
        char ch = *text++;
        if (ch >= '0' && ch <= '9') {
            part = part * 10u + (uint32_t) (ch - '0');
            if (part > 255) { return false; }
            have_digit = true;
            continue;
        }
        if (ch == '.' && have_digit && index < 3) {
            out[index++] = (uint8_t) part;
            part = 0;
            have_digit = false;
            continue;
        }
        return false;
    }
    if (!have_digit || index != 3) { return false; }
    out[index] = (uint8_t) part;
    return true;
}
void ipv4_to_text(const uint8_t ip[4], char *out)
{
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t v = ip[i];
        char tmp[4];
        uint32_t n = 0;
        if (v == 0) { tmp[n++] = '0'; }
        else {
            char rev[4]; uint32_t r = 0;
            while (v > 0) { rev[r++] = (char) ('0' + v % 10); v /= 10; }
            while (r > 0) { tmp[n++] = rev[--r]; }
        }
        tmp[n] = '\0';
        for (uint32_t k = 0; k <= n; k++) { out[pos++] = tmp[k]; }
        if (i < 3) { out[pos - 1] = '.'; }
    }
}
bool ipv4_is_loopback(const uint8_t ip[4]) { return ip[0] == 127; }
bool ipv4_is_private(const uint8_t ip[4]) { return ip[0] == 10 || (ip[0] == 192 && ip[1] == 168); }
bool ipv4_is_link_local(const uint8_t ip[4]) { return ip[0] == 169 && ip[1] == 254; }
bool ipv4_is_multicast(const uint8_t ip[4]) { return (ip[0] & 0xF0) == 0xE0; }

/* ============================================================
 *  osui theme refresh stub (real one touches the renderer; host
 *  tests only exercise the component registry).
 * ============================================================ */
void osui_theme_init(void) { }
void osui_theme_refresh(void) { }

/* ============================================================
 *  Fake VFS used by osui component loading. The tests never need to
 *  enumerate real custom components: file_exists("C:\Monios\System")
 *  returns false so osui_init() skips the load-custom walk.
 * ============================================================ */
bool file_exists(const char *path) { (void) path; return false; }
bool file_mkdir(const char *path) { (void) path; return true; }
int32_t file_mount_partition_hint(void) { return -1; }
bool file_list_dir(const char *path, char *buffer, uint32_t buffer_size)
{ (void) path; (void) buffer; (void) buffer_size; return false; }
int32_t file_read(const char *path, void *buffer, uint32_t buffer_size)
{ (void) path; (void) buffer; (void) buffer_size; return -1; }
