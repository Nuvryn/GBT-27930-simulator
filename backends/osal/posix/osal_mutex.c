/*
 * osal_mutex.c
 *
 * POSIX backend for OSAL mutex APIs.
 * Maps onto pthread_mutex_init / lock / trylock / timedlock / unlock /
 * destroy. The handle is an opaque void* wrapping a malloc'd pthread_mutex_t.
 */
#define _POSIX_C_SOURCE 200809L /* expose POSIX.1-2008 APIs under -std=c11 */
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include "osal/osal.h"
#include "osal_posix_internal.h"

int osal_mutex_create(osal_mutex_t *m)
{
	pthread_mutex_t *mu;
	int rc;

	if (m == NULL)
		return -EINVAL;

	mu = malloc(sizeof(*mu));
	if (mu == NULL)
		return -ENOMEM;

	/* Default (fast, non-recursive) mutex: matches the OSAL contract
	 * "mutexes are not recursive — relocking from the owner is a
	 * deadlock/error", which is what the default attribute gives us. */
	rc = pthread_mutex_init(mu, NULL);
	if (rc != 0) {
		free(mu);
		return -rc;
	}

	*m = mu;
	return OSAL_OK;
}

int osal_mutex_lock(osal_mutex_t m, uint32_t timeout_ms)
{
	pthread_mutex_t *mu = m;
	struct timespec abs;
	int rc;

	if (mu == NULL)
		return -EINVAL;

	if (timeout_ms == OSAL_WAIT_FOREVER) {
		rc = pthread_mutex_lock(mu);
	} else if (timeout_ms == OSAL_NO_WAIT) {
		rc = pthread_mutex_trylock(mu);
		if (rc == EBUSY)
			return -EBUSY; /* OSAL_ERR_BUSY */
	} else {
		osal_posix_deadline(timeout_ms, &abs);
		do {
			rc = pthread_mutex_timedlock(mu, &abs);
		} while (rc == EINTR); /* same absolute deadline, no drift */
		if (rc == ETIMEDOUT)
			return -ETIMEDOUT;
	}

	if (rc != 0)
		return -rc;
	return OSAL_OK;
}

int osal_mutex_unlock(osal_mutex_t m)
{
	pthread_mutex_t *mu = m;
	int rc;

	if (mu == NULL)
		return -EINVAL;

	rc = pthread_mutex_unlock(mu);
	if (rc != 0)
		return -rc;
	return OSAL_OK;
}

int osal_mutex_destroy(osal_mutex_t m)
{
	pthread_mutex_t *mu = m;
	int rc;

	if (mu == NULL)
		return OSAL_OK;

	/* Undefined if the mutex is still locked (contract). */
	rc = pthread_mutex_destroy(mu);
	if (rc != 0)
		return -rc;

	free(mu);
	return OSAL_OK;
}