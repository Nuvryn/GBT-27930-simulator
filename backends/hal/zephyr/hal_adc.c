/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_adc.c
 *
 * Zephyr backend for the ADC HAL interface contract (hal_adc.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_adc.c); the bodies are not:
 *   hal_adc_get_dev -> NULL (contract: device not available)
 *   hal_adc_read    -> -ENOSYS
 *
 * No real Zephyr adc_channel_setup()/adc_read() logic is implemented.
 * Every operation is counted in s_unimpl_count so a caller can never
 * mistake a skeleton for a working ADC backend.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include "hal/hal_adc.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_adc_dev_t hal_adc_get_dev(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_adc_read(hal_adc_dev_t dev, uint8_t channel, uint32_t *value)
{
	(void)dev;
	(void)channel;
	(void)value;
	s_unimpl_count++;
	return -ENOSYS;
}