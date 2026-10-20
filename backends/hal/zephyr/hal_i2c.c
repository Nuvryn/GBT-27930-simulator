/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_i2c.c
 *
 * Zephyr backend for the I2C HAL interface contract (hal_i2c.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_i2c.c); the bodies are not:
 *   hal_i2c_get_bus   -> NULL (contract: device not available)
 *   hal_i2c_transfer  -> -ENOSYS
 *   hal_i2c_read_reg  -> -ENOSYS
 *   hal_i2c_write_reg -> -ENOSYS
 *
 * No real Zephyr i2c_transfer()/device-table logic is implemented. Every
 * operation is counted in s_unimpl_count so a caller can never mistake a
 * skeleton for a working I2C backend.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "hal/hal_i2c.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_i2c_bus_t hal_i2c_get_bus(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_i2c_transfer(hal_i2c_bus_t bus, hal_i2c_msg_t *msgs, uint16_t n)
{
	(void)bus;
	(void)msgs;
	(void)n;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_i2c_read_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
		     uint8_t *buf, uint16_t n)
{
	(void)bus;
	(void)dev_addr;
	(void)reg;
	(void)buf;
	(void)n;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_i2c_write_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
		      const uint8_t *buf, uint16_t n)
{
	(void)bus;
	(void)dev_addr;
	(void)reg;
	(void)buf;
	(void)n;
	s_unimpl_count++;
	return -ENOSYS;
}