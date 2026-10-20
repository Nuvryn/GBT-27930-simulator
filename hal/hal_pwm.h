/*
 * hal_pwm.h
 *
 * HAL (Hardware Abstraction Layer) PWM interface contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The device is an opaque `void *` handle; the concrete object lives in
 *    the backend (Zephyr `const struct device` / Linux sysfs pwm base).
 *    The application never dereferences it.
 *  - Multi-channel PWM devices address channels by a logical index within
 *    the device.
 *  - Duty cycle is expressed in permille (0-1000 = 0.0%-100.0%) to avoid
 *    float and remain resolution-independent across backends.
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-ENODEV`, ...) on
 *    failure.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_pwm.h
 */
#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** PWM device handle (backend: Zephyr `const struct device` / Linux sysfs). */
typedef void *hal_pwm_dev_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Set the PWM output frequency (affects all channels of the device).
 *
 * Applies to channels that are not yet enabled or to all currently enabled
 * channels, backend-defined. Subsequent @ref hal_pwm_set_duty calls use this
 * frequency as the period base.
 *
 * @param dev      PWM device handle.
 * @param freq_hz  Output frequency in hertz (> 0).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_pwm_set_freq(hal_pwm_dev_t dev, uint32_t freq_hz);

/**
 * @brief Set one channel's duty cycle.
 *
 * @param dev           PWM device handle.
 * @param channel       Logical channel index within the device.
 * @param duty_permille Duty cycle in permille, 0-1000 (1000 = 100%).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_pwm_set_duty(hal_pwm_dev_t dev, uint8_t channel, uint16_t duty_permille);

/**
 * @brief Start output on a channel.
 *
 * @param dev     PWM device handle.
 * @param channel Logical channel index within the device.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_pwm_enable(hal_pwm_dev_t dev, uint8_t channel);

/**
 * @brief Stop output on a channel.
 *
 * @param dev     PWM device handle.
 * @param channel Logical channel index within the device.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_pwm_disable(hal_pwm_dev_t dev, uint8_t channel);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_H */