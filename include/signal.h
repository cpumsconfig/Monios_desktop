#ifndef _SIGNAL_H_
#define _SIGNAL_H_

#include "stdbool.h"
#include "stdint.h"

#define SIGNAL_CALL_SEND    1U
#define SIGNAL_CALL_PENDING 2U
#define SIGNAL_CALL_TAKE    3U
#define SIGNAL_CALL_CLEAR   4U

typedef struct {
    uint32_t sent_count;
    uint32_t fetch_count;
    uint32_t clear_count;
} signal_info_t;

typedef struct {
    int32_t pid;
    uint32_t signo;
    uint32_t mask;
} signal_request_t;

void signal_init(void);
bool signal_send(int32_t pid, uint8_t signo);
uint32_t signal_pending(int32_t pid);
uint32_t signal_take_pending(int32_t pid);
void signal_clear(int32_t pid);
const signal_info_t *signal_info(void);
const char *signal_status(void);

#endif
