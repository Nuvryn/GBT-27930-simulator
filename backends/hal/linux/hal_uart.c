/*
 * hal_uart.c - Linux UART backend (termios + poll + read/write).
 *
 * hal_uart_get_bus("uartN")       -> open("/dev/ttySN", O_RDWR)
 *   - names starting with '/' are used as absolute device paths (runtime
 *     discovery, sanity-check #2), e.g. "/dev/ttyACM0".
 *   - names starting with "tty" map 1:1, e.g. "ttyUSB0" -> /dev/ttyUSB0.
 * hal_uart_init(dev, cfg)         -> termios cfsetispeed/cfsetospeed +
 *                                    tcsetattr(TCSANOW).
 * hal_uart_write / hal_uart_read  -> write() / poll()+read().
 *
 * Read timeout semantics: the line is configured raw with VMIN=0/VTIME=0
 * (non-canonical, non-blocking at the driver level); hal_uart_read waits on
 * poll(POLLIN) with the requested timeout and then reads whatever is ready.
 * UINT32_MAX = wait forever, 0 = poll once.
 *
 * Error codes (M4): -errno. Handle convention (C1): void* wrapping
 * { fd, pthread_mutex_t }; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE		/* CRTSCTS is a __USE_MISC-only macro */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define UART_DEV_PATH_MAX HAL_LINUX_PATH_MAX

/* Compose the device path for a logical UART name. Returns 0 or -errno. */
static int hal_linux_uart_path(const char *name, char *path, size_t cap)
{
	int idx, n;

	if (name[0] == '/') {
		/* Absolute path passthrough for runtime discovery. */
		if (strlen(name) >= cap)
			return -ENAMETOOLONG;
		memcpy(path, name, strlen(name) + 1u);
		return 0;
	}
	if (strncmp(name, "tty", 3) == 0) {
		/* "ttyS0"/"ttyUSB0"/"ttyACM0" used as the /dev basename. */
		n = snprintf(path, cap, "/dev/%s", name);
		if (n < 0 || n >= (int)cap)
			return -ENAMETOOLONG;
		return 0;
	}
	/* "uartN" -> /dev/ttySN */
	if (hal_linux_parse_trailing_index(name, &idx) != 0)
		return -EINVAL;
	n = snprintf(path, cap, "/dev/ttyS%d", idx);
	if (n < 0 || n >= (int)cap)
		return -ENAMETOOLONG;
	return 0;
}

hal_uart_dev_t hal_uart_get_bus(const char *name)
{
	hal_linux_dev_t *d;
	char path[UART_DEV_PATH_MAX];
	int fd;

	if (name == NULL)
		return NULL;
	if (hal_linux_uart_path(name, path, sizeof(path)) != 0)
		return NULL;

	fd = open(path, O_RDWR | O_NOCTTY);
	if (fd < 0)
		return NULL; /* C1 */

	d = hal_linux_dev_alloc();
	if (d == NULL) {
		(void)close(fd);
		return NULL;
	}
	d->fd = fd;
	return d;
}

/* Map a numeric baud rate to its termios Bxxx flag. Returns 0 if unknown. */
static speed_t hal_linux_baud_to_flag(uint32_t baud)
{
	switch (baud) {
	case 50:     return B50;
	case 75:     return B75;
	case 110:    return B110;
	case 134:    return B134;
	case 150:    return B150;
	case 200:    return B200;
	case 300:    return B300;
	case 600:    return B600;
	case 1200:   return B1200;
	case 1800:   return B1800;
	case 2400:   return B2400;
	case 4800:   return B4800;
	case 9600:   return B9600;
	case 19200:  return B19200;
	case 38400:  return B38400;
	case 57600:  return B57600;
	case 115200: return B115200;
	case 230400: return B230400;
	case 460800: return B460800;
	case 500000: return B500000;
	case 576000: return B576000;
	case 921600: return B921600;
	case 1000000: return B1000000;
	case 1152000: return B1152000;
	case 1500000: return B1500000;
	case 2000000: return B2000000;
	case 2500000: return B2500000;
	case 3000000: return B3000000;
	case 3500000: return B3500000;
	case 4000000: return B4000000;
	default:     return 0;
	}
}

