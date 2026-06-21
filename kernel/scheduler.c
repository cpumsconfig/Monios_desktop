#include "scheduler.h"
#include "common.h"
#include "eevdf.h"
#include "muqss.h"

static scheduler_info_t g_scheduler_info;
static char g_scheduler_status[64];

const char *scheduler_policy_name(scheduler_policy_t policy)
{
    switch (policy) {
    case SCHED_POLICY_EEVDF:
        return "eevdf";
    case SCHED_POLICY_MUQSS:
        return "muqss";
    case SCHED_POLICY_RR:
    default:
        return "rr";
    }
}

void scheduler_init(void)
{
    memset(&g_scheduler_info, 0, sizeof(g_scheduler_info));
    g_scheduler_info.policy = SCHED_POLICY_RR;
    g_scheduler_info.last_task = 0xFFFFFFFFu;
    strcpy(g_scheduler_status, "scheduler: rr");
    eevdf_init();
    muqss_init();
}

void scheduler_register_task(uint32_t task_id, uint32_t period_ticks)
{
    if (task_id + 1 > g_scheduler_info.task_count) {
        g_scheduler_info.task_count = task_id + 1;
    }
    eevdf_register_task(task_id, period_ticks == 0 ? 1u : period_ticks);
    muqss_register_task(task_id, period_ticks == 0 ? 1u : period_ticks);
}

void scheduler_note_run(uint32_t task_id, uint32_t period_ticks, uint64_t now_ticks)
{
    g_scheduler_info.dispatches++;
    g_scheduler_info.last_task = task_id;
    eevdf_note_run(task_id, period_ticks, now_ticks);
    muqss_note_run(task_id, period_ticks, now_ticks);
}

bool scheduler_set_policy(scheduler_policy_t policy)
{
    if (policy != SCHED_POLICY_RR && policy != SCHED_POLICY_EEVDF && policy != SCHED_POLICY_MUQSS) {
        return false;
    }
    g_scheduler_info.policy = policy;
    strcpy(g_scheduler_status, "scheduler: ");
    strcat(g_scheduler_status, scheduler_policy_name(policy));
    return true;
}

const scheduler_info_t *scheduler_info(void)
{
    return &g_scheduler_info;
}

const char *scheduler_status(void)
{
    return g_scheduler_status;
}
