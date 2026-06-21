#include "common.h"
#include "scheduler.h"
#include "schedopt.h"
#include "smp.h"

static schedopt_info_t g_schedopt_info;

void schedopt_refresh(void)
{
    const smp_info_t *smp = smp_info();
    const scheduler_info_t *sched = scheduler_info();

    g_schedopt_info.smp_supported = smp->supported;
    g_schedopt_info.ap_scheduler_ready = !smp->bootstrap_only && smp->online_processors > 1;
    g_schedopt_info.load_balancer_ready = g_schedopt_info.ap_scheduler_ready;
    g_schedopt_info.logical_processors = smp->logical_processors;
    g_schedopt_info.online_processors = smp->online_processors;
    g_schedopt_info.dispatches = sched->dispatches;
    strcpy(g_schedopt_info.policy, scheduler_policy_name(sched->policy));
    strcpy(g_schedopt_info.status,
           g_schedopt_info.ap_scheduler_ready ? "schedopt: smp load balancing ready" : "schedopt: bsp scheduler tuned");
}

void schedopt_init(void)
{
    memset(&g_schedopt_info, 0, sizeof(g_schedopt_info));
    g_schedopt_info.initialized = true;
    schedopt_refresh();
}

const schedopt_info_t *schedopt_info(void)
{
    schedopt_refresh();
    return &g_schedopt_info;
}

const char *schedopt_status(void)
{
    schedopt_refresh();
    return g_schedopt_info.status;
}
