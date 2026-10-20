/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_spi.c
 *
 * Zephyr backend for the SPI HAL interface contract (hal_spi.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_spi.c); the bodies are not:
 *   hal_spi_get_bus    -> NULL (contract: device not available)
 *   hal_spi_transfer   -> -ENOSYS
 *   hal_spi_transceive -> -ENOSYS
 *
 * No real Zephyr spi_transceive()/device-table logic is implemented. Every
 * operation is counted in s_unimpl_count so a caller can never mistake a
 * skeleton for a working SPI backend.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "hal/hal_spi.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_spi_bus_t hal_spi_get_bus(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_spi_transfer(hal_spi_bus_t bus, hal_spi_msg_t *msgs, uint16_t n)
{
	(void)bus;
	(void)msgs;
	(void)n;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_spi_transceive(hal_spi_bus_t bus, const uint8_t *tx, uint8_t *rx,
		       size_t len)
{
	(void)bus;
	(void)tx;
	(void)rx;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}