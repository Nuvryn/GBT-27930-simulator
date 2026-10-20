/*
 * osal.h
 *
 * OSAL (OS Abstraction Layer) interface contract.
 *
 * Design principles:
 *  - Pure functional interface contract: plain C functions, no vtable, no
 *    function-pointer tables, no RTOS-specific API shapes. The same business
 *    code compiles against Zephyr (k_thread/k_mutex/k_sem/k_msgq/k_msleep)
 *    and Linux (pthread/mutex/sem/mqueue/nanosleep) backends unchanged.
 *  - Layer policy: only abstract what differs between target platforms
 *    (task/mutex/semaphore/queue/delay/memory). Everything a platform already
 *    provides (device model, DTS, boot orchestration) is left untouched.
 *  - Error handling (M4): every function returns `int`, 0 = success, a
 *    negative errno value on failure (-EINVAL, -ENOMEM, -ETIMEDOUT, ...).
 *    Constants defined in osal_types.h.
 *  - Context constraint (M7): ALL APIs below are thread-context only and must
 *    NOT be called from interrupt context (Zephyr ISR / Linux signal handler).
 *    Interrupts do minimal work (set a flag, wake a task) and defer all real
 *    processing to a task. There are no `_from_isr` variants in v1.
 *  - Timeout semantics: OSAL_WAIT_FOREVER blocks indefinitely,
 *    OSAL_NO_WAIT returns immediately. timeout_ms >= 1 blocks up to N ms.
 *
 * This header must compile standalone:
 *   gcc -c -I osal/ -x c -fsyntax-only osal/osal.h
 */
#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>
#include "osal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Task management                                                           */
/* ------------------------------------------------------------------------- */

/**
 * @brief Create and start a task.
 *
 * The task begins running as soon as it is created (backend-dependent
 * scheduling latency applies). @p attr->entry receives @p attr->arg.
 *
 * @param[in] attr Task attributes (name/entry/arg/stack_size/prio).
 *                 Must not be NULL; entry and stack_size must be valid.
 *
 * @retval OSAL_OK              Success.
 * @retval OSAL_ERR_PARAM       Invalid/ NULL argument or bad stack size.
 * @retval OSAL_ERR_NOMEM       Insufficient memory for the task control
 *                              block or its stack.
 *
 * @note Thread context only (M7). Not callable from interrupts.
 */
int osal_task_create(const osal_task_attr_t *attr);

/**
 * @brief Delete a task and release its resources.
 *
 * @param[in] task Handle returned by @ref osal_task_create. Passing NULL is
 *                 allowed and is a no-op.
 *
 * @note Thread context only (M7).
 */
void osal_task_delete(osal_task_t task);

/**
 * @brief Yield the CPU to another ready task of equal or higher priority.
 *
 * @note Thread context only (M7).
 */
void osal_task_yield(void);

/* ------------------------------------------------------------------------- */
/* Delay / time                                                              */
/* ------------------------------------------------------------------------- */

/**
 * @brief Sleep for at least @p ms milliseconds.
 *
 * Yields the CPU for the requested duration (Zephyr k_msleep / POSIX
 * nanosleep). Actual sleep may be longer than requested.
 *
 * @param[in] ms Delay in milliseconds. 0 means a single yield.
 *
 * @note Thread context only (M7).
 */
void osal_delay_ms(uint32_t ms);

/**
 * @brief Busy-wait for approximately @p us microseconds.
 *
 * Does NOT yield the CPU (Zephyr k_busy_wait / POSIX busy loop). Use only
 * for short, time-critical delays where yielding would break timing.
 *
 * @param[in] us Delay in microseconds.
 *
 * @note Thread context only (M7).
 */
void osal_delay_us(uint32_t us);

/**
 * @brief Get the current system tick in milliseconds.
 *
 * @return Monotonic tick counter in ms. Not guaranteed to share an epoch
 *         across reboots; use only for relative timing/deltas.
 *
 * @note Thread context only (M7).
 */
uint32_t osal_get_tick_ms(void);

/* ------------------------------------------------------------------------- */
/* Mutex                                                                     */
/* ------------------------------------------------------------------------- */

