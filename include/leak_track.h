#ifndef _LEAK_TRACK_H_
#define _LEAK_TRACK_H_

#include "stdint.h"

/*
 * MoniOS kernel heap leak tracker.
 *
 * The tracker is fed by kmalloc()/kfree() in kernel/mm/memory.c:
 *   - on allocation, memory.c calls leak_track_record() with the returned
 *     pointer, the requested size and __builtin_return_address(0) (which,
 *     inside kmalloc(), resolves to its caller);
 *   - on free, memory.c calls leak_track_unrecord().
 *
 * No heap memory is used by the tracker itself: entries live in a statically
 * allocated array so the tracker can record allocations made before the heap
 * is fully up, and so leak reporting is safe inside a panic context.
 */

/* (Re)initialise the tracker. Safe to call multiple times. */
void leak_track_init(void);

/* Record a live allocation. ptr must be non-NULL. */
void leak_track_record(void *ptr, uint64_t size, void *caller);

/* Drop the record for ptr (no-op if never recorded / already freed). */
void leak_track_unrecord(void *ptr);

/* Dump every still-live allocation over the serial port. */
void leak_track_report(void);

/* Snapshot current totals. Either out parameter may be NULL. */
void leak_track_summary(uint64_t *out_blocks, uint64_t *out_bytes);

#endif
