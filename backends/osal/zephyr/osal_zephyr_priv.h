/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * osal_zephyr_priv.h
 *
 * Internal helpers shared by the Zephyr OSAL backend (ports/osal/zephyr).
 * This header is NOT part of the osal.h interface contract; it exists only
 * to keep port-internal pieces in one place. Do not include it from outside
 * this port directory.
 *
 * Skeleton: the framework original carried a k_timeout_t conversion helper
 * (K_FOREVER / K_NO_WAIT / K_MSEC) that a real backend will fill in later.
 * For now nothing but the include set survives, so this header contributes
 * no driver / OS logic. Keeping the same shape (same guards, same includes)
 * makes the later real implementation a drop-in.
 */
#ifndef OSAL_ZEPHYR_PRIV_H
#define OSAL_ZEPHYR_PRIV_H

#include <stdint.h>

#include <zephyr/kernel.h>

#include "osal/osal_types.h"

#endif /* OSAL_ZEPHYR_PRIV_H */