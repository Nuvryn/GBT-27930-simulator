/*
 * hal_gpio.c - Linux GPIO backend (DEGRADED: legacy /sys/class/gpio sysfs).
 *
 * Per user sanity-check #3, this backend deliberately uses the legacy sysfs
 * interface instead of libgpiod / the character-device gpiochips:
 *
 *   hal_gpio_get_port("gpioN"/"gpioa") -> handle holding the sysfs GPIO base
 *       number. "gpio7" -> base 7, "gpioa" -> base 0 (letters ignored).
 *   hal_gpio_init            -> echo N > .../export, "out"/"in" > ...gpioM/direction
 *   hal_gpio_write/read      -> write/read .../gpioM/value
 *   hal_gpio_toggle          -> read value then write the inverted level
 *   hal_gpio_set_int         -> write the "edge" attribute (none|rising|
 *                               falling|both) and, when a callback is given,
 *                               run a small poll(2)/POLLPRI worker thread on
 *                               the value file (the classic sysfs edge-interrupt
 *                               recipe). cb == NULL disables the interrupt,
 *                               joins the worker and clears the edge.
 *
 * Documented degradations (all sysfs ABI limits, chosen path):
 *   - OPEN-DRAIN (HAL_GPIO_MODE_OUT_OD) is treated as push-pull "out".
 *   - INTERNAL PULL-UPS/DOWNS (HAL_GPIO_MODE_IN_PU/IN_PD) are treated as a
 *     floating "in"; pulls must be configured by the board wiring or device
 *     tree, there is no generic sysfs pull knob.
 *   - Interrupt worker lifetime == process lifetime while the interrupt is
 *     enabled; the callback runs on the worker thread (backend context).
 *
 * The sysfs root can be overridden at compile time
 * (-DHAL_LINUX_GPIO_SYSFS_ROOT="...") so tests can point the backend at a
 * mock sysfs tree. The production default is the kernel path.
 *
 * Error codes (M4): -errno. Handle convention (C1): void * wrapping
 * hal_linux_gpio_t; NULL = not found.
 */
#ifndef HAL_LINUX_GPIO_SYSFS_ROOT
#define HAL_LINUX_GPIO_SYSFS_ROOT "/sys/class/gpio"
#endif

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define HAL_LINUX_GPIO_MAX_IRQ 8

typedef struct hal_linux_gpio_irq {
	int pin;                 /* absolute sysfs gpio number         */
	int active;              /* slot in use                        */
	volatile int running;    /* worker keeps looping while set     */
	pthread_t thread;
	hal_gpio_cb_t cb;        /* list of callbacks (last wins)      */
	void *user_data;
} hal_linux_gpio_irq_t;

typedef struct hal_linux_gpio {
	int base;              /* sysfs gpio base number for this port */
	pthread_mutex_t irq_lock;
	hal_linux_gpio_irq_t irqs[HAL_LINUX_GPIO_MAX_IRQ];
} hal_linux_gpio_t;

/* Write a number to .../export; EINVAL/EBUSY/EEXIST = already exported:
 * idempotent under the sysfs ABI. Returns 0 or -errno. */
static int hal_linux_gpio_export(hal_linux_gpio_t *g, int num)
{
	char path[HAL_LINUX_PATH_MAX];
	int rc;

	(void)g;
	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT "/export");
	rc = hal_linux_write_int_file(path, num);
	if (rc == 0)
		return 0;
	if (rc == -EINVAL || rc == -EBUSY || rc == -EEXIST)
		return 0; /* already exported / in use: the goal is met */
	return rc;
}

hal_gpio_port_t hal_gpio_get_port(const char *name)
{
	hal_linux_gpio_t *g;
	const char *p;
	char *end;
	long base = 0;

	if (name == NULL)
		return NULL;

	/* Logical name -> sysfs base number. Trailing digits are the base
	 * ("gpio7" -> 7); alpha-only names default to base 0 ("gpioa"). */
	p = name;
	while (*p != '\0')
		p++;
	while (p > name && p[-1] >= '0' && p[-1] <= '9')
		p--;
	if (*p != '\0') {
		base = strtol(p, &end, 10);
		if (end == p || base < 0)
			return NULL;
	}

	/* Runtime discovery: the sysfs root must actually exist. */
	if (hal_linux_dir_exists(HAL_LINUX_GPIO_SYSFS_ROOT) != 0)
		return NULL;

	g = calloc(1, sizeof(*g));
	if (g == NULL)
		return NULL;
	g->base = (int)base;
	(void)pthread_mutex_init(&g->irq_lock, NULL);
	for (int i = 0; i < HAL_LINUX_GPIO_MAX_IRQ; i++)
		g->irqs[i].pin = -1;
	return g;
}

int hal_gpio_init(hal_gpio_port_t port, uint8_t pin, hal_gpio_mode_t mode)
{
	hal_linux_gpio_t *g = port;
	char path[HAL_LINUX_PATH_MAX];
	const char *dir;
	int num, rc;

	if (g == NULL)
		return -EINVAL;
	num = g->base + (int)pin;

	rc = hal_linux_gpio_export(g, num);
	if (rc != 0)
		return rc;

	switch (mode) {
	case HAL_GPIO_MODE_OUT_PP:
		dir = "out";
		break;
	case HAL_GPIO_MODE_OUT_OD:
		/* DEGRADED: no generic sysfs open-drain knob; emit a
		 * push-pull output (documented in the file header). */
		dir = "out";
		break;
	case HAL_GPIO_MODE_IN:
		dir = "in";
		break;
	case HAL_GPIO_MODE_IN_PU:
	case HAL_GPIO_MODE_IN_PD:
		/* DEGRADED: sysfs has no generic pull configuration; treated
		 * as a floating input (documented in the file header). */
		dir = "in";
		break;
	default:
		return -EINVAL;
	}

	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT
		       "/gpio%d/direction", num);
	return hal_linux_write_text_file(path, dir);
}

