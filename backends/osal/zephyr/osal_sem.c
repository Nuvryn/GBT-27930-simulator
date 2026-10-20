/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * osal_sem.c
 *
 * Zephyr backend for the OSAL counting semaphore API - SKELETON.
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/osal/zephyr/osal_sem.c); the bodies are not.
 * A real backend wraps a heap-allocated `struct k_sem` (k_malloc +
 * k_sem_init/take/give) behind the opaque handle; nothing of that is
 * implemented here. Every op returns OSAL_ERR_NOTSUP and is counted.
 */
#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "osal/osal.h"
#include "osal/osal_types.h"
#include "osal_zephyr_priv.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

int osal_sem_create(osal_sem_t *s, uint32_t init_count, uint32_t max_count)
{
	(void)s;
	(void)init_count;
	(void)max_count;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_sem_wait(osal_sem_t s, uint32_t timeout_ms)
{
	(void)s;
	(void)timeout_ms;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_sem_post(osal_sem_t s)
{
	(void)s;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}

int osal_sem_destroy(osal_sem_t s)
{
	(void)s;
	s_unimpl_count++;
	return OSAL_ERR_NOTSUP;
}