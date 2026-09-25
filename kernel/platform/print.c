/*
 * print.c - printing subsystem (Feature 10)
 *
 * Framework for Monios printing:
 *   - printer registry (add / remove / set default)
 *   - print job queue (queue / cancel / enumerate)
 *   - print preview: renders output into an in-memory buffer instead of the
 *     physical device, so an app can show a WYSIWYG preview before spooling.
 *
 * This file is owned by the system-services group and is wired into the
 * build by adding print.o to KERNEL_PLATFORM_OBJS in the Makefile (the
 * Makefile is owned by the build group). The kernel-side handler
 * printer_ctl() backs syscall 57 (PRINTER_CTL).
 */

#include "kernel.h"
#include "stddef.h"
#include "print.h"
#include "string.h"

static printer_t g_printers[PRINTER_MAX_PRINTERS];
static print_job_t g_jobs[PRINTER_MAX_JOBS];
static uint8_t g_preview_buffer[PRINTER_PREVIEW_BYTES];
static uint32_t g_preview_used;
static char g_print_status[64];
static uint32_t g_next_job_id;

void print_init(void)
{
    memset(g_printers, 0, sizeof(g_printers));
    memset(g_jobs, 0, sizeof(g_jobs));
    memset(g_preview_buffer, 0, sizeof(g_preview_buffer));
    g_preview_used = 0;
    g_next_job_id = 1;
    strcpy(g_print_status, "print: subsystem ready");
}

static printer_t *printer_find(const char *name)
{
    for (uint32_t i = 0; i < PRINTER_MAX_PRINTERS; i++) {
        if (g_printers[i].name[0] != '\0' && strcmp(g_printers[i].name, name) == 0) {
            return &g_printers[i];
        }
    }
    return NULL;
}

static printer_t *printer_free_slot(void)
{
    for (uint32_t i = 0; i < PRINTER_MAX_PRINTERS; i++) {
        if (g_printers[i].name[0] == '\0') {
            return &g_printers[i];
        }
    }
    return NULL;
}

int32_t printer_register(const char *name, printer_port_kind_t port)
{
    printer_t *p;

    if (name == NULL || name[0] == '\0' || printer_find(name) != NULL) {
        return -1;
    }
    p = printer_free_slot();
    if (p == NULL) {
        strcpy(g_print_status, "print: printer table full");
        return -1;
    }
    strcpy(p->name, name);
    p->port = port;
    p->status = PRINTER_STATUS_IDLE;
    p->is_default = (g_printers[0].name[0] == '\0');
    p->jobs_queued = 0;
    strcpy(g_print_status, "print: printer registered");
    return 0;
}

int32_t printer_remove(const char *name)
{
    printer_t *p = printer_find(name);

    if (p == NULL) {
        return -1;
    }
    memset(p, 0, sizeof(*p));
    return 0;
}

void printer_set_default(const char *name)
{
    for (uint32_t i = 0; i < PRINTER_MAX_PRINTERS; i++) {
        g_printers[i].is_default = strcmp(g_printers[i].name, name) == 0;
    }
}

static print_job_t *job_free_slot(void)
{
    for (uint32_t i = 0; i < PRINTER_MAX_JOBS; i++) {
        if (g_jobs[i].state == PRINT_JOB_PENDING ||
            g_jobs[i].state == PRINT_JOB_DONE ||
            g_jobs[i].state == PRINT_JOB_CANCELLED) {
            if (g_jobs[i].state != PRINT_JOB_PENDING) {
                return &g_jobs[i];
            }
        }
    }
    for (uint32_t i = 0; i < PRINTER_MAX_JOBS; i++) {
        if (g_jobs[i].id == 0) {
            return &g_jobs[i];
        }
    }
    return NULL;
}

int32_t printer_queue_job(const char *printer, const char *document,
                          uint32_t size_bytes, uint32_t pages)
{
    printer_t *p = printer_find(printer);
    print_job_t *job;

    if (p == NULL) {
        return -1;
    }
    job = job_free_slot();
    if (job == NULL) {
        strcpy(g_print_status, "print: job queue full");
        return -1;
    }
    memset(job, 0, sizeof(*job));
    job->id = g_next_job_id++;
    strcpy(job->printer, printer);
    strcpy(job->document, document != NULL ? document : "untitled");
    job->size_bytes = size_bytes;
    job->pages = pages;
    job->state = PRINT_JOB_PENDING;
    p->jobs_queued++;
    strcpy(g_print_status, "print: job queued");
    return (int32_t) job->id;
}

bool printer_cancel_job(uint32_t job_id)
{
    for (uint32_t i = 0; i < PRINTER_MAX_JOBS; i++) {
        if (g_jobs[i].id == job_id) {
            g_jobs[i].state = PRINT_JOB_CANCELLED;
            return true;
        }
    }
    return false;
}

uint32_t printer_enum(printer_t *out, uint32_t max)
{
    uint32_t n = 0;

    if (out == NULL || max == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < PRINTER_MAX_PRINTERS && n < max; i++) {
        if (g_printers[i].name[0] != '\0') {
            out[n++] = g_printers[i];
        }
    }
    return n;
}

uint32_t printer_jobs(print_job_t *out, uint32_t max)
{
    uint32_t n = 0;

    if (out == NULL || max == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < PRINTER_MAX_JOBS && n < max; i++) {
        if (g_jobs[i].id != 0) {
            out[n++] = g_jobs[i];
        }
    }
    return n;
}

int32_t printer_preview_start(const char *printer)
{
    if (printer_find(printer) == NULL) {
        return -1;
    }
    g_preview_used = 0;
    strcpy(g_print_status, "print: preview started");
    return 0;
}

int32_t printer_preview_submit(const uint8_t *data, uint32_t len)
{
    uint32_t space = PRINTER_PREVIEW_BYTES - g_preview_used;

    if (data == NULL || len == 0) {
        return -1;
    }
    if (len > space) {
        len = space;
    }
    memcpy(&g_preview_buffer[g_preview_used], data, len);
    g_preview_used += len;
    return (int32_t) len;
}

int32_t printer_ctl(const printer_ctl_request_t *request)
{
    if (request == NULL) {
        return -1;
    }
    switch (request->cmd) {
    case PRINTER_CTL_REGISTER:
        return printer_register(request->name, (printer_port_kind_t) request->port);
    case PRINTER_CTL_REMOVE:
        return printer_remove(request->name);
    case PRINTER_CTL_SET_DEFAULT:
        printer_set_default(request->name);
        return 0;
    case PRINTER_CTL_QUEUE_JOB:
        return printer_queue_job(request->name, request->name,
                                 request->value, request->value);
    case PRINTER_CTL_CANCEL_JOB:
        return printer_cancel_job(request->job_id) ? 0 : -1;
    case PRINTER_CTL_ENUM_PRINTERS: {
        printer_t list[PRINTER_MAX_PRINTERS];
        return (int32_t) printer_enum(list, PRINTER_MAX_PRINTERS);
    }
    case PRINTER_CTL_ENUM_JOBS: {
        print_job_t list[PRINTER_MAX_JOBS];
        return (int32_t) printer_jobs(list, PRINTER_MAX_JOBS);
    }
    case PRINTER_CTL_PREVIEW_START:
        return printer_preview_start(request->name);
    case PRINTER_CTL_PREVIEW_SUBMIT:
        return printer_preview_submit(request->preview_data, request->preview_len);
    default:
        return -1;
    }
}

const char *printer_status_str(void)
{
    return g_print_status;
}
