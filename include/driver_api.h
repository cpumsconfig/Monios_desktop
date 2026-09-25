#ifndef _DRIVER_API_H_
#define _DRIVER_API_H_

#include "stdbool.h"
#include "stdint.h"

#define MONIOS_DRIVER_ABI_VERSION 1U

#define MONIOS_DRIVER_FLAG_CRITICAL 0x00000001U
#define MONIOS_DRIVER_FLAG_PERIODIC 0x00000002U
#define MONIOS_DRIVER_FLAG_HOTPLUG  0x00000004U

typedef void (*monios_driver_log_fn_t)(const char *text);
typedef void *(*monios_driver_alloc_fn_t)(uint64_t size);
typedef void (*monios_driver_free_fn_t)(void *ptr);

typedef struct {
    uint32_t abi_version;
    uint32_t flags;
    const char *name;
    const char *path;
    monios_driver_log_fn_t log;
    monios_driver_alloc_fn_t alloc;
    monios_driver_free_fn_t free;
} monios_driver_runtime_t;

typedef bool (*monios_driver_entry_fn_t)(const monios_driver_runtime_t *runtime);
typedef void (*monios_driver_unload_fn_t)(void);
typedef void (*monios_driver_tick_fn_t)(uint64_t now_ticks);

#endif
