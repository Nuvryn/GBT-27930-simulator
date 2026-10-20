/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_rng.c
 *
 * Zephyr backend for the RNG HAL interface contract (hal_rng.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_rng.c); the bodies are not:
 *   hal_rng_get_dev -> NULL (contract: device not available)
 *   hal_rng_read    -> -ENOSYS
 *
 * No real Zephyr entropy_get_entropy() logic is implemented. Every
 * operation is counted in s_unimpl_count so a caller can never mistake a
 * skeleton for a working RNG backend.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/kernel.h>

#include "hal/hal_rng.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_rng_dev_t hal_rng_get_dev(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_rng_read(hal_rng_dev_t dev, uint8_t *buf, size_t len)
{
	(void)dev;
	(void)buf;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}