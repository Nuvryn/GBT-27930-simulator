/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_flash.c
 *
 * Zephyr backend for the flash HAL interface contract (hal_flash.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_flash.c); the bodies are not:
 *   hal_flash_get_dev    -> NULL (contract: device not available)
 *   hal_flash_read       -> -ENOSYS
 *   hal_flash_write      -> -ENOSYS
 *   hal_flash_erase      -> -ENOSYS
 *   hal_flash_get_info   -> -ENOSYS
 *
 * No real Zephyr flash_read()/flash_erase()/flash_get_page_* logic is
 * implemented. Every operation is counted in s_unimpl_count so a caller
 * can never mistake a skeleton for a working flash backend.
 */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>

#include "hal/hal_flash.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_flash_dev_t hal_flash_get_dev(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_flash_read(hal_flash_dev_t dev, uint32_t addr, uint8_t *buf,
		   size_t len)
{
	(void)dev;
	(void)addr;
	(void)buf;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_flash_write(hal_flash_dev_t dev, uint32_t addr, const uint8_t *buf,
		    size_t len)
{
	(void)dev;
	(void)addr;
	(void)buf;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_flash_erase(hal_flash_dev_t dev, uint32_t addr, size_t len)
{
	(void)dev;
	(void)addr;
	(void)len;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_flash_get_info(hal_flash_dev_t dev, uint32_t *sector_size,
		       uint32_t *total_size)
{
	(void)dev;
	(void)sector_size;
	(void)total_size;
	s_unimpl_count++;
	return -ENOSYS;
}