int hal_uart_init(hal_uart_dev_t dev, const hal_uart_config_t *cfg)
{
	hal_linux_dev_t *d = dev;
	struct termios tio;
	speed_t baud;
	int rc;

	if (d == NULL || cfg == NULL)
		return -EINVAL;
	if (cfg->data_bits != 7 && cfg->data_bits != 8)
		return -EINVAL;
	if (cfg->stop_bits != 1 && cfg->stop_bits != 2)
		return -EINVAL;
	if (cfg->flow_ctrl > 1u)
		return -EINVAL;

	baud = hal_linux_baud_to_flag(cfg->baud);
	if (baud == 0)
		return -EINVAL;

	rc = tcgetattr(d->fd, &tio);
	if (rc == -1)
		return -errno;

	/* Raw mode: no canonical/echo/signal processing, 8-bit transparent. */
	tio.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
			 ICRNL | IXON);
	tio.c_oflag &= ~OPOST;
	tio.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB);
	tio.c_cflag |= (CLOCAL | CREAD);

	switch (cfg->data_bits) {
	case 7: tio.c_cflag |= CS7; break;
	case 8: tio.c_cflag |= CS8; break;
	}

	/* Parity. */
	tio.c_iflag &= ~(INPCK | ISTRIP);
	switch (cfg->parity) {
	case HAL_UART_PARITY_NONE: break;
	case HAL_UART_PARITY_ODD:  tio.c_cflag |= (PARENB | PARODD); break;
	case HAL_UART_PARITY_EVEN: tio.c_cflag |= PARENB;            break;
	default: return -EINVAL;
	}

	/* Stop bits. */
	if (cfg->stop_bits == 2)
		tio.c_cflag |= CSTOPB;

	/* Hardware flow control (RTS/CTS when available on the tty port). */
	tio.c_cflag &= ~CRTSCTS;
	if (cfg->flow_ctrl != 0u)
		tio.c_cflag |= CRTSCTS;

	/* Non-canonical reads with VMIN=0/VTIME=0: hal_uart_read() decides
	 * timeouts via poll(); read() never blocks below the driver buffer. */
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	if (cfsetispeed(&tio, baud) == -1)
		return -errno;
	if (cfsetospeed(&tio, baud) == -1)
		return -errno;

	rc = tcsetattr(d->fd, TCSANOW, &tio);
	if (rc == -1)
		return -errno;

	return HAL_OK;
}

int hal_uart_write(hal_uart_dev_t dev, const void *data, size_t len)
{
	hal_linux_dev_t *d = dev;
	const uint8_t *p = data;
	size_t total = 0;

	if (d == NULL || (data == NULL && len != 0))
		return -EINVAL;

	while (total < len) {
		ssize_t w = write(d->fd, p + total, len - total);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		total += (size_t)w;
	}
	return (int)total;
}

int hal_uart_read(hal_uart_dev_t dev, void *buf, size_t len,
		  uint32_t timeout_ms)
{
	hal_linux_dev_t *d = dev;
	struct pollfd pfd;
	ssize_t n;
	int rc;

	if (d == NULL || (buf == NULL && len != 0))
		return -EINVAL;
	if (len == 0)
		return 0;

	pfd.fd = d->fd;
	pfd.events = POLLIN;
	pfd.revents = 0;

	/* UINT32_MAX = wait forever; 0 = poll once; else typed timeout. */
	do {
		rc = poll(&pfd, 1, (timeout_ms == UINT32_MAX) ? -1
					     : (int)(uint32_t)timeout_ms);
	} while (rc == -1 && errno == EINTR);
	if (rc == -1)
		return -errno;
	if (rc == 0)
		return 0; /* timeout: no data */

	n = read(d->fd, buf, len);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
			return 0;
		return -errno;
	}
	return (int)n;
}