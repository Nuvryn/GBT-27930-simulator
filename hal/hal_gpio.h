/*
 * hal_gpio.h
 *
 * HAL (Hardware Abstraction Layer) GPIO interface contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The port is an opaque `void *` handle; the concrete object lives in the
 *    backend (Zephyr `const struct device` / Linux sysfs gpio base, e.g.
 *    "/sys/class/gpio/gpioN"). The application never dereferences it.
 *  - Pins are addressed by a logical `pin` number (0..N) resolved by the
 *    backend to the concrete port pin. Single-pin operations take both the
 *    port handle and the pin index.
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-ENOMEM`, ...) on
 *    failure. `hal_gpio_read` returns the level (0/1) on success, negative
 *    errno on failure.
 *  - Not callable from ISR context (M7): no `_from_isr` variants.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_gpio.h
 */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** GPIO port handle (backend: Zephyr `const struct device` / Linux sysfs). */
typedef void *hal_gpio_port_t;

/* ------------------------------------------------------------------------- */
/* Enums                                                                     */
/* ------------------------------------------------------------------------- */

/** GPIO pin operating mode. */
typedef enum {
	HAL_GPIO_MODE_OUT_PP = 0, /**< Push-pull output. */
	HAL_GPIO_MODE_OUT_OD,     /**< Open-drain output. */
	HAL_GPIO_MODE_IN,         /**< Floating input (no pull). */
	HAL_GPIO_MODE_IN_PU,      /**< Input with internal pull-up. */
	HAL_GPIO_MODE_IN_PD,      /**< Input with internal pull-down. */
} hal_gpio_mode_t;

/** GPIO interrupt trigger mode. */
typedef enum {
	HAL_GPIO_INT_RISING = 0, /**< Trigger on 0 -> 1 edge. */
	HAL_GPIO_INT_FALLING,    /**< Trigger on 1 -> 0 edge. */
	HAL_GPIO_INT_BOTH,       /**< Trigger on either edge. */
} hal_gpio_int_mode_t;

/* ------------------------------------------------------------------------- */
/* Callbacks                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief GPIO interrupt callback.
 *
 * Invoked from backend interrupt context. Keep it minimal (set a flag or
 * post a semaphore); do real processing in a task (M7).
 */
typedef void (*hal_gpio_cb_t)(void *user_data);

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Configure a GPIO pin's mode.
 *
 * @param port GPIO port handle.
 * @param pin  Pin index within the port.
 * @param mode Operating mode to apply (see @ref hal_gpio_mode_t).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_gpio_init(hal_gpio_port_t port, uint8_t pin, hal_gpio_mode_t mode);

/**
 * @brief Drive a GPIO pin to a logic level.
 *
 * @param port  GPIO port handle.
 * @param pin   Pin index within the port.
 * @param value 0 = low, non-zero = high.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_gpio_write(hal_gpio_port_t port, uint8_t pin, int value);

/**
 * @brief Read a GPIO pin's current logic level.
 *
 * @param port GPIO port handle.
 * @param pin  Pin index within the port.
 *
 * @return 0 or 1 on success, negative errno on failure.
 */
int hal_gpio_read(hal_gpio_port_t port, uint8_t pin);

/**
 * @brief Invert a GPIO pin's current output level.
 *
 * @param port GPIO port handle.
 * @param pin  Pin index within the port.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_gpio_toggle(hal_gpio_port_t port, uint8_t pin);

/**
 * @brief Configure and enable an edge-triggered interrupt on a pin.
 *
 * The pin should already be configured as input via @ref hal_gpio_init.
 * A previously registered callback on @p pin is replaced. Pass @p cb = NULL
 * to disable the interrupt on that pin.
 *
 * @param port      GPIO port handle.
 * @param pin       Pin index within the port.
 * @param mode      Trigger edge (see @ref hal_gpio_int_mode_t).
 * @param cb        Callback invoked on trigger; NULL disables the interrupt.
 * @param user_data Opaque argument forwarded to @p cb.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_gpio_set_int(hal_gpio_port_t port, uint8_t pin, hal_gpio_int_mode_t mode,
		     hal_gpio_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */