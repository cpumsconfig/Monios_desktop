#include "signal.h"
#include "common.h"
#include "pcb.h"

static signal_info_t g_signal_info;
static char g_signal_status[64];

void signal_init(void)
{
    memset(&g_signal_info, 0, sizeof(g_signal_info));
    strcpy(g_signal_status, "signal: ready");
}

bool signal_send(int32_t pid, uint8_t signo)
{
    uint32_t mask;

    if (signo == 0 || signo > 32) {
        strcpy(g_signal_status, "signal: bad signo");
        return false;
    }
    mask = 1u << (signo - 1u);
    if (!pcb_signal_or(pid, mask)) {
        strcpy(g_signal_status, "signal: pid missing");
        return false;
    }
    g_signal_info.sent_count++;
    strcpy(g_signal_status, "signal: queued");
    return true;
}

uint32_t signal_pending(int32_t pid)
{
    return pcb_pending_signal_mask(pid);
}

uint32_t signal_take_pending(int32_t pid)
{
    uint32_t mask = pcb_pending_signal_mask(pid);

    if (mask != 0 && pcb_signal_set(pid, 0)) {
        g_signal_info.fetch_count++;
        strcpy(g_signal_status, "signal: fetched");
    }
    return mask;
}

void signal_clear(int32_t pid)
{
    if (pcb_signal_set(pid, 0)) {
        g_signal_info.clear_count++;
        strcpy(g_signal_status, "signal: cleared");
    }
}

const signal_info_t *signal_info(void)
{
    return &g_signal_info;
}

const char *signal_status(void)
{
    return g_signal_status;
}
