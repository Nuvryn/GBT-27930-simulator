/*
 * osal_posix_internal.c
 *
 * Private helper implementation for the POSIX OSAL backend.
 */
#define _POSIX_C_SOURCE 200809L /* expose POSIX.1-2008 APIs under -std=c11 */
#include "osal_posix_internal.h"

void osal_posix_deadline(uint32_t timeout_ms, struct timespec *abs)
{
	long long ns;

	/* POSIX timed waits take an absolute CLOCK_REALTIME deadline. */
	(void)clock_gettime(CLOCK_REALTIME, abs);

	ns = (long long)abs->tv_nsec + (long long)(timeout_ms % 1000u) * 1000000LL;
	abs->tv_sec += (time_t)(timeout_ms / 1000u) + (time_t)(ns / 1000000000LL);
	abs->tv_nsec = (long)(ns % 1000000000LL);
}