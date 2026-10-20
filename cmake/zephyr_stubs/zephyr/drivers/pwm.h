/*
 * pwm.h - MOCK STUB for the Zephyr PWM driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_pwm.c (Zephyr v4.x):
 *   pwm_flags_t / PWM_POLARITY_NORMAL
 *   pwm_set_cycles(dev, channel, period, pulse, flags)
 *   pwm_get_cycles_per_sec(dev, channel, &cycles)
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_PWM_H_
#define ZEPHYR_INCLUDE_DRIVERS_PWM_H_

#include <stdint.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pwm_flags_t;

#define PWM_POLARITY_NORMAL 0

extern int pwm_set_cycles(const struct device *dev, uint32_t channel,
			  uint32_t period_cycles, uint32_t pulse_cycles,
			  pwm_flags_t flags);
extern int pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
				  uint64_t *cycles);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_PWM_H_ */