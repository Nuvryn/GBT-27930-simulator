/*
 * entropy.h - MOCK STUB for the Zephyr entropy driver API (host-side testing
 * only).
 *
 * Mirrors the API used by hal_rng.c (Zephyr v4.x):
 *   entropy_get_entropy(dev, buffer, length)
 *   (length is uint16_t in Zephyr; > UINT16_MAX must be chunked.)
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_ENTROPY_H_
#define ZEPHYR_INCLUDE_DRIVERS_ENTROPY_H_

#include <stdint.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int entropy_get_entropy(const struct device *dev, uint8_t *buffer,
			       uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ENTROPY_H_ */