#ifndef _DRIVER_MANAGER_H_
#define _DRIVER_MANAGER_H_

#include "stdbool.h"
#include "stdint.h"

typedef bool (*driver_init_fn_t)(void);
typedef void (*driver_shutdown_fn_t)(void);

typedef struct {
    const char *name;
    driver_init_fn_t init;
    driver_shutdown_fn_t shutdown;
    bool loaded;
} kernel_driver_t;

void driver_manager_init(void);
void driver_manager_shutdown(void);
uint32_t driver_manager_count(void);
const kernel_driver_t *driver_manager_at(uint32_t index);

#endif
