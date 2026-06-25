#ifndef _PCB_H_
#define _PCB_H_

#include "stdbool.h"
#include "stdint.h"

typedef enum {
    PCB_STATE_FREE = 0,
    PCB_STATE_READY,
    PCB_STATE_RUNNING,
    PCB_STATE_EXITED,
    PCB_STATE_ABORTED,
    PCB_STATE_STOPPED
} pcb_state_t;

typedef struct {
    bool used;
    uint32_t slot;
    uint32_t pid;
    char name[32];
    char path[128];
    pcb_state_t state;
    int32_t exit_code;
    uint64_t start_tick;
    uint64_t end_tick;
    uint32_t pending_signals;
} pcb_t;

void pcb_init(void);
int32_t pcb_process_start(const char *path);
void pcb_process_exit(int32_t pid, int32_t exit_code);
void pcb_process_abort(int32_t pid, int32_t exit_code);
void pcb_process_stop(int32_t pid);
int32_t pcb_current_pid(void);
uint32_t pcb_count(void);
uint32_t pcb_capacity(void);
bool pcb_snapshot(uint32_t index, pcb_t *out);
uint32_t pcb_pending_signal_mask(int32_t pid);
bool pcb_signal_or(int32_t pid, uint32_t mask);
bool pcb_signal_set(int32_t pid, uint32_t mask);
const char *pcb_status(void);
const char *pcb_state_name(pcb_state_t state);

#endif
