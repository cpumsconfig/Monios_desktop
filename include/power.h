#ifndef _POWER_H_
#define _POWER_H_

#include "stdbool.h"
#include "stdint.h"

typedef struct {
    bool initialized;
    bool acpi_ready;
    bool power_button_ready;
    bool sleep_ready;
    bool reset_ready;
    bool cpu_frequency_detected;
    bool device_power_ready;
    uint16_t sci_irq;
    uint8_t sleep_state;
    char status[64];
} power_info_t;

/* Power action states for syscall POWER_SET_STATE (54). */
typedef enum {
    POWER_STATE_ON = 0,
    POWER_STATE_SLEEP_S3 = 3,     /* suspend to RAM */
    POWER_STATE_HIBERNATE_S4 = 4, /* suspend to disk */
    POWER_STATE_SHUTDOWN_S5 = 5,
    POWER_STATE_REBOOT = 6
} power_state_t;

/* Battery / AC status (Feature 9). */
typedef struct {
    bool present;
    bool ac_online;
    bool charging;
    uint8_t percent;        /* 0..100 */
    uint32_t remaining_minutes;
    char source[16];
} power_battery_t;

/* Power policy (Feature 9): configurable timeouts in seconds. */
typedef struct {
    uint32_t screen_off_seconds;   /* 0 = never */
    uint32_t standby_seconds;      /* 0 = never */
    bool display_off;
    bool standby_enabled;
} power_policy_t;

/* Syscall 54 request. */
typedef struct {
    uint32_t action;   /* power_state_t */
    int32_t  result;  /* out: 0 ok, <0 error */
} power_set_state_request_t;

void power_init(void);
void power_refresh(void);
const power_info_t *power_info(void);
const char *power_status(void);

/* --- Feature 9: idle / display / battery policy --- */
void power_notify_activity(void);                 /* call on any input event */
void power_tick(void);                            /* called once per timer tick */
void power_set_screen_timeout(uint32_t seconds);
void power_set_standby_timeout(uint32_t seconds);
const power_policy_t *power_get_policy(void);
const power_battery_t *power_battery_status(void);
bool power_display_off(void);

/* --- Feature 8: sleep / hibernate framework --- */
int32_t power_set_state(uint32_t action);        /* syscall 54 handler */
bool power_hibernate_to_disk(void);              /* S4: save RAM to file */
bool power_restore_from_disk(void);              /* S4: load RAM from file */
bool power_enter_s3(void);                       /* S3: suspend to RAM */

#endif
