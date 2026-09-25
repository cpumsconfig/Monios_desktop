#ifndef _DRIVER_MANAGER_H_
#define _DRIVER_MANAGER_H_

#include "stdbool.h"
#include "stdint.h"
#include "driver_api.h"
#include "driver_status.h"

typedef bool (*driver_init_fn_t)(void);
typedef void (*driver_shutdown_fn_t)(void);
typedef int32_t (*driver_probe_fn_t)(void);
typedef void (*driver_update_fn_t)(uint64_t now_ticks);

typedef struct {
    const char *name;
    driver_init_fn_t init;
    driver_shutdown_fn_t shutdown;
    const char *depends_on;
    driver_probe_fn_t probe;
    driver_update_fn_t update;
    uint32_t priority;
    uint32_t update_interval_ticks;
    uint64_t next_update_tick;
    uint64_t last_update_tick;
    int32_t adapter_score;
    bool loaded;
    bool external;
    bool verified;
    bool critical;
    bool unloadable;
    bool adapter_ready;
    bool scheduled;
    void *image;
    uint32_t image_size;
    monios_driver_unload_fn_t unload;
    monios_driver_tick_fn_t tick;
} kernel_driver_t;

void driver_manager_init(void);
void driver_manager_shutdown(void);
bool driver_manager_load(const char *path);
bool driver_manager_unload(const char *name, bool force);
bool driver_manager_rebind(const char *name);
bool driver_manager_rescan(void);
void driver_manager_update(uint64_t now_ticks);
bool driver_manager_snapshot(driver_status_snapshot_t *snapshot);
uint32_t driver_manager_count(void);
const kernel_driver_t *driver_manager_at(uint32_t index);

#endif
