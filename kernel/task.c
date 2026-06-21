#include "common.h"
#include "interrupt.h"
#include "scheduler.h"
#include "task.h"
#include "tcb.h"

#define MAX_TASKS 8

static tcb_t tasks[MAX_TASKS];
static bool g_task_scheduler_stopping;

void task_init(void)
{
    memset(tasks, 0, sizeof(tasks));
    g_task_scheduler_stopping = false;
}

int task_create(const char *name, task_fn_t fn, void *arg, uint32_t period_ticks, bool run_immediately)
{
    uint64_t now = timer_ticks();

    if (g_task_scheduler_stopping) {
        return -1;
    }

    for (int i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].used) {
            tasks[i].used = true;
            tasks[i].id = (uint32_t) i;
            tasks[i].name = name;
            tasks[i].entry = fn;
            tasks[i].arg = arg;
            tasks[i].period_ticks = period_ticks == 0 ? 1 : period_ticks;
            tasks[i].next_tick = run_immediately ? now : now + tasks[i].period_ticks;
            tasks[i].last_run_tick = 0;
            tasks[i].run_count = 0;
            scheduler_register_task(tasks[i].id, tasks[i].period_ticks);
            return i;
        }
    }
    return -1;
}

void task_run_ready(void)
{
    uint64_t now = timer_ticks();

    if (g_task_scheduler_stopping) {
        return;
    }

    for (int i = 0; i < MAX_TASKS; i++) {
        if (!tasks[i].used || tasks[i].entry == NULL) {
            continue;
        }
        if (now >= tasks[i].next_tick) {
            tasks[i].entry(tasks[i].arg);
            tasks[i].last_run_tick = now;
            tasks[i].run_count++;
            scheduler_note_run(tasks[i].id, tasks[i].period_ticks, now);
            tasks[i].next_tick = now + tasks[i].period_ticks;
        }
    }
}

void task_shutdown_all(void)
{
    g_task_scheduler_stopping = true;
    memset(tasks, 0, sizeof(tasks));
}

bool task_scheduler_stopping(void)
{
    return g_task_scheduler_stopping;
}

uint32_t task_count(void)
{
    uint32_t count = 0;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].used) {
            count++;
        }
    }
    return count;
}

uint32_t tcb_capacity(void)
{
    return MAX_TASKS;
}

bool tcb_snapshot(uint32_t index, tcb_t *out)
{
    if (index >= MAX_TASKS || out == NULL) {
        return false;
    }
    *out = tasks[index];
    return true;
}
