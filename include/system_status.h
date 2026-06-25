#ifndef _SYSTEM_STATUS_H_
#define _SYSTEM_STATUS_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    uint32_t task_count;
    bool scheduler_stopping;
    bool shutdown_requested;
    bool reboot_requested;
    bool net_present;
    bool net_connected;
    uint32_t net_tx_packets;
    uint32_t net_rx_packets;
    uint32_t net_ping_requests;
    uint32_t net_ping_replies;
    bool net_dhcp_configured;
    char net_driver[16];
    char net_mac[18];
    char net_ip[16];
    char net_gateway[16];
    char net_dns[16];
    char net_status[64];
    char net_last_target[64];
    bool audio_playing;
    bool audio_paused;
    bool audio_present;
    uint8_t audio_volume;
    char audio_driver[16];
    char audio_track[64];
    uint32_t gpu_submits;
    uint32_t gpu_presents;
    uint32_t gpu_pending;
    uint32_t wm_windows;
    uint32_t wm_focused;
    bool terminal_active;
    bool terminal_focused;
    uint32_t terminal_lines;
    bool smp_supported;
    bool smp_bootstrap_only;
    uint32_t smp_logical_processors;
    uint32_t smp_online_processors;
} system_status_t;

#endif
