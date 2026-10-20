/*
 * osal_task.c
 *
 * POSIX backend for OSAL task management + delay/time APIs.
 * Maps onto pthread_create / pthread_cancel / sched_yield / nanosleep /
 * clock_gettime(CLOCK_MONOTONIC).
 */
#define _POSIX_C_SOURCE 200809L /* expose POSIX.1-2008 APIs under -std=c11 */
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "osal/osal.h"
#include "osal_posix_internal.h"

/* SCHED_FIFO valid range on Linux is 1..99. OSAL abstract prio 0 = lowest. */
#define OSAL_POSIX_FIFO_MAX 99

/*
 * Per-task record. Returned to the app as an opaque osal_task_t.
 *
 * Entry-signature adaptation (POSIX wants void *(*)(void *), OSAL provides
 * void (*)(void *)): osal_posix_run() is the pthread entry, it unwraps and
 * calls t->entry(t->arg), then returns NULL.
 *
 * Lifecycle: threads are created detached, so the OS reclaims the thread
 * when it exits. When it exits naturally, osal_posix_run() releases the
 * record itself. osal_task_delete() (documented as "not graceful, prefer
 * natural exit") cancels and releases the record; the v1 contract does not
 * return the handle from osal_task_create(), so delete is best treated as
 * unreachable from application code.
 */
typedef struct osal_posix_task {
	char *name;                /* strdup'd copy, may be NULL */
	void (*entry)(void *arg);  /* OSAL entry signature */
	void *arg;                 /* user argument */
	pthread_t thread;
} osal_posix_task_t;

static void *osal_posix_run(void *arg)
{
	osal_posix_task_t *t = arg;

	t->entry(t->arg);

	/* Natural exit: release the record (thread is detached, OS reclaims
	 * the thread itself). */
	free(t->name);
	free(t);
	return NULL;
}

int osal_task_create(const osal_task_attr_t *attr)
{
	osal_posix_task_t *t;
	pthread_attr_t pa;
	struct sched_param prio;
	int fifo_prio;
	int rc;

	if (attr == NULL || attr->entry == NULL || attr->stack_size == 0)
		return -EINVAL;

	t = malloc(sizeof(*t));
	if (t == NULL)
		return -ENOMEM;

	t->name = NULL;
	if (attr->name != NULL) {
		t->name = strdup(attr->name);
		if (t->name == NULL) {
			free(t);
			return -ENOMEM;
		}
	}
	t->entry = attr->entry;
	t->arg = attr->arg;

	pthread_attr_init(&pa);
	pthread_attr_setstacksize(&pa, (size_t)attr->stack_size);

	/* M6: abstract OSAL prio (0 = lowest) maps to SCHED_FIFO 1..99.
	 * SCHED_FIFO requires CAP_SYS_NICE (root); pthread_create then fails
	 * with EPERM for non-root users and we retry with the backend default
	 * policy (SCHED_OTHER). */
	fifo_prio = (int)attr->prio + 1;
	if (fifo_prio < 1)
		fifo_prio = 1;
	if (fifo_prio > OSAL_POSIX_FIFO_MAX)
		fifo_prio = OSAL_POSIX_FIFO_MAX;
	prio.sched_priority = fifo_prio;
	if (pthread_attr_setschedpolicy(&pa, SCHED_FIFO) == 0 &&
	    pthread_attr_setschedparam(&pa, &prio) == 0)
		(void)pthread_attr_setinheritsched(&pa, PTHREAD_EXPLICIT_SCHED);

	rc = pthread_create(&t->thread, &pa, osal_posix_run, t);
	if (rc != 0 && (rc == EPERM || rc == EACCES)) {
		/* Non-root degradation (M6): retry with default policy. */
		pthread_attr_destroy(&pa);
		pthread_attr_init(&pa);
		pthread_attr_setstacksize(&pa, (size_t)attr->stack_size);
		rc = pthread_create(&t->thread, &pa, osal_posix_run, t);
	}
	pthread_attr_destroy(&pa);

	if (rc != 0) {
		free(t->name);
		free(t);
		return -rc;
	}

	/* Detached: resources reclaimed by the OS when the thread exits;
	 * no pthread_join() is ever needed. */
	(void)pthread_detach(t->thread);

	return OSAL_OK;
}

void osal_task_delete(osal_task_t task)
{
	osal_posix_task_t *t = task;

	if (t == NULL)
		return;

	/* NOT graceful (documented): pthread_cancel() is deferred, so it only
	 * takes effect once the target reaches a cancellation point (a
	 * blocking call such as nanosleep/sem_wait). A CPU-bound loop that
	 * never blocks cannot be interrupted. Prefer natural task exit. */
	(void)pthread_cancel(t->thread);
	free(t->name);
	free(t);
}

void osal_task_yield(void)
{
	(void)sched_yield();
}

void osal_delay_ms(uint32_t ms)
{
	struct timespec req, rem;

	if (ms == 0) {
		/* Contract: 0 ms means a single yield. */
		(void)sched_yield();
		return;
	}

	req.tv_sec = (time_t)(ms / 1000u);
	req.tv_nsec = (long)(ms % 1000u) * 1000000L;

	/* Restart on EINTR (signal delivery), as nanosleep requires. */
	while (nanosleep(&req, &rem) == -1 && errno == EINTR)
		req = rem;
}

void osal_delay_us(uint32_t us)
{
	struct timespec now, end;
	long long ns;

	/* k_busy_wait semantics: busy-wait, does NOT yield the CPU. Used only
	 * for short, time-critical delays. */
	(void)clock_gettime(CLOCK_MONOTONIC, &end);
	ns = (long long)end.tv_nsec + (long long)us * 1000LL;
	end.tv_sec += (time_t)(ns / 1000000000LL);
	end.tv_nsec = (long)(ns % 1000000000LL);

	do {
		(void)clock_gettime(CLOCK_MONOTONIC, &now);
	} while (now.tv_sec < end.tv_sec ||
	         (now.tv_sec == end.tv_sec && now.tv_nsec < end.tv_nsec));
}

uint32_t osal_get_tick_ms(void)
{
	struct timespec ts;

	(void)clock_gettime(CLOCK_MONOTONIC, &ts);

	/* Monotonic counter; wraps after ~49.7 days, which is fine for the
	 * relative-delta use case the contract promises. */
	return (uint32_t)(ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L);
}