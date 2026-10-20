/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * osal_mutex.c
 *
 * Zephyr backend for the OSAL mutex API - SKELETON.
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/osal/zephyr/osal_mutex.c); the bodies are not.
 * A real backend wraps a heap-allocated `struct k_mutex` (k_malloc +
 * k_mutex_init/lock/unlock) behind the opaque handle; nothing of that is
 * implemented here. Every op returns OSAL_ERR_NOTSUP and is counted, so a
 * caller can never mistake a skeleton for a working mutex.
 */
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "osal/osal.h"
#include "osal/osal_types.h"
#include "osal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

int osal_mutex_create(osal_mutex_t *m)
{
	(void)m;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_mutex_lock(osal_mutex_t m, uint32_t timeout_ms)
{
	(void)m;
	(void)timeout_ms;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_mutex_unlock(osal_mutex_t m)
{
	(void)m;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_mutex_destroy(osal_mutex_t m)
{
	(void)m;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}