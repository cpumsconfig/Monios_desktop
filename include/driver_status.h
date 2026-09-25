#ifndef _DRIVER_STATUS_H_
#define _DRIVER_STATUS_H_

#include "stdint.h"

#define DRIVER_STATUS_NAME_MAX 64U
#define DRIVER_STATUS_MAX 32U

#define DRIVER_STATUS_FLAG_EXTERNAL 0x00000001U
#define DRIVER_STATUS_FLAG_VERIFIED  0x00000002U
#define DRIVER_STATUS_FLAG_CRITICAL  0x00000004U
#define DRIVER_STATUS_FLAG_UNLOADABLE 0x00000008U
#define DRIVER_STATUS_FLAG_ADAPTER_READY 0x00000010U
#define DRIVER_STATUS_FLAG_SCHEDULED 0x00000020U
#define DRIVER_STATUS_FLAG_DEGRADED 0x00000040U

typedef struct {
    char name[DRIVER_STATUS_NAME_MAX];
    uint32_t flags;
    int32_t adapter_score;
    uint32_t priority;
    uint64_t last_update_tick;
    uint8_t loaded;
    uint8_t adapter_ready;
    uint8_t scheduled;
    uint8_t reserved[1];
} driver_status_entry_t;

typedef struct {
    uint32_t count;
    uint32_t loaded_count;
    driver_status_entry_t entries[DRIVER_STATUS_MAX];
} driver_status_snapshot_t;

#endif
