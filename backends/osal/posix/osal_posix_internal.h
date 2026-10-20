/*
 * osal_posix_internal.h
 *
 * Private helpers shared by the POSIX OSAL backend files. NOT part of the
 * public contract; never include this from application code.
 */
#ifndef OSAL_POSIX_INTERNAL_H
#define OSAL_POSIX_INTERNAL_H

#include <stdint.h>
#include <time.h>
#include "osal/osal.h"

/**
 * @brief Convert an OSAL relative timeout to an absolute CLOCK_REALTIME
 *        deadline usable with pthread_mutex_timedlock / sem_timedwait /
 *        mq_timedsend / mq_timedreceive.
 *
 * @param[in]  timeout_ms OSAL timeout (OSAL_NO_WAIT = 0 -> "now", so the
 *                        timed call fails immediately; OSAL_WAIT_FOREVER is
 *                        handled by the callers, which use the blocking
 *                        non-timed primitive instead).
 * @param[out] abs        Receives the absolute deadline.
 */
void osal_posix_deadline(uint32_t timeout_ms, struct timespec *abs);

#endif /* OSAL_POSIX_INTERNAL_H */