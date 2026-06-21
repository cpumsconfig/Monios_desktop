#include "eevdf.h"
#include "common.h"

#define EEVDF_MAX_TASKS 32U

typedef struct {
    bool used;
    uint32_t task_id;
    uint32_t weight;
    uint64_t vruntime;
    uint64_t deadline;
} eevdf_task_t;

static eevdf_task_t g_tasks[EEVDF_MAX_TASKS];
static eevdf_info_t g_info;
static char g_status[64];

static eevdf_task_t *eevdf_find_or_create(uint32_t task_id)
{
    eevdf_task_t *free_slot = NULL;

    for (uint32_t index = 0; index < EEVDF_MAX_TASKS; index++) {
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
        free_slot->weight = 1;
        g_info.tracked_tasks++;
    }
    return free_slot;
}

static void eevdf_recalc_min_deadline(void)
{
    bool seen = false;
    uint64_t min_deadline = 0;

    for (uint32_t index = 0; index < EEVDF_MAX_TASKS; index++) {
        if (!g_tasks[index].used) {
            continue;
        }
        if (!seen || g_tasks[index].deadline < min_deadline) {
            min_deadline = g_tasks[index].deadline;
            seen = true;
        }
    }
    g_info.min_deadline = seen ? min_deadline : 0;
}

void eevdf_init(void)
{
    memset(g_tasks, 0, sizeof(g_tasks));
    memset(&g_info, 0, sizeof(g_info));
    strcpy(g_status, "eevdf: ready");
}

void eevdf_register_task(uint32_t task_id, uint32_t weight)
{
    eevdf_task_t *task = eevdf_find_or_create(task_id);

    if (task == NULL) {
        strcpy(g_status, "eevdf: table full");
        return;
    }
    task->weight = weight == 0 ? 1u : weight;
}

void eevdf_note_run(uint32_t task_id, uint32_t slice_ticks, uint64_t now_ticks)
{
    eevdf_task_t *task = eevdf_find_or_create(task_id);
    uint64_t slice = slice_ticks == 0 ? 1u : slice_ticks;

    if (task == NULL) {
        strcpy(g_status, "eevdf: task dropped");
        return;
    }
    task->vruntime += slice;
    task->deadline = now_ticks + slice + (task->vruntime / (uint64_t) task->weight);
    g_info.dispatches++;
    g_info.last_deadline = task->deadline;
    eevdf_recalc_min_deadline();
    strcpy(g_status, "eevdf: updated");
}

bool eevdf_task_deadline(uint32_t task_id, uint64_t *out_deadline)
{
    for (uint32_t index = 0; index < EEVDF_MAX_TASKS; index++) {
        if (g_tasks[index].used && g_tasks[index].task_id == task_id) {
            if (out_deadline != NULL) {
                *out_deadline = g_tasks[index].deadline;
            }
            return true;
        }
    }
    return false;
}

const eevdf_info_t *eevdf_info(void)
{
    return &g_info;
}

const char *eevdf_status(void)
{
    return g_status;
}
