/*
 * hal_pwm.c - Linux PWM backend (legacy /sys/class/pwm sysfs).
 *
 * hal_pwm_get_dev("pwmN") -> /sys/class/pwm/pwmchipN (must exist).
 *
 * Linux sysfs PWM is per-channel: each channel is exported with
 * `echo ch > pwmchipN/export`, which creates pwmch/duty_cycle, period,
 * polarity and enable attributes. The HAL contract says:
 *   - hal_pwm_set_freq affects the whole device. The backend caches the
 *     frequency in the handle and writes the derived period (ns) to every
 *     channel exported so far; channels enabled later inherit it.
 *   - hal_pwm_set_duty writes duty_cycle = period * permille / 1000 to the
 *     channel (0..1000 permille).
 *   - hal_pwm_enable exports the channel, ensures period + duty_cycle are
 *     written (sysfs requires period > 0 before duty_cycle, both before
 *     enable=1), then writes enable=1.
 *   - hal_pwm_disable writes enable=0 (the channel stays exported).
 *
 * Error codes (M4): -errno. Handle convention (C1): void * wrapping
 * hal_linux_pwm_t; NULL = not found.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "hal/hal_dev.h"
#include "hal_linux_priv.h"

#define PWM_CLASS_ROOT "/sys/class/pwm"
#define HAL_LINUX_PWM_MAX_CH 8
#define NS_PER_SEC 1000000000u

typedef struct hal_linux_pwm {
	int chip;                    /* pwmchip index                    */
	uint32_t freq_hz;            /* cached frequency (0 = unset)     */
	uint32_t duty_ns[HAL_LINUX_PWM_MAX_CH]; /* cached duty per channel */
	uint8_t duty_valid[HAL_LINUX_PWM_MAX_CH];
	uint8_t exported[HAL_LINUX_PWM_MAX_CH];
} hal_linux_pwm_t;

static int hal_linux_pwm_write_period_attr(const hal_linux_pwm_t *p, int ch)
{
	char path[HAL_LINUX_PATH_MAX];

	if (p->freq_hz == 0)
		return -EINVAL;
	(void)snprintf(path, sizeof(path), PWM_CLASS_ROOT "/pwmchip%d/pwm%d"
		       "/period", p->chip, ch);
	return hal_linux_write_int_file(path,
					 (long)(NS_PER_SEC / p->freq_hz));
}

static int hal_linux_pwm_write_duty_attr(const hal_linux_pwm_t *p, int ch)
{
	char path[HAL_LINUX_PATH_MAX];

	if (p->freq_hz == 0 || !p->duty_valid[ch])
		return -EINVAL;
	(void)snprintf(path, sizeof(path), PWM_CLASS_ROOT "/pwmchip%d/pwm%d"
		       "/duty_cycle", p->chip, ch);
	return hal_linux_write_int_file(path, (long)p->duty_ns[ch]);
}

hal_pwm_dev_t hal_pwm_get_dev(const char *name)
{
	hal_linux_pwm_t *p;
	char path[HAL_LINUX_PATH_MAX];
	int idx, n;

	if (name == NULL)
		return NULL;
	if (hal_linux_parse_trailing_index(name, &idx) != 0)
		return NULL;

	n = snprintf(path, sizeof(path), PWM_CLASS_ROOT "/pwmchip%d", idx);
	if (n < 0 || n >= (int)sizeof(path))
		return NULL;

	/* Runtime discovery: the pwmchip must exist. */
	if (hal_linux_dir_exists(path) != 0)
		return NULL;

	p = calloc(1, sizeof(*p));
	if (p == NULL)
		return NULL;
	p->chip = idx;
	return p;
}

int hal_pwm_set_freq(hal_pwm_dev_t dev, uint32_t freq_hz)
{
	hal_linux_pwm_t *p = dev;
	int i, rc;

	if (p == NULL || freq_hz == 0)
		return -EINVAL;

	p->freq_hz = freq_hz;

	for (i = 0; i < HAL_LINUX_PWM_MAX_CH; i++) {
		if (!p->exported[i])
			continue;
		rc = hal_linux_pwm_write_period_attr(p, i);
		if (rc != 0)
			return rc;
	}
	return 0;
}

int hal_pwm_set_duty(hal_pwm_dev_t dev, uint8_t channel,
		     uint16_t duty_permille)
{
	hal_linux_pwm_t *p = dev;
	uint64_t duty_ns;

	if (p == NULL || channel >= HAL_LINUX_PWM_MAX_CH)
		return -EINVAL;
	if (duty_permille > 1000)
		return -EINVAL;
	if (p->freq_hz == 0)
		return -EINVAL;

	/* period * permille / 1000 in ns. */
	duty_ns = (uint64_t)(NS_PER_SEC / p->freq_hz) * duty_permille / 1000u;
	p->duty_ns[channel] = (uint32_t)duty_ns;
	p->duty_valid[channel] = 1;

	if (!p->exported[channel])
		return 0; /* written when the channel is enabled */
	return hal_linux_pwm_write_duty_attr(p, channel);
}

int hal_pwm_enable(hal_pwm_dev_t dev, uint8_t channel)
{
	hal_linux_pwm_t *p = dev;
	char export_path[HAL_LINUX_PATH_MAX];
	char enable_path[HAL_LINUX_PATH_MAX];
	int rc;

	if (p == NULL || channel >= HAL_LINUX_PWM_MAX_CH)
		return -EINVAL;
	if (p->freq_hz == 0)
		return -EINVAL;

	/* Export the channel (creating pwmch/... attributes). */
	if (!p->exported[channel]) {
		(void)snprintf(export_path, sizeof(export_path),
			       PWM_CLASS_ROOT "/pwmchip%d/export", p->chip);
		rc = hal_linux_write_int_file(export_path, channel);
		if (rc != 0 && rc != -EBUSY && rc != -EINVAL && rc != -EEXIST)
			return rc;
		p->exported[channel] = 1;
	}

	/* Sysfs ordering: period (freq) and duty_cycle must be written
	 * before enable=1. */
	rc = hal_linux_pwm_write_period_attr(p, channel);
	if (rc != 0)
		return rc;
	if (p->duty_valid[channel]) {
		rc = hal_linux_pwm_write_duty_attr(p, channel);
		if (rc != 0)
			return rc;
	} else {
		/* Default to 0% so the output does not latch high. */
		(void)snprintf(enable_path, sizeof(enable_path),
			       PWM_CLASS_ROOT "/pwmchip%d/pwm%d/duty_cycle",
			       p->chip, channel);
		(void)hal_linux_write_int_file(enable_path, 0);
	}

	(void)snprintf(enable_path, sizeof(enable_path),
		       PWM_CLASS_ROOT "/pwmchip%d/pwm%d/enable",
		       p->chip, channel);
	return hal_linux_write_int_file(enable_path, 1);
}

int hal_pwm_disable(hal_pwm_dev_t dev, uint8_t channel)
{
	hal_linux_pwm_t *p = dev;
	char path[HAL_LINUX_PATH_MAX];

	if (p == NULL || channel >= HAL_LINUX_PWM_MAX_CH)
		return -EINVAL;
	if (!p->exported[channel])
		return 0;

	(void)snprintf(path, sizeof(path), PWM_CLASS_ROOT "/pwmchip%d/pwm%d"
		       "/enable", p->chip, channel);
	return hal_linux_write_int_file(path, 0);
}