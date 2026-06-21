#ifndef _FUTEX_H_
#define _FUTEX_H_

#include "stdint.h"

#define FUTEX_CALL_WAIT  1U
#define FUTEX_CALL_WAKE  2U
#define FUTEX_CALL_COUNT 3U

typedef struct {
    uint32_t total_waiters;
    uint32_t wake_ops;
    uint32_t timeout_reaps;
    uint32_t used_slots;
} futex_info_t;

typedef struct {
    uint64_t address;
    uint32_t expected;
    uint32_t timeout_ticks;
    int32_t result;
} futex_wait_request_t;

typedef struct {
    uint64_t address;
    uint32_t count;
    uint32_t result;
} futex_wake_request_t;

void futex_init(void);
void futex_update(void);
int32_t futex_wait(uint64_t address, uint32_t expected, uint32_t timeout_ticks);
uint32_t futex_wake(uint64_t address, uint32_t count);
uint32_t futex_waiter_count(uint64_t address);
const futex_info_t *futex_info(void);
const char *futex_status(void);

#endif
