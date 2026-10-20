/*
 * hal_spi.c - Linux SPI backend (spidev ioctl).
 *
 * hal_spi_get_bus("spiN" / "spiN.M") -> open("/dev/spidevN.M", O_RDWR)
 * hal_spi_transfer(bus, m, n)        -> ioctl(fd, SPI_IOC_MESSAGE(n), &xfer)
 *
 * Field mapping (M2) hal_spi_msg_t -> struct spi_ioc_transfer:
 *   tx_buf   -> .tx_buf          rx_buf -> .rx_buf
 *   len      -> .len             speed_hz -> .speed_hz
 *   cs_gpio  -> .cs_change (non-zero cs_gpio toggles CS between segments)
 *   mode     -> CPOL/CPHA encoded as SPI_MODE_0..3. spi_ioc_transfer has no
 *               per-segment mode field; spidev applies mode per device via
 *               the SPI_IOC_WR_MODE ioctl, so the backend caches the device
 *               mode in the handle and re-applies it when a segment's mode
 *               differs from the current one.
 *
 * Error codes (M4): -errno conversion. Handle convention (C1): void *
 * wrapping { fd, mutex } + cached mode; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define SPI_DEV_PATH_MAX HAL_LINUX_PATH_MAX

/* Defaults used by hal_spi_transceive() when the app provides no per-segment
 * settings. */
#define HAL_LINUX_SPI_DEFAULT_SPEED_HZ 1000000u
#define HAL_LINUX_SPI_DEFAULT_MODE     0u /* SPI_MODE_0: CPOL=0, CPHA=0 */

/* 9th byte of the handle: cached device mode for the SPI_IOC_WR_MODE
 * lazily-applied CPOL/CPHA encoding (see file header). */
typedef struct hal_linux_spi_dev {
	hal_linux_dev_t base;
	uint8_t mode; /* currently applied SPI mode (SPI_MODE_x) */
} hal_linux_spi_dev_t;

/* Parse "spiN" or "spiN.M" into bus/chip. Returns 0 on success. */
static int hal_linux_spi_parse_name(const char *name, int *bus, int *chip)
{
	const char *p, *ep;
	long a, b;

	if (name == NULL || bus == NULL || chip == NULL)
		return -EINVAL;

	p = name;
	if (p[0] == 's' && p[1] == 'p' && p[2] == 'i')
		p += 3;
	if (*p < '0' || *p > '9')
		return -EINVAL;

	a = strtol(p, (char **)&ep, 10);
	if (ep == p || a < 0 || a > 255)
		return -EINVAL;

	*bus = (int)a;
	*chip = 0;

	if (*ep == '\0')
		return 0;
	if (*ep != '.')
		return -EINVAL;

	b = strtol(ep + 1, (char **)&ep, 10);
	if (*ep != '\0' || b < 0 || b > 255)
		return -EINVAL;
	*chip = (int)b;
	return 0;
}

hal_spi_bus_t hal_spi_get_bus(const char *name)
{
	hal_linux_spi_dev_t *sd;
	char path[SPI_DEV_PATH_MAX];
	int bus, chip, fd, n;

	if (hal_linux_spi_parse_name(name, &bus, &chip) != 0)
		return NULL;

	n = snprintf(path, sizeof(path), "/dev/spidev%d.%d", bus, chip);
	if (n < 0 || n >= (int)sizeof(path))
		return NULL;

	fd = open(path, O_RDWR);
	if (fd < 0)
		return NULL; /* C1 */

	sd = malloc(sizeof(*sd));
	if (sd == NULL) {
		(void)close(fd);
		return NULL;
	}
	memset(sd, 0, sizeof(*sd));
	if (pthread_mutex_init(&sd->base.lock, NULL) != 0) {
		free(sd);
		(void)close(fd);
		return NULL;
	}
	sd->base.fd = fd;
	sd->mode = 0xFF; /* force first SPI_IOC_WR_MODE application */
	return sd;
}

int hal_spi_transfer(hal_spi_bus_t bus, hal_spi_msg_t *msgs, uint16_t n)
{
	hal_linux_spi_dev_t *sd = bus;
	hal_linux_dev_t *d = bus;
	struct spi_ioc_transfer *t;
	uint16_t i;
	int rc;
	uint8_t mode;

	if (d == NULL || msgs == NULL || n == 0)
		return -EINVAL;

	t = calloc((size_t)n, sizeof(*t));
	if (t == NULL)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		const hal_spi_msg_t *m = &msgs[i];

		if (m->len == 0 || (m->tx_buf == NULL && m->rx_buf == NULL)) {
			free(t);
			return -EINVAL;
		}
		if (m->mode > 3u) {
			free(t);
			return -EINVAL;
		}

		/* M2 mapping hal_spi_msg_t -> struct spi_ioc_transfer */
		t[i].tx_buf = (uint64_t)(uintptr_t)m->tx_buf;
		t[i].rx_buf = (uint64_t)(uintptr_t)m->rx_buf;
		t[i].len = (uint32_t)m->len;
		t[i].speed_hz = m->speed_hz;
		t[i].cs_change = (m->cs_gpio != 0u) ? 1u : 0u;
		t[i].bits_per_word = 8;
		t[i].delay_usecs = 0;
	}

	/* CPOL/CPHA encoding (M2): spidev stores the mode per device; encode
	 * the segment's mode bits by re-programming the device when it
	 * changes. Both ioctls run under the M8 bus lock. */
	hal_linux_dev_lock(d);

	mode = (uint8_t)(msgs[0].mode & 0x03u);
	if (sd != NULL && mode != sd->mode) {
		if (ioctl(d->fd, SPI_IOC_WR_MODE, &mode) == -1) {
			hal_linux_dev_unlock(d);
			free(t);
			return -errno; /* M4 */
		}
		sd->mode = mode;
	}

	rc = ioctl(d->fd, SPI_IOC_MESSAGE(n), t);
	hal_linux_dev_unlock(d);

	free(t);

	if (rc == -1)
		return -errno; /* M4 */
	return HAL_OK;
}

int hal_spi_transceive(hal_spi_bus_t bus, const uint8_t *tx, uint8_t *rx,
		       size_t len)
{
	hal_spi_msg_t m;

	if (bus == NULL || len == 0)
		return -EINVAL;

	m.tx_buf = (uint8_t *)tx;
	m.rx_buf = rx;
	m.len = len;
	m.speed_hz = HAL_LINUX_SPI_DEFAULT_SPEED_HZ;
	m.cs_gpio = 0;
	m.mode = HAL_LINUX_SPI_DEFAULT_MODE;

	return hal_spi_transfer(bus, &m, 1);
}