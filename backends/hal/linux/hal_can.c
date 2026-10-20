/*
 * hal_can.c - Linux CAN backend (SocketCAN, C3 medium-complexity adapter).
 *
 * NOT a thin shell. SocketCAN sockets are blocking kernel objects, while the
 * HAL contract is callback-based async (matching Zephyr's can_send /
 * can_add_rx_filter). This backend therefore runs a background pthread
 * worker that owns the socket's event loop:
 *
 *   hal_can_get_bus("canX") -> socket(AF_CAN, SOCK_RAW, CAN_RAW) +
 *                              if_nametoindex("canX") + bind(ifindex) +
 *                              set O_NONBLOCK.
 *   hal_can_add_rx_filter   -> append {id, mask, cb, user_data} to an
 *                              internal filter list (mutex-guarded).
 *   hal_can_remove_filter   -> remove from the list.
 *   worker thread (started on first get_bus / re-started by add/send):
 *       poll(fd, POLLIN|POLLOUT, 500ms)
 *         . POLLIN  -> recvfrom(MSG_DONTWAIT), match frame id against the
 *                      filter list, dispatch matching callbacks.
 *         . POLLOUT -> flush the deferred TX frame, then invoke done_cb.
 *         . idle (no filters AND no deferred TX) -> self-exit so the bus
 *                      has no background thread when unused.
 *   hal_can_send          -> non-blocking sendto(MSG_DONTWAIT). If the
 *                            socket TX buffer accepts it, done_cb(arg, 0) is
 *                            invoked immediately. If it returns EAGAIN the
 *                            frame is deferred to the worker, which flushes
 *                            it on POLLOUT (or times it out) and invokes
 *                            done_cb from the worker context. An osal_sem is
 *                            used for the async-completion signal; with
 *                            done_cb == NULL the call blocks on that sem
 *                            (synchronous fallback the contract allows).
 *   hal_can_get_state     -> always HAL_CAN_STATE_ERROR_ACTIVE (v1
 *                            simplification: default CAN_RAW_ERR_FILTER = 0
 *                            means error frames never reach the socket).
 *
 * M8: all bus state is accessed under the per-bus pthread mutex. Callbacks
 * are ALWAYS invoked with the mutex released (the app may call back into
 * the HAL from the callback).
 *
 * Error codes (M4): -errno. Handle convention (C1): void* wrapping
 * hal_linux_can_t; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "osal/osal.h"
#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define HAL_LINUX_CAN_MAX_FILTERS 16
/* Deviation #4 (see DEVIATIONS.md): 500 -> 25 ms.
 * A synchronous hal_can_send (done_cb == NULL) waits at most the caller's
 * timeout_ms (the project adapter passes CAN_BUS_TX_TIMEOUT_MS = 100 ms). The
 * worker only evaluates a deferred-TX deadline at the top of its poll loop, so
 * the poll cadence MUST stay comfortably below that wait: otherwise the waiter
 * can time out, read a stale tx.last_status (usually 0) and report success while
 * the frame is still pending - and the next frame then gets -EAGAIN and is
 * dropped, breaking the "never ahead of the hardware" ordering guarantee. */
#define HAL_LINUX_CAN_POLL_TIMEOUT_MS 25
_Static_assert(HAL_LINUX_CAN_POLL_TIMEOUT_MS <= 50,
               "poll cadence must stay well below the synchronous TX wait (CAN_BUS_TX_TIMEOUT_MS)");

typedef struct hal_linux_can_filter {
	uint32_t id;               /* filter id (HAL logical 11/29-bit)     */
	uint32_t mask;             /* acceptance mask (0 = accept all)      */
	hal_can_rx_cb_t cb;        /* callback                             */
	void *user_data;           /* opaque argument for cb               */
	int in_use;
} hal_linux_can_filter_t;

/* Completion record: what to do once a TX finishes. Collected under the
 * bus lock, acted on after it is released. */
typedef struct hal_linux_can_complete {
	hal_can_done_cb_t cb;
	void *arg;
	int status;
	int sync; /* 1 = a synchronous send is blocked on done_sem */
} hal_linux_can_complete_t;

