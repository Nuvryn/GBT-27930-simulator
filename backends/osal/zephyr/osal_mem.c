/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * osal_mem.c
 *
 * Zephyr backend for the OSAL heap memory API - SKELETON.
 *
 * The exported symbol set and signatures are identical to the framework
 * implementation (ports/osal/zephyr/osal_mem.c); the bodies are not.
 * A real backend maps osal_malloc/osal_free to k_malloc/k_free; nothing of
 * that is implemented here. osal_malloc returns NULL (the contract's OOM
 * outcome, never a fabricated allocation) and counts the call; osal_free is
 * a counted no-op (file-local counter; NOT externally observable).
 */
#include <stddef.h>
#include <zephyr/kernel.h>

#include "osal/osal.h"
#include "osal/osal_types.h"

/* File-local diagnostic counter for unimplemented skeleton calls (NOT externally observable). */
static uint32_t s_unimpl_count = 0;

void *osal_malloc(size_t size)
{
	(void)size;
	s_unimpl_count++;
	/* NULL is the contract's out-of-memory outcome; a skeleton must not
	 * hand out allocations pretend to be backed by k_malloc.
	 */
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
	return NULL;
}

void osal_free(void *ptr)
{
	(void)ptr;
	s_unimpl_count++;
	(void)OSAL_ERR_NOTSUP; /* marker: operation not implemented */
}