/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * osal_task.c
 *
 * Zephyr backend for the OSAL task and time APIs - SKELETON.
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/osal/zephyr/osal_task.c); the bodies are not:
 *
 *   osal_task_create   -> OSAL_ERR_NOTSUP (not implemented)
 *   osal_task_delete   -> counted no-op (v1 has no task handle; the call is counted, not silently successful)
 *   osal_task_yield    -> counted no-op
 *   osal_delay_ms      -> counted no-op
 *   osal_delay_us      -> counted no-op
 *   osal_get_tick_ms   -> returns (uint32_t)OSAL_ERR_NOTSUP, counted
 *
 * No real Zephyr calls are made; a real backend (k_thread_create /
 * k_msleep / k_uptime_get_32) fills these in later. Every unimplemented op
 * is tracked by a file-local s_unimpl_count (NOT externally observable); nothing here fabricates success.
 */
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "osal/osal.h"
#include "osal/osal_types.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

int osal_task_create(const osal_task_attr_t *attr)
{
	(void)attr;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

void osal_task_delete(osal_task_t task)
{
	(void)task;
	s_unimpl_count++;
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
}

void osal_task_yield(void)
{
	s_unimpl_count++;
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
}

void osal_delay_ms(uint32_t ms)
{
	(void)ms;
	s_unimpl_count++;
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
}

void osal_delay_us(uint32_t us)
{
	(void)us;
	s_unimpl_count++;
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
}

uint32_t osal_get_tick_ms(void)
{
	s_unimpl_count++;
	return (uint32_t)OSAL_ERR_NOTSUP;
}