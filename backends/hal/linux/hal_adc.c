/*
 * hal_adc.c - Linux ADC backend (IIO sysfs).
 *
 * hal_adc_get_dev("adcN") -> discover /sys/bus/iio/devices/iio:deviceN
 *   (runtime discovery: the directory must exist; no hardcoded chip path).
 * hal_adc_read(dev, ch, &value) -> read in_voltage{ch}_raw; when
 *   in_voltage{ch}_scale is present the raw count is scaled to millivolts
 *   (value = raw * scale * 1000). If no scale attribute exists the raw
 *   count is returned and this deviation from the millivolt contract is
 *   documented here (backends that cannot scale return raw counts).
 *
 * Error codes (M4): -errno. Handle convention (C1): void * wrapping
 * hal_linux_adc_t; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define ADC_IIO_ROOT "/sys/bus/iio/devices"

typedef struct hal_linux_adc {
	char base_dir[HAL_LINUX_PATH_MAX]; /* /sys/bus/iio/devices/iio:deviceN */
} hal_linux_adc_t;

hal_adc_dev_t hal_adc_get_dev(const char *name)
{
	hal_linux_adc_t *a;
	char path[HAL_LINUX_PATH_MAX];
	int idx, n;

	if (name == NULL)
		return NULL;
	if (hal_linux_parse_trailing_index(name, &idx) != 0)
		return NULL;

	n = snprintf(path, sizeof(path), ADC_IIO_ROOT "/iio:device%d", idx);
	if (n < 0 || n >= (int)sizeof(path))
		return NULL;

	/* Runtime discovery: accept only if the IIO device actually exists. */
	if (hal_linux_dir_exists(path) != 0)
		return NULL;

	a = malloc(sizeof(*a));
	if (a == NULL)
		return NULL;
	(void)snprintf(a->base_dir, sizeof(a->base_dir), "%s", path);
	return a;
}

int hal_adc_read(hal_adc_dev_t dev, uint8_t channel, uint32_t *value)
{
	hal_linux_adc_t *a = dev;
	char raw_path[256];
	char scale_path[256];
	char buf[64];
	long raw;
	char *end;
	double scale;
	int rc;

	if (a == NULL || value == NULL)
		return -EINVAL;

	(void)snprintf(raw_path, sizeof(raw_path), "%s/in_voltage%u_raw",
		       a->base_dir, (unsigned int)channel);
	rc = hal_linux_read_int_file(raw_path, &raw);
	if (rc != 0)
		return rc;

	/* Optional scale attribute: raw * scale = volts, so mV = raw * scale
	 * * 1000. */
	(void)snprintf(scale_path, sizeof(scale_path), "%s/in_voltage%u_scale",
		       a->base_dir, (unsigned int)channel);
	if (hal_linux_read_text_file(scale_path, buf, sizeof(buf)) == 0) {
		scale = strtod(buf, &end);
		if (end != buf)
			*value = (uint32_t)((double)raw * scale * 1000.0);
		else
			*value = (uint32_t)raw; /* documented deviation */
	} else {
		/* No scale attribute: raw counts (documented deviation). */
		*value = (uint32_t)raw;
	}
	return 0;
}