/*
 * hal_rng.c - Linux RNG backend (/dev/urandom).
 *
 * hal_rng_get_dev(name) -> returns a static handle (the framework has a
 *   single kernel RNG source); any logical RNG name maps to it.
 * hal_rng_read(dev, buf, len) -> read() loop from /dev/urandom (lazily
 *   opened on first use, serialized by a mutex). The buffer is filled
 *   completely before returning (no short reads), per the contract.
 *
 * getrandom(2) was considered but /dev/urandom is equally non-blocking for
 * CRNG-seeded output and keeps the backend to plain POSIX I/O.
 *
 * Error codes (M4): -errno. Handle convention (C1): void * pointing at a
 * static process-wide instance; NULL = invalid name-arg only (the source
 * itself never fails to exist).
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

typedef struct hal_linux_rng {
	int fd;              /* /dev/urandom fd, -1 before first use */
	pthread_mutex_t lock;
} hal_linux_rng_t;

/* Single process-wide RNG source; the static mutex initializer is valid
 * regardless of stack/heap placement. */
static hal_linux_rng_t g_rng = {
	.fd = -1,
	.lock = PTHREAD_MUTEX_INITIALIZER,
};

hal_rng_dev_t hal_rng_get_dev(const char *name)
{
	if (name == NULL || name[0] == '\0')
		return NULL;
	/* Single-source backend: any logical name (e.g. "rng0", "trng",
	 * "hwrng") resolves to the same kernel entropy source. */
	return &g_rng;
}

int hal_rng_read(hal_rng_dev_t dev, uint8_t *buf, size_t len)
{
	hal_linux_rng_t *r = dev;
	size_t total = 0;
	int rc = 0;

	if (r == NULL || (buf == NULL && len != 0))
		return -EINVAL;
	if (len == 0)
		return 0;

	(void)pthread_mutex_lock(&r->lock);

	if (r->fd < 0) {
		r->fd = open("/dev/urandom", O_RDONLY);
		if (r->fd < 0) {
			rc = -errno;
			goto out;
		}
	}

	while (total < len) {
		ssize_t n = read(r->fd, buf + total, len - total);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			rc = -errno;
			goto out;
		}
		if (n == 0) {
			rc = -EIO; /* urandom must never EOF unexpectedly */
			goto out;
		}
		total += (size_t)n;
	}

out:
	(void)pthread_mutex_unlock(&r->lock);
	return rc;
}