typedef struct hal_linux_can {
	pthread_mutex_t lock;
	int fd;                        /* SocketCAN socket, -1 = closed  */
	int ifindex;
	char ifname[IF_NAMESIZE];

	hal_linux_can_filter_t filters[HAL_LINUX_CAN_MAX_FILTERS];

	osal_sem_t done_sem;           /* async-completion semaphore (C3) */

	int worker_started;            /* guarded by lock               */
	volatile int worker_stop;      /* set to stop the worker        */
	pthread_t worker;

	/* One-deep deferred TX slot (v1 simplification). */
	struct {
		volatile int pending;    /* frame awaits a POLLOUT flush   */
		struct can_frame frame;
		hal_can_done_cb_t done_cb;
		void *cb_arg;
		int sync_wait;           /* send() blocks on done_sem     */
		int last_status;         /* status of the latest flush    */
		uint32_t deadline_tick;  /* TX timeout deadline (tick ms) */
	} tx;
} hal_linux_can_t;

static int hal_linux_can_has_filters(const hal_linux_can_t *can)
{
	int i;

	for (i = 0; i < HAL_LINUX_CAN_MAX_FILTERS; i++) {
		if (can->filters[i].in_use)
			return 1;
	}
	return 0;
}

/* Invoke the completion (post sem for synchronous waits, call cb for
 * asynchronous callers). Caller must NOT hold the bus lock. */
static void hal_linux_can_finish(hal_linux_can_t *can,
				 const hal_linux_can_complete_t *done)
{
	if (done == NULL)
		return;
	if (done->sync)
		(void)osal_sem_post(can->done_sem);
	if (done->cb != NULL)
		done->cb(done->arg, done->status);
	(void)can;
}

/* Flush the deferred TX frame. Called from the worker on POLLOUT. */
static void hal_linux_can_flush_tx(hal_linux_can_t *can)
{
	hal_linux_can_complete_t done;
	struct sockaddr_can addr;
	ssize_t s;

	memset(&done, 0, sizeof(done));

	pthread_mutex_lock(&can->lock);
	if (!can->tx.pending) {
		pthread_mutex_unlock(&can->lock);
		return;
	}

	addr.can_family = AF_CAN;
	addr.can_ifindex = can->ifindex;
	s = sendto(can->fd, &can->tx.frame, sizeof(can->tx.frame),
		   MSG_DONTWAIT, (const struct sockaddr *)&addr, sizeof(addr));

	if (s == (ssize_t)sizeof(can->tx.frame)) {
		can->tx.last_status = 0;
		done.cb = can->tx.done_cb;
		done.arg = can->tx.cb_arg;
		done.sync = can->tx.sync_wait;
		can->tx.pending = 0;
	} else if (s < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
		can->tx.last_status = -errno;
		done.cb = can->tx.done_cb;
		done.arg = can->tx.cb_arg;
		done.sync = can->tx.sync_wait;
		done.status = can->tx.last_status;
		can->tx.pending = 0;
	} else {
		/* Still EAGAIN: keep the frame for the next POLLOUT. */
		pthread_mutex_unlock(&can->lock);
		return;
	}

	pthread_mutex_unlock(&can->lock);
	hal_linux_can_finish(can, &done);
}

/* Receive all pending frames and dispatch them to matching filters.
 * Callbacks run with the bus lock released. */
