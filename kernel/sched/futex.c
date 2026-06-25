#include "futex.h"
#include "common.h"
#include "interrupt.h"

#define FUTEX_MAX_SLOTS 16U

typedef struct {
    bool used;
    uint64_t address;
    uint32_t expected;
    uint32_t waiters;
    uint64_t deadline;
} futex_slot_t;

static futex_slot_t g_slots[FUTEX_MAX_SLOTS];
static futex_info_t g_futex_info;
static char g_futex_status[64];

static futex_slot_t *futex_find_slot(uint64_t address)
{
    for (uint32_t index = 0; index < FUTEX_MAX_SLOTS; index++) {
        if (g_slots[index].used && g_slots[index].address == address) {
            return &g_slots[index];
        }
    }
    return NULL;
}

static futex_slot_t *futex_find_or_create_slot(uint64_t address)
{
    futex_slot_t *slot = futex_find_slot(address);

    if (slot != NULL) {
        return slot;
    }
    for (uint32_t index = 0; index < FUTEX_MAX_SLOTS; index++) {
        if (!g_slots[index].used) {
            g_slots[index].used = true;
            g_slots[index].address = address;
            g_futex_info.used_slots++;
            return &g_slots[index];
        }
    }
    return NULL;
}

void futex_init(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(&g_futex_info, 0, sizeof(g_futex_info));
    strcpy(g_futex_status, "futex: ready");
}

void futex_update(void)
{
    uint64_t now = timer_ticks();

    for (uint32_t index = 0; index < FUTEX_MAX_SLOTS; index++) {
        futex_slot_t *slot = &g_slots[index];

        if (!slot->used || slot->deadline == 0 || now < slot->deadline) {
            continue;
        }
        if (g_futex_info.total_waiters >= slot->waiters) {
            g_futex_info.total_waiters -= slot->waiters;
        } else {
            g_futex_info.total_waiters = 0;
        }
        slot->used = false;
        slot->waiters = 0;
        slot->deadline = 0;
        if (g_futex_info.used_slots > 0) {
            g_futex_info.used_slots--;
        }
        g_futex_info.timeout_reaps++;
        strcpy(g_futex_status, "futex: timeout reaped");
    }
}

int32_t futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks)
{
    futex_slot_t *slot;

    if (address == 0) {
        strcpy(g_futex_status, "futex: bad address");
        return -1;
    }
    slot = futex_find_or_create_slot(address);
    if (slot == NULL) {
        strcpy(g_futex_status, "futex: table full");
        return -1;
    }
    slot->expected = expected;
    slot->waiters++;
    slot->deadline = timeout_ticks == 0 ? 0 : timer_ticks() + timeout_ticks;
    g_futex_info.total_waiters++;
    strcpy(g_futex_status, "futex: waiter queued");
    return (int32_t) slot->waiters;
}

uint32_t futex_wake(uint64_t address, uint32_t count)
{
    futex_slot_t *slot = futex_find_slot(address);
    uint32_t wake_count;

    if (slot == NULL) {
        strcpy(g_futex_status, "futex: no waiters");
        return 0;
    }
    wake_count = count == 0 || count > slot->waiters ? slot->waiters : count;
    slot->waiters -= wake_count;
    if (g_futex_info.total_waiters >= wake_count) {
        g_futex_info.total_waiters -= wake_count;
    } else {
        g_futex_info.total_waiters = 0;
    }
    if (slot->waiters == 0) {
        slot->used = false;
        slot->deadline = 0;
        if (g_futex_info.used_slots > 0) {
            g_futex_info.used_slots--;
        }
    }
    g_futex_info.wake_ops += wake_count;
    strcpy(g_futex_status, "futex: wake complete");
    return wake_count;
}

uint32_t futex_waiter_count(uint64_t address)
{
    futex_slot_t *slot = futex_find_slot(address);

    return slot == NULL ? 0u : slot->waiters;
}

const futex_info_t *futex_info(void)
{
    return &g_futex_info;
}

const char *futex_status(void)
{
    return g_futex_status;
}
