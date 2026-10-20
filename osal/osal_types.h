/*
 * osal_types.h
 *
 * OSAL (OS Abstraction Layer) common types shared by all backends.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No RTOS-specific types leak through.
 *  - Handles are opaque `void *` pointers; the concrete object lives in the
 *    backend (Zephyr k_* objects / Linux pthread & POSIX IPC objects). The
 *    application never dereferences them, so the ABI stays stable across
 *    backends.
 *  - Error codes follow the Zephyr style: functions return 0 on success and
 *    a negative errno value (`-EINVAL`, `-ENOMEM`, ...) on failure.
 *  - Priority is an abstract value: 0 = lowest. Applications use 0-31.
 *
 * This header must compile standalone:
 *   gcc -c -I osal/ -x c -fsyntax-only osal/osal_types.h
 */
#ifndef OSAL_TYPES_H
#define OSAL_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** Task handle (backend: Zephyr k_thread / POSIX pthread). */
typedef void *osal_task_t;

/** Mutex handle (backend: Zephyr k_mutex / POSIX pthread_mutex_t). */
typedef void *osal_mutex_t;

/** Counting semaphore handle (backend: Zephyr k_sem / POSIX sem_t). */
typedef void *osal_sem_t;

/** Message queue handle (backend: Zephyr k_msgq / POSIX mqd_t). */
typedef void *osal_mq_t;

/* ------------------------------------------------------------------------- */
/* Error codes (M4: negative errno style, Zephyr-like)                       */
/* ------------------------------------------------------------------------- */

/** Operation succeeded. */
#define OSAL_OK 0
/** Invalid parameter (=-EINVAL). */
#define OSAL_ERR_PARAM (-22)
/** Out of memory (=-ENOMEM). */
#define OSAL_ERR_NOMEM (-12)
/** Operation timed out (=-ETIMEDOUT). */
#define OSAL_ERR_TIMEOUT (-110)
/** Resource busy (=-EBUSY). */
#define OSAL_ERR_BUSY (-16)
/** Resource not available / no such object (=-ENODEV). */
#define OSAL_ERR_NODEV (-19)
/** Operation not supported (=-ENOTSUP). */
#define OSAL_ERR_NOTSUP (-95)

/* ------------------------------------------------------------------------- */
/* Timeout semantics                                                         */
/* ------------------------------------------------------------------------- */

/** Block forever until the operation completes. */
#define OSAL_WAIT_FOREVER (0xFFFFFFFFu)
/** Never block; return immediately with OSAL_ERR_TIMEOUT if not ready. */
#define OSAL_NO_WAIT (0u)

/* ------------------------------------------------------------------------- */
/* Task attributes                                                           */
/* ------------------------------------------------------------------------- */

/**
 * @brief Task creation attributes.
 *
 * @p prio is an abstract priority where 0 = lowest. Application code should
 * use the 0-31 range; each backend maps it onto its native policy
 * (Zephyr preemptive range, Linux SCHED_FIFO 1-32).
 */
typedef struct {
	/** Task name (may be NULL; used for debug/logging only). */
	const char *name;
	/** Task entry point. @p arg is passed through unchanged. */
	void (*entry)(void *arg);
	/** User argument forwarded to @ref entry. */
	void *arg;
	/** Stack size in bytes. */
	uint32_t stack_size;
	/** Abstract priority, 0 = lowest (see @ref osal_task_attr_t). */
	uint32_t prio;
} osal_task_attr_t;

#ifdef __cplusplus
}
#endif

#endif /* OSAL_TYPES_H */