static void hal_linux_can_drain_rx(hal_linux_can_t *can)
{
	hal_can_rx_cb_t cbs[HAL_LINUX_CAN_MAX_FILTERS];
	void *args[HAL_LINUX_CAN_MAX_FILTERS];
	struct can_frame frame;
	hal_can_msg_t m;
	int ncb, i;
	ssize_t n;

	for (;;) {
		n = recvfrom(can->fd, &frame, sizeof(frame), MSG_DONTWAIT,
			     NULL, NULL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;	/* EAGAIN: drained */
		}
		if ((size_t)n < sizeof(frame))
			continue;	/* short/truncated: ignore */

		/* v1 simplification: error frames are never registered
		 * (default CAN_RAW_ERR_FILTER = 0), so none arrive. */
		if (frame.can_id & CAN_ERR_FLAG)
			continue;

		m.flags = 0;
		if (frame.can_id & CAN_EFF_FLAG) {
			m.id = frame.can_id & CAN_EFF_MASK;
			m.flags |= HAL_CAN_EXT_ID;
		} else {
			m.id = frame.can_id & CAN_SFF_MASK;
		}
		if (frame.can_id & CAN_RTR_FLAG)
			m.flags |= HAL_CAN_RTR;

		m.len = frame.len;
		if (m.flags & HAL_CAN_RTR)
			m.len = 0;
		if (m.len > CAN_MAX_DLEN)
			m.len = CAN_MAX_DLEN;
		memset(m.data, 0, sizeof(m.data));
		if (m.len > 0)
			memcpy(m.data, frame.data, (size_t)m.len);

		/* Match against the filter list: (frame_id & mask) ==
		 * (filter.id & mask), same semantics as a kernel
		 * CAN_RAW_FILTER. */
		pthread_mutex_lock(&can->lock);
		ncb = 0;
		for (i = 0; i < HAL_LINUX_CAN_MAX_FILTERS; i++) {
			const hal_linux_can_filter_t *f = &can->filters[i];

			if (f->in_use && ((m.id & f->mask) == (f->id & f->mask))) {
				cbs[ncb] = f->cb;
				args[ncb] = f->user_data;
				ncb++;
			}
		}
		pthread_mutex_unlock(&can->lock);

		for (i = 0; i < ncb; i++)
			cbs[i](args[i], &m);
	}
}

static void *hal_linux_can_worker(void *arg)
{
	hal_linux_can_t *can = arg;
	hal_linux_can_complete_t done;
	struct pollfd pfd;
	int rc;

	while (!can->worker_stop) {
		pthread_mutex_lock(&can->lock);

		if (can->fd < 0) {
			pthread_mutex_unlock(&can->lock);
			break;
		}

		/* Deferred TX deadline: the frame was never accepted by the
		 * socket within its TX timeout. Drop it and complete. */
		if (can->tx.pending &&
		    (int32_t)(osal_get_tick_ms() - can->tx.deadline_tick) >= 0) {
			can->tx.last_status = -ETIMEDOUT;
			done.cb = can->tx.done_cb;
			done.arg = can->tx.cb_arg;
			done.sync = can->tx.sync_wait;
			done.status = -ETIMEDOUT;
			can->tx.pending = 0;
			pthread_mutex_unlock(&can->lock);
			hal_linux_can_finish(can, &done);
			continue;
		}

		/* Idle self-exit: no RX filters and no deferred TX. The
		 * worker restarts on the next add_rx_filter()/send(). */
		if (!hal_linux_can_has_filters(can) && !can->tx.pending) {
			can->worker_started = 0;
			pthread_mutex_unlock(&can->lock);
			break;
		}

		pthread_mutex_unlock(&can->lock);

		pfd.fd = can->fd;
		pfd.events = POLLIN;
		if (can->tx.pending)
			pfd.events |= POLLOUT;

		do {
			rc = poll(&pfd, 1, HAL_LINUX_CAN_POLL_TIMEOUT_MS);
		} while (rc == -1 && errno == EINTR);
		if (rc <= 0)
			continue;	/* timeout or poll error: loop */

		if (pfd.revents & POLLOUT)
			hal_linux_can_flush_tx(can);
		if (pfd.revents & POLLIN)
			hal_linux_can_drain_rx(can);

		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			/* Cannot classify a controller fault from userspace
			 * (error frames are filtered out by default):
			 * v1 simplification, see hal_can_get_state(). */
			continue;
		}
	}
	return NULL;
}

/* Start the worker thread if it is not running. Returns 0 or -errno. */
static int hal_linux_can_ensure_worker(hal_linux_can_t *can)
{
	int rc = 0;

	pthread_mutex_lock(&can->lock);
	if (!can->worker_started && can->fd >= 0) {
		can->worker_stop = 0;
		rc = pthread_create(&can->worker, NULL, hal_linux_can_worker,
				    can);
		if (rc == 0) {
			can->worker_started = 1;
			(void)pthread_detach(can->worker);
			rc = 0;
		}
	}
	pthread_mutex_unlock(&can->lock);

	return (rc == 0) ? 0 : -rc;
}

