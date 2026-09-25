#ifndef _PRINT_H_
#define _PRINT_H_

#include "stdbool.h"
#include "stdint.h"

#define PRINTER_MAX_PRINTERS 4U
#define PRINTER_MAX_JOBS     8U
#define PRINTER_NAME_MAX     32U
#define PRINTER_PREVIEW_BYTES (64U * 1024U)

/* Printer port kinds. */
typedef enum {
    PRINTER_PORT_NONE = 0,
    PRINTER_PORT_PARALLEL,   /* LPT1 */
    PRINTER_PORT_USB,
    PRINTER_PORT_NETWORK
} printer_port_kind_t;

typedef enum {
    PRINTER_STATUS_IDLE = 0,
    PRINTER_STATUS_PRINTING,
    PRINTER_STATUS_OFFLINE,
    PRINTER_STATUS_ERROR
} printer_status_t;

typedef enum {
    PRINT_JOB_PENDING = 0,
    PRINT_JOB_PRINTING,
    PRINT_JOB_DONE,
    PRINT_JOB_CANCELLED
} print_job_state_t;

typedef struct {
    char name[PRINTER_NAME_MAX];
    printer_port_kind_t port;
    printer_status_t status;
    bool is_default;
    uint32_t jobs_queued;
} printer_t;

typedef struct {
    uint32_t id;
    char printer[PRINTER_NAME_MAX];
    char document[PRINTER_NAME_MAX];
    uint32_t size_bytes;
    uint32_t pages;
    print_job_state_t state;
} print_job_t;

/* Syscall 57 request descriptor. */
typedef enum {
    PRINTER_CTL_REGISTER = 0,
    PRINTER_CTL_REMOVE,
    PRINTER_CTL_QUEUE_JOB,
    PRINTER_CTL_CANCEL_JOB,
    PRINTER_CTL_ENUM_PRINTERS,
    PRINTER_CTL_ENUM_JOBS,
    PRINTER_CTL_PREVIEW_START,
    PRINTER_CTL_PREVIEW_SUBMIT,
    PRINTER_CTL_SET_DEFAULT
} printer_ctl_cmd_t;

typedef struct {
    uint32_t cmd;                 /* in: printer_ctl_cmd_t */
    char     name[PRINTER_NAME_MAX]; /* in/out: printer name */
    uint32_t port;                /* in: printer_port_kind_t */
    uint32_t job_id;              /* in/out */
    uint32_t value;               /* in: pages / size */
    const uint8_t *preview_data;  /* in: preview submit buffer */
    uint32_t preview_len;
    int32_t  result;              /* out */
} printer_ctl_request_t;

void print_init(void);
int32_t printer_register(const char *name, printer_port_kind_t port);
int32_t printer_remove(const char *name);
void printer_set_default(const char *name);
int32_t printer_queue_job(const char *printer, const char *document,
                          uint32_t size_bytes, uint32_t pages);
bool printer_cancel_job(uint32_t job_id);
uint32_t printer_enum(printer_t *out, uint32_t max);
uint32_t printer_jobs(print_job_t *out, uint32_t max);
int32_t printer_preview_start(const char *printer);
int32_t printer_preview_submit(const uint8_t *data, uint32_t len);
int32_t printer_ctl(const printer_ctl_request_t *request);
const char *printer_status_str(void);

#endif