/**
 * @brief Create a mutex.
 *
 * @param[out] m Receives the new mutex handle. Must not be NULL.
 *
 * @retval OSAL_OK         Success.
 * @retval OSAL_ERR_PARAM  @p m is NULL.
 * @retval OSAL_ERR_NOMEM  Insufficient memory.
 *
 * @note Thread context only (M7).
 */
int osal_mutex_create(osal_mutex_t *m);

/**
 * @brief Lock a mutex within @p timeout_ms.
 *
 * @param[in] m          Mutex handle from @ref osal_mutex_create.
 * @param[in] timeout_ms OSAL_WAIT_FOREVER to block indefinitely,
 *                       OSAL_NO_WAIT for non-blocking try-lock.
 *
 * @retval OSAL_OK             Lock acquired.
 * @retval OSAL_ERR_PARAM      @p m is NULL/invalid.
 * @retval OSAL_ERR_TIMEOUT    Mutex not free before the timeout expired.
 * @retval OSAL_ERR_BUSY       (OSAL_NO_WAIT) Mutex already locked.
 *
 * @note Thread context only (M7). Mutexes are not recursive in this contract:
 *       locking the same mutex from the owning task is a deadlock/error.
 */
int osal_mutex_lock(osal_mutex_t m, uint32_t timeout_ms);

/**
 * @brief Unlock a mutex owned by the calling task.
 *
 * @param[in] m Mutex handle from @ref osal_mutex_create.
 *
 * @retval OSAL_OK         Success.
 * @retval OSAL_ERR_PARAM  @p m is NULL/invalid.
 *
 * @note Thread context only (M7).
 */
int osal_mutex_unlock(osal_mutex_t m);

/**
 * @brief Destroy a mutex and release its resources.
 *
 * The mutex must be unlocked when destroyed; behaviour is undefined if a task
 * holds it.
 *
 * @param[in] m Mutex handle from @ref osal_mutex_create. Passing NULL is
 *              allowed and is a no-op.
 *
 * @retval OSAL_OK Success.
 *
 * @note Thread context only (M7).
 */
int osal_mutex_destroy(osal_mutex_t m);

/* ------------------------------------------------------------------------- */
/* Counting semaphore                                                        */
/* ------------------------------------------------------------------------- */

/**
 * @brief Create a counting semaphore with an initial and a maximum count.
 *
 * @param[out] s          Receives the new semaphore handle. Must not be NULL.
 * @param[in]  init_count Initial count (0 = locked).
 * @param[in]  max_count  Maximum count the semaphore can reach. Must be >= 1
 *                        and >= init_count.
 *
 * @retval OSAL_OK         Success.
 * @retval OSAL_ERR_PARAM  @p s NULL, or invalid init/max counts.
 * @retval OSAL_ERR_NOMEM  Insufficient memory.
 *
 * @note Thread context only (M7).
 */
int osal_sem_create(osal_sem_t *s, uint32_t init_count, uint32_t max_count);

/**
 * @brief Wait on a semaphore within @p timeout_ms.
 *
 * Decrements the count if > 0; otherwise blocks until a @ref osal_sem_post
 * wakes this waiter or the timeout expires.
 *
 * @param[in] s          Semaphore handle from @ref osal_sem_create.
 * @param[in] timeout_ms OSAL_WAIT_FOREVER to block indefinitely,
 *                       OSAL_NO_WAIT for non-blocking try-wait.
 *
 * @retval OSAL_OK             Acquired.
 * @retval OSAL_ERR_PARAM      @p s is NULL/invalid.
 * @retval OSAL_ERR_TIMEOUT    Not signalled before the timeout expired.
 *
 * @note Thread context only (M7).
 */
int osal_sem_wait(osal_sem_t s, uint32_t timeout_ms);

/**
 * @brief Post (give) a semaphore.
 *
 * Increments the count (capped at max_count) and wakes one pending waiter if
 * any.
 *
 * @param[in] s Semaphore handle from @ref osal_sem_create.
 *
 * @retval OSAL_OK         Success.
 * @retval OSAL_ERR_PARAM  @p s is NULL/invalid.
 *
 * @note Thread context only (M7). There is intentionally no _from_isr variant
 *       in v1 (M7); signal tasks with a stale flag checked from the task
 *       context instead.
 */
int osal_sem_post(osal_sem_t s);

