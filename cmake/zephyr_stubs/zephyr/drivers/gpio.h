/*
 * gpio.h - MOCK STUB for the Zephyr GPIO driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_gpio.c (Zephyr v4.x):
 *   gpio_flags_t / gpio_pin_t / gpio_port_pins_t / gpio_configure flags
 *   gpio_pin_configure / gpio_pin_set / gpio_pin_get / gpio_pin_toggle
 *   gpio_pin_interrupt_configure + GPIO_INT_* flags
 *   gpio_callback_handler_t + struct gpio_callback + gpio_init_callback /
 *   gpio_add_callback / gpio_remove_callback
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_GPIO_H_
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_H_

#include <stdint.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t gpio_flags_t;
typedef uint32_t gpio_pin_t;
typedef uint32_t gpio_port_pins_t;

/* Input/output configuration flags. */
#define GPIO_DISCONNECTED	0
#define GPIO_INPUT		BIT(16)
#define GPIO_OUTPUT		BIT(17)
#define GPIO_OPEN_DRAIN		BIT(21)
#define GPIO_PULL_UP		BIT(4)
#define GPIO_PULL_DOWN		BIT(5)

/* Interrupt configuration flags. */
#define GPIO_INT_DISABLE		0
#define GPIO_INT_ENABLE			BIT(8)
#define GPIO_INT_EDGE			BIT(9)
#define GPIO_INT_DOUBLE_EDGE		BIT(11)
#define GPIO_INT_HIGH_1			BIT(13)
#define GPIO_INT_LOW_0			BIT(14)

#define GPIO_INT_EDGE_RISING	(GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_HIGH_1)
#define GPIO_INT_EDGE_FALLING	(GPIO_INT_ENABLE | GPIO_INT_EDGE | GPIO_INT_LOW_0)
#define GPIO_INT_EDGE_BOTH	(GPIO_INT_ENABLE | GPIO_INT_EDGE | \
				 GPIO_INT_LOW_0 | GPIO_INT_HIGH_1)

struct gpio_callback;

typedef void (*gpio_callback_handler_t)(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins);

struct gpio_callback {
	gpio_callback_handler_t handler;
	gpio_port_pins_t pin_mask;
};

extern int gpio_pin_configure(const struct device *port, gpio_pin_t pin,
			      gpio_flags_t flags);
extern int gpio_pin_set(const struct device *port, gpio_pin_t pin, int value);
extern int gpio_pin_get(const struct device *port, gpio_pin_t pin);
extern int gpio_pin_toggle(const struct device *port, gpio_pin_t pin);
extern int gpio_pin_interrupt_configure(const struct device *port,
					gpio_pin_t pin, gpio_flags_t flags);

static inline void gpio_init_callback(struct gpio_callback *callback,
				      gpio_callback_handler_t handler,
				      gpio_port_pins_t pin_mask)
{
	if (callback != NULL) {
		callback->handler = handler;
		callback->pin_mask = pin_mask;
	}
}

extern int gpio_add_callback(const struct device *port,
			     struct gpio_callback *callback);
extern int gpio_remove_callback(const struct device *port,
				struct gpio_callback *callback);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_GPIO_H_ */