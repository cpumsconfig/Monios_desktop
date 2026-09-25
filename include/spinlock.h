#ifndef _SPINLOCK_H_
#define _SPINLOCK_H_

#include "stdint.h"

/*
 * MoniOS x86_64 spinlocks.
 *
 * Test-and-set style using GCC's __sync atomic builtins (supported by the
 * project's x86_64-elf-gcc 7.1 toolchain).  The irqsave variants additionally
 * clear EFLAGS.IF around the lock so an interrupt handler on the same CPU
 * cannot re-enter the protected data structure and self-deadlock.
 *
 * Usage:
 *   static spinlock_t g_foo_lock = SPINLOCK_INITIALIZER;
 *   uint64_t flags;
 *   spin_lock_irqsave(&g_foo_lock, flags);
 *   ... critical section ...
 *   spin_unlock_irqrestore(&g_foo_lock, flags);
 */

typedef struct {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INITIALIZER { 0U }

static inline void spinlock_init(spinlock_t *lock)
{
    if (lock != NULL) {
        lock->locked = 0U;
    }
}

static inline void spin_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1U) != 0U) {
        while (lock->locked != 0U) {
            __asm__ volatile ("pause" ::: "memory");
        }
    }
    __asm__ volatile ("" ::: "memory");
}

static inline void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");
    __sync_lock_release(&lock->locked, 0U);
}

static inline void spin_lock_irqsave(spinlock_t *lock, uint64_t *flags)
{
#ifdef __HOST_TEST__
    /* Host (Windows / MinGW) regression build: cli/popfq are privileged
     * and #GP in user space. The host tests are single-threaded, so the
     * lock is a no-op. The real kernel build is unchanged. */
    (void) lock; (void) flags;
#else
    uint64_t f;

    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    *flags = f;
    spin_lock(lock);
#endif
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags)
{
#ifdef __HOST_TEST__
    (void) lock; (void) flags;
#else
    spin_unlock(lock);
    __asm__ volatile ("push %0; popfq" :: "r"(flags) : "memory");
#endif
}

/* Non-sleeping try-lock: returns 1 if acquired, 0 if already held. */
static inline uint32_t spin_trylock(spinlock_t *lock)
{
    return (uint32_t)__sync_lock_test_and_set(&lock->locked, 1U) == 0U;
}

#endif
