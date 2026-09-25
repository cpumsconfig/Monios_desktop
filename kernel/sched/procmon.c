/*
 * procmon.c - process monitor (Feature 7)
 *
 * Presentation / aggregation layer built on top of the PCB enumeration API:
 *   - samples per-process CPU time over an interval and derives CPU usage %
 *   - groups processes into a parent -> children process tree
 *
 * NOTE: this module is owned by the system-services group. It is wired into
 * the build by adding procmon.o to KERNEL_RUNTIME_OBJS in the Makefile (the
 * Makefile is owned by the build group and is not edited by us). Until then the
 * callable entry points below are exercised through pcb.c's process_enum().
 */

#include "stddef.h"
#include "interrupt.h"
#include "pcb.h"
#include "string.h"

#define PROCMON_MAX_PROCESSES 16U
#define PROCMON_MAX_CHILDREN  8U

typedef struct {
    process_info_t info;
    uint32_t sample_cpu_ticks;  /* cpu ticks accumulated across the window */
    uint32_t cpu_permille;      /* 0..1000 = 0.0%..100.0% */
    int32_t children[PROCMON_MAX_CHILDREN];
    uint32_t child_count;
} procmon_node_t;

typedef struct {
    uint64_t window_ticks;
    uint64_t sample_cpu_total;
    procmon_node_t nodes[PROCMON_MAX_PROCESSES];
    uint32_t count;
} procmon_state_t;

static procmon_state_t g_procmon;
static uint64_t g_last_sample_ticks;
static uint64_t g_last_cpu[PROCMON_MAX_PROCESSES];
static int32_t  g_last_pid[PROCMON_MAX_PROCESSES];

void procmon_init(void)
{
    memset(&g_procmon, 0, sizeof(g_procmon));
    memset(g_last_cpu, 0, sizeof(g_last_cpu));
    for (uint32_t i = 0; i < PROCMON_MAX_PROCESSES; i++) {
        g_last_pid[i] = -1;
    }
    g_last_sample_ticks = timer_ticks();
}

/*
 * Take a sample. Call once per scheduler tick. The first call only records
 * the baseline CPU counters; the next call computes deltas.
 */
void procmon_sample(void)
{
    uint64_t now = timer_ticks();
    uint64_t elapsed = now - g_last_sample_ticks;
    process_info_t snapshot[PROCMON_MAX_PROCESSES];
    uint32_t n = process_enum(snapshot, PROCMON_MAX_PROCESSES);

    g_procmon.window_ticks = elapsed;
    g_procmon.count = n;
    g_procmon.sample_cpu_total = 0;

    for (uint32_t i = 0; i < n; i++) {
        uint64_t total = snapshot[i].cpu_user_ticks + snapshot[i].cpu_kernel_ticks;
        uint64_t prev = 0;
        bool found = false;

        g_procmon.nodes[i].info = snapshot[i];
        for (uint32_t j = 0; j < PROCMON_MAX_PROCESSES; j++) {
            if (g_last_pid[j] == (int32_t) snapshot[i].pid) {
                prev = g_last_cpu[j];
                found = true;
                break;
            }
        }
        if (found && elapsed > 0) {
            uint64_t delta = total - prev;
            uint64_t pct = (delta * 1000ULL) / elapsed;
            if (pct > 1000ULL) {
                pct = 1000ULL;
            }
            g_procmon.nodes[i].cpu_permille = (uint32_t) pct;
        } else {
            g_procmon.nodes[i].cpu_permille = 0;
        }
        g_last_cpu[i] = total;
        g_last_pid[i] = (int32_t) snapshot[i].pid;
        g_procmon.sample_cpu_total += g_procmon.nodes[i].cpu_permille;
    }
    g_last_sample_ticks = now;
}

/*
 * Build the parent -> children links after a sample.
 */
void procmon_build_tree(void)
{
    for (uint32_t i = 0; i < g_procmon.count; i++) {
        g_procmon.nodes[i].child_count = 0;
        for (uint32_t c = 0; c < PROCMON_MAX_CHILDREN; c++) {
            g_procmon.nodes[i].children[c] = -1;
        }
    }
    for (uint32_t child = 0; child < g_procmon.count; child++) {
        int32_t parent = g_procmon.nodes[child].info.parent_pid;
        for (uint32_t p = 0; p < g_procmon.count; p++) {
            if ((int32_t) g_procmon.nodes[p].info.pid == parent &&
                g_procmon.nodes[p].child_count < PROCMON_MAX_CHILDREN) {
                g_procmon.nodes[p].children[g_procmon.nodes[p].child_count++] =
                    (int32_t) g_procmon.nodes[child].info.pid;
                break;
            }
        }
    }
}

uint32_t procmon_count(void)
{
    return g_procmon.count;
}

const procmon_node_t *procmon_node(uint32_t index)
{
    return index < g_procmon.count ? &g_procmon.nodes[index] : NULL;
}

uint32_t procmon_total_cpu_permille(void)
{
    return g_procmon.sample_cpu_total;
}