hal_can_bus_t hal_can_get_bus(const char *name)
{
	hal_linux_can_t *can = NULL;
	struct sockaddr_can addr;
	unsigned int ifindex;
	long flags;
	int fd;

	if (name == NULL || name[0] == '\0' || strlen(name) >= IF_NAMESIZE)
		return NULL;

	/* Runtime discovery: resolve the interface to its ifindex (the
	 * interface must actually exist on this system), then open the
	 * SocketCAN raw socket and bind it. No hardcoded device path. */
	ifindex = if_nametoindex(name);
	if (ifindex == 0)
		return NULL;

	fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
	if (fd < 0)
		return NULL;

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = (int)ifindex;
	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
		(void)close(fd);
		return NULL;
	}

	/* Non-blocking mode: the worker's poll() decides read/write
	 * readiness; all recvfrom/sendto use MSG_DONTWAIT. */
	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
		(void)close(fd);
		return NULL;
	}

	can = calloc(1, sizeof(*can));
	if (can == NULL) {
		(void)close(fd);
		return NULL;
	}
	(void)pthread_mutex_init(&can->lock, NULL);
	can->fd = fd;
	can->ifindex = (int)ifindex;
	(void)snprintf(can->ifname, sizeof(can->ifname), "%s", name);

	/* Async-completion semaphore (C3): signalled when a deferred TX
	 * flush finishes. Binary; one outstanding deferred frame max. */
	if (osal_sem_create(&can->done_sem, 0, 1) != 0) {
		(void)pthread_mutex_destroy(&can->lock);
		(void)close(fd);
		free(can);
		return NULL;
	}

	/* Worker starts on first get_bus (spec). */
	if (hal_linux_can_ensure_worker(can) != 0) {
		(void)osal_sem_destroy(can->done_sem);
		(void)pthread_mutex_destroy(&can->lock);
		(void)close(fd);
		free(can);
		return NULL;
	}

	return can;
}

int hal_can_send(hal_can_bus_t bus, const hal_can_msg_t *msg,
		 uint32_t timeout_ms, hal_can_done_cb_t done_cb, void *cb_arg)
{
	hal_linux_can_t *can = bus;
	hal_linux_can_complete_t done;
	struct sockaddr_can addr;
	struct can_frame frame;
	ssize_t s;
	int rc = 0;

	if (can == NULL || msg == NULL)
		return -EINVAL;
	if (msg->len > CAN_MAX_DLEN)
		return -EINVAL;
	if ((msg->flags & HAL_CAN_EXT_ID) ? (msg->id > CAN_EFF_MASK)
					   : (msg->id > CAN_SFF_MASK))
		return -EINVAL;

	memset(&frame, 0, sizeof(frame));
	if (msg->flags & HAL_CAN_EXT_ID) {
		frame.can_id = (msg->id & CAN_EFF_MASK) | CAN_EFF_FLAG;
	} else {
		frame.can_id = msg->id & CAN_SFF_MASK;
	}
	if (msg->flags & HAL_CAN_RTR)
		frame.can_id |= CAN_RTR_FLAG;
	frame.len = msg->len;
	if (!(msg->flags & HAL_CAN_RTR) && msg->len > 0)
		memcpy(frame.data, msg->data, (size_t)msg->len);

	memset(&done, 0, sizeof(done));

	pthread_mutex_lock(&can->lock);
	if (can->fd < 0) {
		pthread_mutex_unlock(&can->lock);
		return -ENODEV;
	}

	if (can->tx.pending) {
		/* One-deep deferred-TX slot already occupied: v1
		 * simplification, report busy instead of queueing. */
		rc = -EAGAIN;
		done.cb = done_cb;
		done.arg = cb_arg;
		done.status = -EAGAIN;
	} else {
		addr.can_family = AF_CAN;
		addr.can_ifindex = can->ifindex;
		s = sendto(can->fd, &frame, sizeof(frame), MSG_DONTWAIT,
			   (const struct sockaddr *)&addr, sizeof(addr));

		if (s == (ssize_t)sizeof(frame)) {
			/* Accepted immediately: async completion. */
			done.cb = done_cb;
			done.arg = cb_arg;
			done.status = 0;
			done.sync = 0;
			rc = 0;
		} else if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			/* Socket TX buffer full: defer to the worker, which
			 * polls POLLOUT and flushes within timeout_ms. */
			if (timeout_ms == 0) {
				rc = -EAGAIN;
				done.cb = done_cb;
				done.arg = cb_arg;
				done.status = -EAGAIN;
			} else {
				can->tx.pending = 1;
				can->tx.frame = frame;
				can->tx.done_cb = done_cb;
				can->tx.cb_arg = cb_arg;
				can->tx.sync_wait = (done_cb == NULL) ? 1 : 0;
				can->tx.deadline_tick =
				    (uint32_t)(osal_get_tick_ms() + timeout_ms);
				rc = 0; /* queued; completion follows */
			}
		} else {
			rc = -errno;
			done.cb = done_cb;
			done.arg = cb_arg;
			done.status = rc;
		}
	}
	pthread_mutex_unlock(&can->lock);

	/* Immediate (non-deferred) completion path. */
	if (done.cb != NULL && can->tx.pending == 0) {
		hal_linux_can_finish(can, &done);
		return rc;
	}

	if (rc == 0 && can->tx.pending) {
		/* Make sure the worker is running to flush the frame. */
		(void)hal_linux_can_ensure_worker(can);

		if (done_cb == NULL) {
			int status;

			/* Synchronous fallback (contract allows backends that
			 * only support synchronous sends): wait for the flush
			 * on the async-completion semaphore, then report the
			 * flush outcome. */
			(void)osal_sem_wait(can->done_sem, timeout_ms);
			pthread_mutex_lock(&can->lock);
			status = can->tx.last_status;
			pthread_mutex_unlock(&can->lock);
			return status;
		}
	}
	return rc;
}

