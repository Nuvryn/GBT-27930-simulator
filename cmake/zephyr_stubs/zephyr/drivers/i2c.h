/*
 * i2c.h - MOCK STUB for the Zephyr I2C driver API (host-side testing only).
 *
 * Mirrors the Zephyr v3.7/v4.1 I2C API surface used by the backend:
 *   struct i2c_msg { uint8_t *buf; uint32_t len; uint8_t flags; }
 *   I2C_MSG_WRITE / I2C_MSG_READ / I2C_MSG_STOP / I2C_MSG_RESTART /
 *   I2C_MSG_ADDR_10_BITS
 *   i2c_transfer(dev, msgs, num_msgs, addr)
 *
 * The link-time mock (tests/i2c_mock/) provides i2c_transfer() and records the
 * arguments so the field mapping can be asserted.
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_I2C_H_
#define ZEPHYR_INCLUDE_DRIVERS_I2C_H_

#include <stdint.h>
#include <stddef.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C message flags (values identical in Zephyr). */
#define I2C_MSG_WRITE			(0U << 0U)
#define I2C_MSG_READ			BIT(0)
#define I2C_MSG_STOP			BIT(1)
#define I2C_MSG_RESTART			BIT(2)
#define I2C_MSG_ADDR_10_BITS		BIT(3)

struct i2c_msg {
	uint8_t		*buf;
	uint32_t	len;
	uint8_t		flags;
};

/* Defined by the link-time mock (tests/i2c_mock/). */
extern int i2c_transfer(const struct device *dev, struct i2c_msg *msgs,
			uint8_t num_msgs, uint16_t addr);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_I2C_H_ */