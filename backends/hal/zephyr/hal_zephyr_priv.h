/*
 * SKELETON — see .omo/plans/framework-port.md (todo #14)
 *
 * hal_zephyr_priv.h
 *
 * Internal helpers shared by all Zephyr HAL backends (ports/hal/zephyr).
 * This header is NOT part of the hal interface contract (see the headers
 * under the hal directory); do not include it from outside this port
 * directory.
 *
 * Skeleton: the framework original carried the device-discovery lookup
 * helper (hal_zephyr_dev_lookup), the shared-bus lock wrapper (M8) and the
 * timeout conversion. None of that driver logic exists here yet - only the
 * include set is kept so that a later real implementation slots in drop-in.
 * Every exported hal_* operation that is not device discovery returns
 * -ENOSYS.
 */
#ifndef HAL_ZEPHYR_PRIV_H
#define HAL_ZEPHYR_PRIV_H

#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#endif /* HAL_ZEPHYR_PRIV_H */