/**
 * @brief Destroy a semaphore and release its resources.
 *
 * Behaviour is undefined if tasks are still blocked on @p s.
 *
 * @param[in] s Semaphore handle from @ref osal_sem_create. Passing NULL is
 *              allowed and is a no-op.
 *
 * @retval OSAL_OK Success.
 *
 * @note Thread context only (M7).
 */
int osal_sem_destroy(osal_sem_t s);

/* ------------------------------------------------------------------------- */
/* Message queue                                                             */
/* ------------------------------------------------------------------------- */

/**
 * @brief Create a fixed-size message queue.
 *
 * The queue holds up to @p max_msgs messages, each @p msg_size bytes.
 * Message data is copied in and out; ownership stays with the caller.
 *
 * @param[out] q         Receives the new queue handle. Must not be NULL.
 * @param[in]  msg_size  Size of one message in bytes. Must be >= 1.
 * @param[in]  max_msgs  Capacity in messages. Must be >= 1.
 *
 * @retval OSAL_OK         Success.
 * @retval OSAL_ERR_PARAM  @p q NULL, or @p msg_size/@p max_msgs == 0.
 * @retval OSAL_ERR_NOMEM  Insufficient memory for the queue or its buffer.
 *
 * @note Thread context only (M7).
 */
int osal_mq_create(osal_mq_t *q, size_t msg_size, size_t max_msgs);

/**
 * @brief Send a message to a queue within @p timeout_ms.
 *
 * Blocks while the queue is full until space frees up or the timeout expires.
 *
 * @param[in] q          Queue handle from @ref osal_mq_create.
 * @param[in] msg        Pointer to @p msg_size bytes of message data.
 * @param[in] timeout_ms OSAL_WAIT_FOREVER to block indefinitely,
 *                       OSAL_NO_WAIT to fail immediately when full.
 *
 * @retval OSAL_OK             Message copied into the queue.
 * @retval OSAL_ERR_PARAM      @p q or @p msg is NULL/invalid.
 * @retval OSAL_ERR_TIMEOUT    Queue stayed full until the timeout expired.
 *
 * @note Thread context only (M7).
 */
int osal_mq_send(osal_mq_t q, const void *msg, uint32_t timeout_ms);

/**
 * @brief Receive a message from a queue within @p timeout_ms.
 *
 * Blocks while the queue is empty until a message arrives or the timeout
 * expires. The message is copied into @p msg (@p msg_size bytes).
 *
 * @param[in]  q          Queue handle from @ref osal_mq_create.
 * @param[out] msg        Buffer receiving the message data.
 * @param[in]  timeout_ms OSAL_WAIT_FOREVER to block indefinitely,
 *                        OSAL_NO_WAIT to fail immediately when empty.
 *
 * @retval OSAL_OK             Message copied into @p msg.
 * @retval OSAL_ERR_PARAM      @p q or @p msg is NULL/invalid.
 * @retval OSAL_ERR_TIMEOUT    Queue stayed empty until the timeout expired.
 *
 * @note Thread context only (M7).
 */
int osal_mq_recv(osal_mq_t q, void *msg, uint32_t timeout_ms);

/**
 * @brief Destroy a message queue and release its resources.
 *
 * Behaviour is undefined if tasks are still blocked on @p q.
 *
 * @param[in] q Queue handle from @ref osal_mq_create. Passing NULL is
 *              allowed and is a no-op.
 *
 * @retval OSAL_OK Success.
 *
 * @note Thread context only (M7).
 */
int osal_mq_destroy(osal_mq_t q);

/* ------------------------------------------------------------------------- */
/* Memory allocation (M3)                                                    */
/* ------------------------------------------------------------------------- */

/**
 * @brief Allocate @p size bytes from the OS heap.
 *
 * Application code MUST use this instead of calling malloc/k_malloc directly,
 * so the same binary ports between Zephyr (k_malloc, requires
 * CONFIG_HEAP_MEM_POOL_SIZE) and Linux (malloc).
 *
 * @param[in] size Number of bytes to allocate.
 *
 * @return Non-NULL pointer on success, NULL if out of memory or @p size == 0.
 *
 * @note Thread context only (M7).
 */
void *osal_malloc(size_t size);

/**
 * @brief Free a block returned by @ref osal_malloc.
 *
 * @param[in] ptr Pointer to free. Passing NULL is allowed and is a no-op.
 *
 * @note Thread context only (M7).
 */
void osal_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */