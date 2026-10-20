/*
 * osal_sem.c
 *
 * POSIX backend for OSAL counting semaphore APIs.
 * Maps onto sem_init / sem_trywait / sem_timedwait / sem_wait / sem_post /
 * sem_destroy (unnamed semaphore, process-local). The handle is an opaque
 * void* wrapping a malloc'd osal_posix_sem_t.
 *
 * POSIX sems have no maximum count, so the osal max_count cap is enforced
 * with an internal counter + guard mutex: post() is a no-op at the cap.
 */
#define _POSIX_C_SOURCE 200809L /* expose POSIX.1-2008 APIs under -std=c11 */
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>
#include "osal/osal.h"
#include "osal_posix_internal.h"

typedef struct osal_posix_sem {
	sem_t sem;
	pthread_mutex_t cap_lock; /* guards cap_count only */
	size_t cap_count;         /* current logical count */
	size_t max_count;         /* cap enforced by post() */
} osal_posix_sem_t;

int osal_sem_create(osal_sem_t *s, uint32_t init_count, uint32_t max_count)
{
	osal_posix_sem_t *sem;
	int rc;

	if (s == NULL || max_count < 1u || init_count > max_count)
		return -EINVAL;

	sem = malloc(sizeof(*sem));
	if (sem == NULL)
		return -ENOMEM;

	rc = sem_init(&sem->sem, 0, (unsigned int)init_count);
	if (rc != 0) {
		rc = errno;
		free(sem);
		return -rc;
	}
	rc = pthread_mutex_init(&sem->cap_lock, NULL);
	if (rc != 0) {
		(void)sem_destroy(&sem->sem);
		free(sem);
		return -rc;
	}
	sem->cap_count = init_count;
	sem->max_count = max_count;

	*s = sem;
	return OSAL_OK;
}

static int osal_posix_sem_acquire(osal_posix_sem_t *sem)
{
	int rc = 0;

	if (pthread_mutex_lock(&sem->cap_lock) != 0)
		return -EINVAL; /* cannot happen with a valid semaphore */
	if (sem->cap_count > 0u)
		sem->cap_count--;
	/* Underflow must never happen: a successful sem_wait below was
	 * preceded by a post(), and cap_count is incremented before every
	 * sem_post(). */
	(void)pthread_mutex_unlock(&sem->cap_lock);
	return rc;
}

int osal_sem_wait(osal_sem_t s, uint32_t timeout_ms)
{
	osal_posix_sem_t *sem = s;
	struct timespec abs;
	int rc;

	if (sem == NULL)
		return -EINVAL;

	if (timeout_ms == OSAL_WAIT_FOREVER) {
		if (sem_wait(&sem->sem) == -1)
			return -errno;
	} else if (timeout_ms == OSAL_NO_WAIT) {
		if (sem_trywait(&sem->sem) == -1)
			return (errno == EAGAIN) ? -ETIMEDOUT : -errno;
	} else {
		osal_posix_deadline(timeout_ms, &abs);
		do {
			rc = sem_timedwait(&sem->sem, &abs);
		} while (rc == -1 && errno == EINTR);
		if (rc == -1) {
			if (errno == ETIMEDOUT)
				return -ETIMEDOUT;
			return -errno;
		}
	}

	return osal_posix_sem_acquire(sem);
}

int osal_sem_post(osal_sem_t s)
{
	osal_posix_sem_t *sem = s;

	if (sem == NULL)
		return -EINVAL;

	/* Cap at max_count: above the cap, post() is a no-op (contract). */
	if (pthread_mutex_lock(&sem->cap_lock) != 0)
		return -EINVAL;
	if (sem->cap_count < sem->max_count) {
		sem->cap_count++;
		if (sem_post(&sem->sem) == -1) {
			(void)pthread_mutex_unlock(&sem->cap_lock);
			return -errno;
		}
	}
	(void)pthread_mutex_unlock(&sem->cap_lock);
	return OSAL_OK;
}

int osal_sem_destroy(osal_sem_t s)
{
	osal_posix_sem_t *sem = s;

	if (sem == NULL)
		return OSAL_OK;

	/* Undefined if tasks are still blocked on the semaphore (contract). */
	(void)sem_destroy(&sem->sem);
	(void)pthread_mutex_destroy(&sem->cap_lock);
	free(sem);
	return OSAL_OK;
}