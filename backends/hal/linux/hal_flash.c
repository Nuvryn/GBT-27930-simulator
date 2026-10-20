/*
 * hal_flash.c - Linux NOR flash backend (/dev/mtd* + MTD ioctls).
 *
 * hal_flash_get_dev("flashN"/"mtdN") -> open("/dev/mtdN", O_RDWR)
 *   (names starting with '/' are used as absolute device paths).
 *
 * Linux MTD data path: reads/writes go through read()/write() on the char
 * device after lseek() (mainline MTD has no MEMREAD/MEMWRITE data ioctls;
 * MEMREADOOB/MEMWRITEOOB are OOB-only). Erase uses the MEMERASE ioctl,
 * geometry uses MEMGETINFO:
 *
 *   hal_flash_read        -> lseek()+read() loop (EINTR-safe)
 *   hal_flash_write       -> lseek()+write() loop (caller must erase first)
 *   hal_flash_erase       -> validate sector alignment, MEMERASE per sector
 *   hal_flash_get_info    -> MEMGETINFO -> sector_size/total_size
 *
 * Erase granularity note: MTD erasesize is the minimum erase unit; the HAL
 * sector size is the MTD erasesize. Writing partial erase blocks is the
 * caller's responsibility (erase first, contract).
 *
 * Error codes (M4): -errno. Handle convention (C1): void * wrapping
 * hal_linux_flash_t; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <mtd/mtd-user.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define FLASH_DEV_PATH_MAX HAL_LINUX_PATH_MAX

typedef struct hal_linux_flash {
	int fd;
	pthread_mutex_t lock;   /* M8-style serialization */
	uint32_t sector_size;
	uint32_t total_size;
	int geometry_valid;
} hal_linux_flash_t;

hal_flash_dev_t hal_flash_get_dev(const char *name)
{
	hal_linux_flash_t *f;
	char path[FLASH_DEV_PATH_MAX];
	int idx, fd, n;

	if (name == NULL)
		return NULL;

	if (name[0] == '/') {
		if (strlen(name) >= sizeof(path))
			return NULL;
		memcpy(path, name, strlen(name) + 1u);
	} else {
		if (hal_linux_parse_trailing_index(name, &idx) != 0)
			return NULL;
		n = snprintf(path, sizeof(path), "/dev/mtd%d", idx);
		if (n < 0 || n >= (int)sizeof(path))
			return NULL;
	}

	fd = open(path, O_RDWR);
	if (fd < 0)
		return NULL; /* C1 */

	f = malloc(sizeof(*f));
	if (f == NULL) {
		(void)close(fd);
		return NULL;
	}
	memset(f, 0, sizeof(*f));
	if (pthread_mutex_init(&f->lock, NULL) != 0) {
		free(f);
		(void)close(fd);
		return NULL;
	}
	f->fd = fd;
	f->geometry_valid = 0;
	return f;
}

int hal_flash_get_info(hal_flash_dev_t dev, uint32_t *sector_size,
		       uint32_t *total_size)
{
	hal_linux_flash_t *f = dev;
	struct mtd_info_user info;
	int rc = 0;

	if (f == NULL)
		return -EINVAL;

	hal_linux_dev_lock((hal_linux_dev_t *)f);
	if (!f->geometry_valid) {
		if (ioctl(f->fd, MEMGETINFO, &info) == -1) {
			rc = -errno;
		} else {
			f->sector_size = info.erasesize;
			f->total_size = info.size;
			f->geometry_valid = 1;
		}
	}
	if (rc == 0) {
		if (sector_size != NULL)
			*sector_size = f->sector_size;
		if (total_size != NULL)
			*total_size = f->total_size;
	}
	hal_linux_dev_unlock((hal_linux_dev_t *)f);
	return rc;
}

int hal_flash_read(hal_flash_dev_t dev, uint32_t addr, uint8_t *buf,
		   size_t len)
{
	hal_linux_flash_t *f = dev;
	size_t got = 0;
	int rc = 0;

	if (f == NULL || (buf == NULL && len != 0))
		return -EINVAL;

	hal_linux_dev_lock((hal_linux_dev_t *)f);

	if (lseek(f->fd, (off_t)addr, SEEK_SET) == (off_t)-1) {
		rc = -errno;
		goto out;
	}
	while (got < len) {
		ssize_t n = read(f->fd, buf + got, len - got);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			rc = -errno;
			goto out;
		}
		if (n == 0) {
			rc = -EIO; /* short read past the end of the device */
			goto out;
		}
		got += (size_t)n;
	}

out:
	hal_linux_dev_unlock((hal_linux_dev_t *)f);
	return rc;
}

int hal_flash_write(hal_flash_dev_t dev, uint32_t addr, const uint8_t *buf,
		    size_t len)
{
	hal_linux_flash_t *f = dev;
	size_t written = 0;
	int rc = 0;

	if (f == NULL || (buf == NULL && len != 0))
		return -EINVAL;

	hal_linux_dev_lock((hal_linux_dev_t *)f);

	if (lseek(f->fd, (off_t)addr, SEEK_SET) == (off_t)-1) {
		rc = -errno;
		goto out;
	}
	while (written < len) {
		ssize_t n = write(f->fd, buf + written, len - written);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			rc = -errno;
			goto out;
		}
		if (n == 0) {
			rc = -EIO;
			goto out;
		}
		written += (size_t)n;
	}

out:
	hal_linux_dev_unlock((hal_linux_dev_t *)f);
	return rc;
}

int hal_flash_erase(hal_flash_dev_t dev, uint32_t addr, size_t len)
{
	hal_linux_flash_t *f = dev;
	struct erase_info_user ei;
	uint32_t pos, end;

	if (f == NULL || len == 0)
		return -EINVAL;

	hal_linux_dev_lock((hal_linux_dev_t *)f);

	if (!f->geometry_valid) {
		struct mtd_info_user info;

		if (ioctl(f->fd, MEMGETINFO, &info) == -1) {
			hal_linux_dev_unlock((hal_linux_dev_t *)f);
			return -errno;
		}
		f->sector_size = info.erasesize;
		f->total_size = info.size;
		f->geometry_valid = 1;
	}

	/* Contract: range must be sector-aligned. */
	if (f->sector_size == 0 ||
	    (uint64_t)addr % f->sector_size != 0 ||
	    (uint64_t)len % f->sector_size != 0) {
		hal_linux_dev_unlock((hal_linux_dev_t *)f);
		return -EINVAL;
	}
	if ((uint64_t)addr + len > f->total_size) {
		hal_linux_dev_unlock((hal_linux_dev_t *)f);
		return -EINVAL;
	}

	pos = addr;
	end = (uint32_t)(addr + len);
	while (pos < end) {
		ei.start = pos;
		ei.length = f->sector_size;
		if (ioctl(f->fd, MEMERASE, &ei) == -1) {
			int rc = -errno;

			hal_linux_dev_unlock((hal_linux_dev_t *)f);
			return rc;
		}
		pos += f->sector_size;
	}

	hal_linux_dev_unlock((hal_linux_dev_t *)f);
	return 0;
}