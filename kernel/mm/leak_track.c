#include "common.h"
#include "kernel.h"
#include "leak_track.h"

/*
 * Static, heap-independent allocation tracker.
 *
 * Hash table: 1024 buckets; entries are carved from a fixed static pool so
 * the tracker never needs to call kmalloc() (which would recurse).  Lookups
 * key on the user-visible pointer returned by kmalloc().
 */

#define LEAK_BUCKETS   1024U
#define LEAK_POOL_SIZE 4096U
#define LEAK_END       0xFFFFFFFFU

typedef struct {
    uint8_t  used;
    uint64_t ptr;
    uint64_t size;
    uint64_t caller;
    uint64_t ticks;
    uint32_t next;
} leak_entry_t;

static leak_entry_t pool[LEAK_POOL_SIZE];
static uint32_t    buckets[LEAK_BUCKETS];
static uint64_t    live_blocks;
static uint64_t    live_bytes;
static uint64_t    record_total;
static uint64_t    unrecord_total;
static uint64_t    dropped;

static uint32_t leak_hash(uint64_t ptr)
{
    uint64_t h = (ptr >> 4) * 2654435761ULL;

    return (uint32_t)((h ^ (h >> 16)) % LEAK_BUCKETS);
}

void leak_track_init(void)
{
    for (uint32_t i = 0; i < LEAK_POOL_SIZE; i++) {
        pool[i].used = 0;
        pool[i].ptr = 0;
        pool[i].size = 0;
        pool[i].caller = 0;
        pool[i].ticks = 0;
        pool[i].next = LEAK_END;
    }
    for (uint32_t i = 0; i < LEAK_BUCKETS; i++) {
        buckets[i] = LEAK_END;
    }
    live_blocks = 0;
    live_bytes = 0;
    record_total = 0;
    unrecord_total = 0;
    dropped = 0;
}

void leak_track_record(void *ptr, uint64_t size, void *caller)
{
    uint32_t b;
    uint32_t i;

    if (ptr == NULL) {
        return;
    }

    /* Lazy first-run initialisation: the toolchain zero-inits BSS, but
     * buckets[] must explicitly know there are no chains yet. */
    if (record_total == 0 && live_blocks == 0 && buckets[0] == 0 &&
        buckets[LEAK_BUCKETS - 1] == 0) {
        leak_track_init();
    }

    b = leak_hash((uint64_t)(uintptr_t)ptr);

    /* Reject duplicate records for the same pointer. */
    for (i = buckets[b]; i != LEAK_END; i = pool[i].next) {
        if (pool[i].used && pool[i].ptr == (uint64_t)(uintptr_t)ptr) {
            pool[i].size = size;
            pool[i].caller = (uint64_t)(uintptr_t)caller;
            pool[i].ticks = timer_ticks();
            return;
        }
    }

    /* Find a free slot. */
    for (i = 0; i < LEAK_POOL_SIZE; i++) {
        if (!pool[i].used) {
            break;
        }
    }
    if (i >= LEAK_POOL_SIZE) {
        dropped++;
        return;
    }

    pool[i].used = 1;
    pool[i].ptr = (uint64_t)(uintptr_t)ptr;
    pool[i].size = size;
    pool[i].caller = (uint64_t)(uintptr_t)caller;
    pool[i].ticks = timer_ticks();
    pool[i].next = buckets[b];
    buckets[b] = i;

    live_blocks++;
    live_bytes += size;
    record_total++;
}

void leak_track_unrecord(void *ptr)
{
    uint32_t b;
    uint32_t i;
    uint32_t prev;

    if (ptr == NULL) {
        return;
    }

    b = leak_hash((uint64_t)(uintptr_t)ptr);
    prev = LEAK_END;
    for (i = buckets[b]; i != LEAK_END; prev = i, i = pool[i].next) {
        if (pool[i].used && pool[i].ptr == (uint64_t)(uintptr_t)ptr) {
            if (prev == LEAK_END) {
                buckets[b] = pool[i].next;
            } else {
                pool[prev].next = pool[i].next;
            }
            live_blocks--;
            if (live_bytes >= pool[i].size) {
                live_bytes -= pool[i].size;
            } else {
                live_bytes = 0;
            }
            unrecord_total++;
            pool[i].used = 0;
            pool[i].ptr = 0;
            return;
        }
    }
}

/* ── serial output helpers ─────────────────────────────────── */
static void print_hex(uint64_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[17];

    for (int8_t i = 15; i >= 0; i--) {
        buf[15 - i] = hex[(v >> (i * 4)) & 0xFU];
    }
    buf[16] = '\0';
    serial_write(buf);
}

static void print_dec(uint64_t v)
{
    char tmp[24];
    uint32_t n = 0;

    if (v == 0) {
        serial_write("0");
        return;
    }
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10U));
        v /= 10U;
    }
    while (n > 0) {
        char c[2] = { tmp[--n], '\0' };
        serial_write(c);
    }
}

void leak_track_report(void)
{
    uint64_t now = timer_ticks();
    uint64_t count = 0;

    serial_write("\r\n===== KERNEL HEAP LEAK REPORT =====\r\n");
    serial_write("live_blocks=");     print_dec(live_blocks);
    serial_write(" live_bytes=");    print_dec(live_bytes);
    serial_write(" recorded_total="); print_dec(record_total);
    serial_write(" freed_total=");   print_dec(unrecord_total);
    serial_write(" dropped=");       print_dec(dropped);
    serial_write("\r\n");

    for (uint32_t b = 0; b < LEAK_BUCKETS; b++) {
        for (uint32_t i = buckets[b]; i != LEAK_END; i = pool[i].next) {
            uint64_t age;

            if (!pool[i].used) {
                continue;
            }
            count++;
            age = (now >= pool[i].ticks) ? (now - pool[i].ticks) : 0;

            serial_write("#");
            print_dec(count);
            serial_write(" ptr=0x");
            print_hex(pool[i].ptr);
            serial_write(" size=");
            print_dec(pool[i].size);
            serial_write(" caller=0x");
            print_hex(pool[i].caller);
            serial_write(" age_ticks=");
            print_dec(age);
            serial_write("\r\n");
        }
    }
    serial_write("===== END LEAK REPORT =====\r\n");
}

void leak_track_summary(uint64_t *out_blocks, uint64_t *out_bytes)
{
    if (out_blocks != NULL) {
        *out_blocks = live_blocks;
    }
    if (out_bytes != NULL) {
        *out_bytes = live_bytes;
    }
}
