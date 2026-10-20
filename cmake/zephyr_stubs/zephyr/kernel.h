/*
 * kernel.h - MOCK STUB for the Zephyr kernel API (host-side testing only).
 *
 * This is NOT part of the shipped Zephyr backend. It exists solely so the
 * backend .c files can be compiled and link-time mock-tested on the Windows
 * host (no Zephyr SDK available - see issues.md I3). The real build uses
 * Zephyr's <zephyr/kernel.h>.
 *
 * Only the minimal subset used by hal_zephyr_priv.h is modeled:
 *   k_timeout_t / k_mutex / k_spinlock;
 *   timeout sentinels K_FOREVER / K_NO_WAIT; K_MSEC();
 *   k_mutex_init/lock/unlock; k_spin_lock/unlock; k_uptime_get_32; k_yield.
 */
#ifndef ZEPHYR_INCLUDE_KERNEL_H_
#define ZEPHYR_INCLUDE_KERNEL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------- timeouts -------- */
typedef int64_t k_timeout_t;

#define K_NO_WAIT ((k_timeout_t)0)
#define K_MSEC(ms) ((k_timeout_t)(ms))
#define K_FOREVER ((k_timeout_t)-1)

/* k_ticks_t is not used by the HAL backend. */

/* -------- mutex -------- */
/* Mock representation; nothing but a flag the tests can inspect. */
struct k_mutex {
	int locked;
};

extern int k_mutex_init(struct k_mutex *mutex);
extern int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout);
extern void k_mutex_unlock(struct k_mutex *mutex);

/* -------- spinlock (used by none of the shipped files; kept rather than
 *          breaking a stray include) -------- */
struct k_spinlock {
	int dummy;
};
typedef uint32_t k_spinlock_key_t;

static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l)
{
	(void)l;
	return 0U;
}

static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key)
{
	(void)l;
	(void)key;
}

/* -------- time / scheduling -------- */
extern uint32_t k_uptime_get_32(void);
extern void k_yield(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_KERNEL_H_ */