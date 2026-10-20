/*
 * util.h - MOCK STUB for Zephyr's utility macros (host-side testing only).
 *
 * Provides the small macro set the backend uses (normally from Zephyr's
 * <zephyr/sys/util.h>): ARRAY_SIZE, BIT, CONTAINER_OF.
 */
#ifndef ZEPHYR_INCLUDE_SYS_UTIL_H_
#define ZEPHYR_INCLUDE_SYS_UTIL_H_

#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

#ifndef CONTAINER_OF
#define CONTAINER_OF(ptr, type, field) \
	((type *)(((char *)(ptr)) - offsetof(type, field)))
#endif

#endif /* ZEPHYR_INCLUDE_SYS_UTIL_H_ */