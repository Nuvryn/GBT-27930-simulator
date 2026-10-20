/*
 * osal_mem.c
 *
 * POSIX backend for OSAL memory allocation (M3).
 * osal_malloc/osal_free map directly onto malloc/free so the same binary can
 * be re-linked against the Zephyr backend (k_malloc/k_free) unchanged.
 */
#include <stddef.h>
#include <stdlib.h>
#include "osal/osal.h"

void *osal_malloc(size_t size)
{
	if (size == 0u)
		return NULL; /* contract: NULL for size == 0 */
	return malloc(size);
}

void osal_free(void *ptr)
{
	free(ptr); /* free(NULL) is a safe no-op, matching the contract */
}