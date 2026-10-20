/*
 * hal_i2c.c - Linux I2C backend (i2c-dev ioctl).
 *
 * hal_i2c_get_bus("i2cN")       -> open("/dev/i2c-N", O_RDWR)
 * hal_i2c_transfer(bus, m, n)   -> ioctl(fd, I2C_RDWR, &i2c_rdwr_ioctl_data)
 *
 * Field mapping (M2) hal_i2c_msg_t -> struct i2c_msg:
 *   addr          -> .addr (7-bit or 10-bit slave address, passed through)
 *   flags         -> HAL_I2C_RD (0x1) maps 1:1 onto I2C_M_RD (0x1). A Linux
 *                    i2c_msg with flags == 0 is a WRITE, so HAL_I2C_WR is
 *                    encoded as "no I2C_M_RD bit" rather than a flag of its
 *                    own. Linux keeps the bus held between messages of one
 *                    I2C_RDWR batch (repeated start), so HAL_I2C_NO_STOP is
 *                    the default and needs no bit. HAL_I2C_ADDR_10BIT ->
 *                    I2C_M_TEN.
 *   buf           -> .buf
 *   len           -> .len
 *
 * Error codes (M4): libc -1 + errno is converted to -errno. Handles follow
 * C1: void* wrapping `struct { int fd; pthread_mutex_t lock; }`; NULL =
 * not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

/* Minimize our window for snprintf truncation warnings. */
#define I2C_DEV_PATH_MAX HAL_LINUX_PATH_MAX

hal_i2c_bus_t hal_i2c_get_bus(const char *name)
{
	hal_linux_dev_t *d;
	char path[I2C_DEV_PATH_MAX];
	int idx, fd, n;

	if (name == NULL)
		return NULL;

	/* Logical "i2cN" -> /dev/i2c-N (runtime open() discovery, no
	 * hardcoded char device path; the app only names the bus). */
	if (hal_linux_parse_trailing_index(name, &idx) != 0)
		return NULL;
	n = snprintf(path, sizeof(path), "/dev/i2c-%d", idx);
	if (n < 0 || n >= (int)sizeof(path))
		return NULL;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return NULL; /* C1: NULL = not found / not open */

	d = hal_linux_dev_alloc();
	if (d == NULL) {
		(void)close(fd);
		return NULL;
	}
	d->fd = fd;
	return d;
}

int hal_i2c_transfer(hal_i2c_bus_t bus, hal_i2c_msg_t *msgs, uint16_t n)
{
	struct i2c_rdwr_ioctl_data data;
	struct i2c_msg *kmsgs;
	hal_linux_dev_t *d = bus;
	uint16_t i;
	int rc;

	if (d == NULL || msgs == NULL || n == 0)
		return -EINVAL;

	kmsgs = calloc((size_t)n, sizeof(*kmsgs));
	if (kmsgs == NULL)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		const hal_i2c_msg_t *m = &msgs[i];

		if (m->buf == NULL && m->len != 0) {
			free(kmsgs);
			return -EINVAL;
		}

		/* M2 field mapping hal_i2c_msg_t -> struct i2c_msg. */
		kmsgs[i].addr = m->addr;
		kmsgs[i].flags = 0;
		if (m->flags & HAL_I2C_RD)
			kmsgs[i].flags |= I2C_M_RD;   /* same value (0x1) in both ABIs */
		if (m->flags & HAL_I2C_ADDR_10BIT)
			kmsgs[i].flags |= I2C_M_TEN;
		/* HAL_I2C_WR  -> I2C flags left at 0 (Linux "write" default). */
		/* HAL_I2C_NO_STOP -> Linux default (bus held between messages). */
		kmsgs[i].buf = m->buf;
		kmsgs[i].len = m->len;
	}

	data.msgs = kmsgs;
	data.nmsgs = n;

	hal_linux_dev_lock(d);
	rc = ioctl(d->fd, I2C_RDWR, &data);
	hal_linux_dev_unlock(d);

	free(kmsgs);

	if (rc == -1)
		return -errno; /* M4 */
	return HAL_OK;
}

int hal_i2c_read_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
		     uint8_t *buf, uint16_t n)
{
	hal_i2c_msg_t msgs[2];

	if (bus == NULL || (buf == NULL && n != 0))
		return -EINVAL;
	if (n == 0)
		return HAL_OK;

	/* WR|NO_STOP register write followed by an RD message = repeated
	 * start, which is exactly what Linux's I2C_RDWR batch does. */
	msgs[0].addr = dev_addr;
	msgs[0].flags = HAL_I2C_WR | HAL_I2C_NO_STOP;
	msgs[0].buf = (uint8_t *)&reg;
	msgs[0].len = 1;

	msgs[1].addr = dev_addr;
	msgs[1].flags = HAL_I2C_RD;
	msgs[1].buf = buf;
	msgs[1].len = n;

	return hal_i2c_transfer(bus, msgs, 2);
}

int hal_i2c_write_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
		      const uint8_t *buf, uint16_t n)
{
	hal_i2c_msg_t msg;
	uint8_t *payload;
	size_t size;
	int rc;

	if (bus == NULL || (buf == NULL && n != 0))
		return -EINVAL;

	size = (size_t)n + 1u;
	payload = malloc(size);
	if (payload == NULL)
		return -ENOMEM;
	payload[0] = reg;
	if (n > 0)
		memcpy(&payload[1], buf, n);

	msg.addr = dev_addr;
	msg.flags = HAL_I2C_WR;
	msg.buf = payload;
	msg.len = (uint16_t)size;

	rc = hal_i2c_transfer(bus, &msg, 1);
	free(payload);
	return rc;
}