#include "prsys.h"
#include "common.h"
#include "interrupt.h"
#include "pcb.h"
#include "scheduler.h"
#include "task.h"

static prsys_info_t g_prsys_info;
static char g_prsys_status[64];

static void prsys_refresh(void)
{
    g_prsys_info.processes = pcb_count();
    g_prsys_info.tasks = task_count();
    g_prsys_info.uptime_ticks = timer_ticks();
    g_prsys_info.scheduler_policy = (uint32_t) scheduler_info()->policy;
}

void prsys_init(void)
{
    memset(&g_prsys_info, 0, sizeof(g_prsys_info));
    strcpy(g_prsys_status, "prsys: online");
    prsys_refresh();
}

const prsys_info_t *prsys_info(void)
{
    prsys_refresh();
    return &g_prsys_info;
}

const char *prsys_status(void)
{
    prsys_refresh();
    return g_prsys_status;
}
