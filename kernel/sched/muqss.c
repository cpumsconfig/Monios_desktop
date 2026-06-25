#include "muqss.h"
#include "common.h"

#define MUQSS_MAX_TASKS 32U

typedef struct {
    bool used;
    uint32_t task_id;
    uint32_t period_ticks;
    uint64_t last_run_tick;
    uint32_t score;
} muqss_task_t;

static muqss_task_t g_tasks[MUQSS_MAX_TASKS];
static muqss_info_t g_info;
static char g_status[64];

static muqss_task_t *muqss_find_or_create(uint32_t task_id)
{
    muqss_task_t *free_slot = NULL;

    for (uint32_t index = 0; index < MUQSS_MAX_TASKS; index++) {
        if (g_tasks[index].used && g_tasks[index].task_id == task_id) {
            return &g_tasks[index];
        }
        if (!g_tasks[index].used && free_slot == NULL) {
            free_slot = &g_tasks[index];
        }
    }
    if (free_slot != NULL) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->task_id = task_id;
        g_info.tracked_tasks++;
    }
    return free_slot;
}

void muqss_init(void)
{
    memset(g_tasks, 0, sizeof(g_tasks));
    memset(&g_info, 0, sizeof(g_info));
    strcpy(g_status, "muqss: ready");
}

void muqss_register_task(uint32_t task_id, uint32_t period_ticks)
{
    muqss_task_t *task = muqss_find_or_create(task_id);

    if (task == NULL) {
        strcpy(g_status, "muqss: table full");
        return;
    }
    task->period_ticks = period_ticks == 0 ? 1u : period_ticks;
}

void muqss_note_run(uint32_t task_id, uint32_t period_ticks, uint64_t now_ticks)
{
    muqss_task_t *task = muqss_find_or_create(task_id);

    if (task == NULL) {
        strcpy(g_status, "muqss: task dropped");
        return;
    }
    task->period_ticks = period_ticks == 0 ? 1u : period_ticks;
    task->last_run_tick = now_ticks;
    task->score += task->period_ticks <= 1 ? 8u : (32u / task->period_ticks) + 1u;
    g_info.dispatches++;
    g_info.active_score = task->score;
    g_info.last_task = task_id;
    strcpy(g_status, "muqss: updated");
}

const muqss_info_t *muqss_info(void)
{
    return &g_info;
}

const char *muqss_status(void)
{
    return g_status;
}
