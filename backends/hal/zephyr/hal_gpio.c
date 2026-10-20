/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_gpio.c
 *
 * Zephyr backend for the GPIO HAL interface contract (hal_gpio.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_gpio.c); the bodies are not:
 *   hal_gpio_get_port -> NULL (contract: device not available)
 *   hal_gpio_init     -> -ENOSYS
 *   hal_gpio_write    -> -ENOSYS
 *   hal_gpio_read     -> -ENOSYS
 *   hal_gpio_toggle   -> -ENOSYS
 *   hal_gpio_set_int  -> -ENOSYS
 *
 * No real Zephyr gpio_pin_configure()/gpio_pin_* logic is implemented.
 * Every operation is counted in s_unimpl_count so a caller can never
 * mistake a skeleton for a working GPIO backend.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "hal/hal_gpio.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_gpio_port_t hal_gpio_get_port(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_gpio_init(hal_gpio_port_t port, uint8_t pin, hal_gpio_mode_t mode)
{
	(void)port;
	(void)pin;
	(void)mode;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_gpio_write(hal_gpio_port_t port, uint8_t pin, int value)
{
	(void)port;
	(void)pin;
	(void)value;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_gpio_read(hal_gpio_port_t port, uint8_t pin)
{
	(void)port;
	(void)pin;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_gpio_toggle(hal_gpio_port_t port, uint8_t pin)
{
	(void)port;
	(void)pin;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_gpio_set_int(hal_gpio_port_t port, uint8_t pin,
		     hal_gpio_int_mode_t mode, hal_gpio_cb_t cb,
		     void *user_data)
{
	(void)port;
	(void)pin;
	(void)mode;
	(void)cb;
	(void)user_data;
	s_unimpl_count++;
	return -ENOSYS;
}