int hal_can_add_rx_filter(hal_can_bus_t bus, const hal_can_filter_t *filter,
			  hal_can_rx_cb_t cb, void *user_data)
{
	hal_linux_can_t *can = bus;
	int slot = -1, i, rc;

	if (can == NULL || cb == NULL)
		return -EINVAL;

	pthread_mutex_lock(&can->lock);
	if (can->fd < 0) {
		pthread_mutex_unlock(&can->lock);
		return -ENODEV;
	}
	for (i = 0; i < HAL_LINUX_CAN_MAX_FILTERS; i++) {
		if (!can->filters[i].in_use) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		pthread_mutex_unlock(&can->lock);
		return -ENOSPC;
	}

	if (filter != NULL) {
		can->filters[slot].id = filter->id;
		can->filters[slot].mask = filter->mask;
	} else {
		can->filters[slot].id = 0;
		can->filters[slot].mask = 0; /* accept all */
	}
	can->filters[slot].cb = cb;
	can->filters[slot].user_data = user_data;
	can->filters[slot].in_use = 1;
	pthread_mutex_unlock(&can->lock);

	/* Filter id delivered to the app == slot index (>= 0). */
	rc = hal_linux_can_ensure_worker(can);
	if (rc != 0) {
		pthread_mutex_lock(&can->lock);
		can->filters[slot].in_use = 0;
		pthread_mutex_unlock(&can->lock);
		return rc;
	}
	return slot;
}

int hal_can_remove_filter(hal_can_bus_t bus, int filter_id)
{
	hal_linux_can_t *can = bus;
	int rc = 0;

	if (can == NULL || filter_id < 0 ||
	    filter_id >= HAL_LINUX_CAN_MAX_FILTERS || !can->filters[filter_id].in_use)
		return -EINVAL;

	pthread_mutex_lock(&can->lock);
	can->filters[filter_id].in_use = 0;
	pthread_mutex_unlock(&can->lock);

	/* The worker self-exits once the last filter is gone and no TX is
	 * pending; nothing to do explicitly here. */
	(void)rc;
	return 0;
}

int hal_can_get_state(hal_can_bus_t bus, hal_can_state_t *state)
{
	hal_linux_can_t *can = bus;

	if (can == NULL || state == NULL)
		return -EINVAL;

	/* v1 simplification (C3 MUST-NOT, no error-frame recovery): the
	 * socket keeps the default CAN_RAW_ERR_FILTER = 0, so error frames
	 * never reach userspace and the controller state is not observable
	 * without root/can-utils. Always report ERROR_ACTIVE. */
	*state = HAL_CAN_STATE_ERROR_ACTIVE;
	return 0;
}