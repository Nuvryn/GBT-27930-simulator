/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_uart.c
 *
 * Zephyr backend for the UART HAL interface contract (hal_uart.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_uart.c); the bodies are not:
 *   hal_uart_get_bus -> NULL (contract: device not available)
 *   hal_uart_init    -> -ENOSYS
 *   hal_uart_write   -> -ENOSYS
 *   hal_uart_read    -> -ENOSYS
 *
 * No real Zephyr uart_configure()/uart_poll_* logic is implemented. Every
 * operation is counted in s_unimpl_count so a caller can never mistake a
 * skeleton for a working UART backend.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "hal/hal_uart.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_uart_dev_t hal_uart_get_bus(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_uart_init(hal_uart_dev_t dev, const hal_uart_config_t *cfg)
{
	(void)dev;
	(void)cfg;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_uart_write(hal_uart_dev_t dev, const void *data, size_t len)
{
	(void)dev;
	(void)data;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_uart_read(hal_uart_dev_t dev, void *buf, size_t len,
		  uint32_t timeout_ms)
{
	(void)dev;
	(void)buf;
	(void)len;
	(void)timeout_ms;
	s_unimpl_count++;
	return -ENOSYS;
}