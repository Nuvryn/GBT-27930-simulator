/*
 * osal_mq.c
 *
 * POSIX backend for OSAL message queue APIs.
 * Maps onto mq_open / mq_send / mq_timedsend / mq_timedreceive / mq_close /
 * mq_unlink (POSIX named message queues). The handle is an opaque void*
 * wrapping a malloc'd osal_posix_mq_t.
 *
 * mqueue cleanup strategy (N2): every queue name embeds the PID plus a
 * per-process sequence number, osal_mq_create() unlinks any stale queue of
 * the same name before mq_open(O_CREAT|O_EXCL), and osal_mq_destroy() calls
 * mq_close() + mq_unlink(). This guarantees a crashed process never leaks a
 * named queue into /dev/mqueue.
 */
#define _POSIX_C_SOURCE 200809L /* expose POSIX.1-2008 APIs under -std=c11 */
#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osal/osal.h"
#include "osal_posix_internal.h"

typedef struct osal_posix_mq {
	mqd_t mq;
	size_t msg_size;
	char *name; /* owns the mqueue name; unlinked on destroy */
} osal_posix_mq_t;

static pthread_mutex_t g_mq_name_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_mq_seq;

/* Generate a process-unique POSIX mqueue name: "/osal_mq_<pid>_<n>". */
static char *osal_posix_mq_name(void)
{
	char buf[64];
	unsigned long n;
	int len;

	(void)pthread_mutex_lock(&g_mq_name_lock);
	n = ++g_mq_seq;
	(void)pthread_mutex_unlock(&g_mq_name_lock);

	len = snprintf(buf, sizeof(buf), "/osal_mq_%ld_%lu",
	               (long)getpid(), n);
	if (len < 0 || (size_t)len >= sizeof(buf))
		return NULL;
	return strdup(buf);
}

int osal_mq_create(osal_mq_t *q, size_t msg_size, size_t max_msgs)
{
	osal_posix_mq_t *mq;
	struct mq_attr attr;
	mqd_t mqd;

	if (q == NULL || msg_size == 0u || max_msgs == 0u)
		return -EINVAL;

	mq = malloc(sizeof(*mq));
	if (mq == NULL)
		return -ENOMEM;

	mq->name = osal_posix_mq_name();
	if (mq->name == NULL) {
		free(mq);
		return -ENOMEM;
	}

	/* N2: clear any stale queue left behind by a crashed process that
	 * shared this name (only possible via PID reuse). */
	(void)mq_unlink(mq->name);

	attr.mq_flags = 0;
	attr.mq_maxmsg = (long)max_msgs;
	attr.mq_msgsize = (long)msg_size;
	attr.mq_curmsgs = 0;

	mqd = mq_open(mq->name, O_CREAT | O_EXCL | O_RDWR, (mode_t)0644, &attr);
	if (mqd == (mqd_t)-1) {
		free(mq->name);
		free(mq);
		return -errno;
	}
	mq->mq = mqd;
	mq->msg_size = msg_size;

	*q = mq;
	return OSAL_OK;
}

int osal_mq_send(osal_mq_t q, const void *msg, uint32_t timeout_ms)
{
	osal_posix_mq_t *mq = q;
	struct timespec abs;
	int rc;

	if (mq == NULL || msg == NULL)
		return -EINVAL;

	if (timeout_ms == OSAL_WAIT_FOREVER) {
		if (mq_send(mq->mq, (const char *)msg, mq->msg_size, 0) == -1)
			return -errno;
		return OSAL_OK;
	}

	osal_posix_deadline(timeout_ms, &abs);
	do {
		rc = mq_timedsend(mq->mq, (const char *)msg, mq->msg_size, 0, &abs);
	} while (rc == -1 && errno == EINTR);
	if (rc == -1) {
		if (errno == ETIMEDOUT || errno == EAGAIN)
			return -ETIMEDOUT;
		return -errno;
	}
	return OSAL_OK;
}

int osal_mq_recv(osal_mq_t q, void *msg, uint32_t timeout_ms)
{
	osal_posix_mq_t *mq = q;
	unsigned int prio = 0u;
	struct timespec abs;
	ssize_t rc;

	if (mq == NULL || msg == NULL)
		return -EINVAL;

	if (timeout_ms == OSAL_WAIT_FOREVER) {
		if (mq_receive(mq->mq, (char *)msg, mq->msg_size, &prio) == -1)
			return -errno;
		return OSAL_OK;
	}

	osal_posix_deadline(timeout_ms, &abs);
	do {
		rc = mq_timedreceive(mq->mq, (char *)msg, mq->msg_size, &prio, &abs);
	} while (rc == -1 && errno == EINTR);
	if (rc == -1) {
		if (errno == ETIMEDOUT || errno == EAGAIN)
			return -ETIMEDOUT;
		return -errno;
	}
	return OSAL_OK;
}

int osal_mq_destroy(osal_mq_t q)
{
	osal_posix_mq_t *mq = q;
	int rc = OSAL_OK;

	if (mq == NULL)
		return OSAL_OK;

	/* N2: never leave the named queue behind; unlink even if close
	 * reports an error. Undefined if tasks are still blocked (contract). */
	if (mq_close(mq->mq) == -1)
		rc = -errno;
	(void)mq_unlink(mq->name);

	free(mq->name);
	free(mq);
	return rc;
}