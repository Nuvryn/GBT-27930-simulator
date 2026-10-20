/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_can.c
 *
 * Zephyr backend for the CAN HAL interface contract (hal_can.h).
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/hal/zephyr/hal_can.c); the bodies are not:
 *   hal_can_get_bus         -> NULL (contract: device not available)
 *   hal_can_send            -> -ENOSYS
 *   hal_can_add_rx_filter   -> -ENOSYS
 *   hal_can_remove_filter   -> -ENOSYS
 *   hal_can_get_state       -> -ENOSYS
 *
 * No real Zephyr can_send()/can_add_rx_filter()/callback-translation logic
 * is implemented. Every operation is counted in s_unimpl_count so a caller
 * can never mistake a skeleton for a working CAN backend.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

#include "hal/hal_can.h"
#include "hal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

hal_can_bus_t hal_can_get_bus(const char *name)
{
	(void)name;
	/* Contract: device discovery is not available in the skeleton. */
	return NULL;
}

int hal_can_send(hal_can_bus_t bus, const hal_can_msg_t *msg,
		 uint32_t timeout_ms, hal_can_done_cb_t done_cb, void *cb_arg)
{
	(void)bus;
	(void)msg;
	(void)timeout_ms;
	(void)done_cb;
	(void)cb_arg;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_can_add_rx_filter(hal_can_bus_t bus, const hal_can_filter_t *filter,
			  hal_can_rx_cb_t cb, void *user_data)
{
	(void)bus;
	(void)filter;
	(void)cb;
	(void)user_data;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_can_remove_filter(hal_can_bus_t bus, int filter_id)
{
	(void)bus;
	(void)filter_id;
	s_unimpl_count++;
	return -ENOSYS;
}

int hal_can_get_state(hal_can_bus_t bus, hal_can_state_t *state)
{
	(void)bus;
	(void)state;
	s_unimpl_count++;
	return -ENOSYS;
}