int hal_gpio_write(hal_gpio_port_t port, uint8_t pin, int value)
{
	hal_linux_gpio_t *g = port;
	char path[HAL_LINUX_PATH_MAX];

	if (g == NULL)
		return -EINVAL;
	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT
		       "/gpio%d/value", g->base + (int)pin);
	return hal_linux_write_text_file(path, value ? "1" : "0");
}

int hal_gpio_read(hal_gpio_port_t port, uint8_t pin)
{
	hal_linux_gpio_t *g = port;
	char path[HAL_LINUX_PATH_MAX];
	long v;
	int rc;

	if (g == NULL)
		return -EINVAL;
	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT
		       "/gpio%d/value", g->base + (int)pin);
	rc = hal_linux_read_int_file(path, &v);
	if (rc != 0)
		return rc;
	return (v == 0) ? 0 : 1;
}

int hal_gpio_toggle(hal_gpio_port_t port, uint8_t pin)
{
	int v = hal_gpio_read(port, pin);

	if (v < 0)
		return v;
	return hal_gpio_write(port, pin, (v == 0) ? 1 : 0);
}

/* sysfs edge-interrupt worker: poll(POLLPRI) on .../value. Re-arming a sysfs
 * edge interrupt requires reading the value file after every event. */
static void *hal_linux_gpio_irq_worker(void *arg)
{
	hal_linux_gpio_irq_t *irq = arg;
	char path[HAL_LINUX_PATH_MAX];
	char buf[16];
	struct pollfd pfd;
	int fd, rc;

	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT
		       "/gpio%d/value", irq->pin);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;

	/* Initial read arms the edge watcher. */
	(void)read(fd, buf, sizeof(buf));

	while (irq->running) {
		pfd.fd = fd;
		pfd.events = POLLPRI;
		pfd.revents = 0;
		do {
			rc = poll(&pfd, 1, 500);
		} while (rc == -1 && errno == EINTR);
		if (rc < 0 || !irq->running)
			break;
		if (rc == 0)
			continue;

		/* Re-arm: the value file must be re-read after each edge. */
		(void)lseek(fd, 0, SEEK_SET);
		(void)read(fd, buf, sizeof(buf));

		if (!irq->running)
			break;
		if (irq->cb != NULL)
			irq->cb(irq->user_data);
	}

	(void)close(fd);
	return NULL;
}

int hal_gpio_set_int(hal_gpio_port_t port, uint8_t pin,
		     hal_gpio_int_mode_t mode, hal_gpio_cb_t cb,
		     void *user_data)
{
	hal_linux_gpio_t *g = port;
	hal_linux_gpio_irq_t *irq = NULL;
	char path[HAL_LINUX_PATH_MAX];
	const char *edge;
	int num, i, rc;

	if (g == NULL)
		return -EINVAL;
	num = g->base + (int)pin;

	edge = (mode == HAL_GPIO_INT_RISING)  ? "rising" :
	       (mode == HAL_GPIO_INT_FALLING) ? "falling" :
	       (mode == HAL_GPIO_INT_BOTH)    ? "both" : "none";

	(void)snprintf(path, sizeof(path), HAL_LINUX_GPIO_SYSFS_ROOT
		       "/gpio%d/edge", num);

	if (cb == NULL) {
		/* Disable: clear the edge attribute and stop the worker. */
		rc = hal_linux_write_text_file(path, "none");
		if (rc != 0)
			return rc;

		pthread_mutex_lock(&g->irq_lock);
		for (i = 0; i < HAL_LINUX_GPIO_MAX_IRQ; i++) {
			if (g->irqs[i].active && g->irqs[i].pin == num) {
				irq = &g->irqs[i];
				break;
			}
		}
		if (irq != NULL) {
			irq->running = 0;   /* worker exits after its poll */
			pthread_mutex_unlock(&g->irq_lock);
			(void)pthread_join(irq->thread, NULL);
			pthread_mutex_lock(&g->irq_lock);
			irq->active = 0;
			irq->cb = NULL;
			irq->pin = -1;
		}
		pthread_mutex_unlock(&g->irq_lock);
		return 0;
	}

	/* Enable: (re-)register the pin and write the edge attribute. */
	pthread_mutex_lock(&g->irq_lock);
	for (i = 0; i < HAL_LINUX_GPIO_MAX_IRQ; i++) {
		if (g->irqs[i].active && g->irqs[i].pin == num) {
			irq = &g->irqs[i];
			break;
		}
	}
	if (irq == NULL) {
		for (i = 0; i < HAL_LINUX_GPIO_MAX_IRQ; i++) {
			if (!g->irqs[i].active) {
				irq = &g->irqs[i];
				break;
			}
		}
	}
	if (irq == NULL) {
		pthread_mutex_unlock(&g->irq_lock);
		return -ENOSPC;
	}

	irq->pin = num;
	irq->cb = cb;
	irq->user_data = user_data;
	irq->running = 1;
	irq->active = 1;

	/* Write the edge attribute first so the worker's initial poll is
	 * armed (the pin should already be `in` per contract). */
	rc = hal_linux_write_text_file(path, edge);
	if (rc != 0) {
		irq->active = 0;
		pthread_mutex_unlock(&g->irq_lock);
		return rc;
	}

	rc = pthread_create(&irq->thread, NULL, hal_linux_gpio_irq_worker, irq);
	if (rc != 0) {
		irq->active = 0;
		irq->running = 0;
		pthread_mutex_unlock(&g->irq_lock);
		return -rc;
	}
	pthread_mutex_unlock(&g->irq_lock);
	return 0;
}