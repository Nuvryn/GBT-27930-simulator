/*
 * hal_i2c.h - I2C interface contract
 *
 * Multi-message transaction API supporting repeated start (NO_STOP).
 * Backend mapping:
 *   Zephyr: hal_i2c_transfer -> i2c_transfer(dev, i2c_msgs, num_msgs)
 *   Linux : hal_i2c_transfer -> ioctl(fd, I2C_RDWR, &i2c_rdwr_ioctl_data)
 *
 * Pure C99/C11 contract. No Zephyr/Linux headers included.
 * Error code convention: int return, 0 = success, negative = -errno.
 */

#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <stdint.h>
#include <stddef.h>

#include "hal.h"

/**
 * @brief A single I2C message within a transaction.
 *
 * All messages in one hal_i2c_transfer() call share the same bus; setting
 * HAL_I2C_NO_STOP on a message keeps the bus held (repeated start) so the
 * next message continues without a STOP/START gap. The typical read-from-
 * register pattern is one WR|NO_STOP + one RD message.
 */
typedef struct {
    uint16_t addr;   /* 7-bit or 10-bit slave address                */
    uint16_t flags;  /* HAL_I2C_RD / HAL_I2C_WR / HAL_I2C_NO_STOP /
                        HAL_I2C_ADDR_10BIT                          */
    uint8_t *buf;    /* Message payload (read or write direction)    */
    uint16_t len;    /* Number of bytes in buf                       */
} hal_i2c_msg_t;

/**
 * @brief Execute a multi-message I2C transaction (supports repeated start).
 *
 * @param bus   Opaque I2C bus handle from hal_i2c_get_bus().
 * @param msgs  Array of messages; direction taken from msg.flags.
 * @param n     Number of messages in msgs.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_i2c_transfer(hal_i2c_bus_t bus, hal_i2c_msg_t *msgs, uint16_t n);

/**
 * @brief Read n bytes from an 8-bit register of a device.
 *
 * Convenience helper built on hal_i2c_transfer(): emits a WR|NO_STOP
 * message (reg) followed by an RD message (buf) - repeated start.
 *
 * @param bus       Opaque I2C bus handle.
 * @param dev_addr  7-bit slave address.
 * @param reg       Register offset to read from.
 * @param buf       Output buffer for the read data.
 * @param n         Number of bytes to read.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_i2c_read_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
                     uint8_t *buf, uint16_t n);

/**
 * @brief Write n bytes to an 8-bit register of a device.
 *
 * Convenience helper built on hal_i2c_transfer(): emits a single WR
 * message whose payload is [reg, buf...].
 *
 * @param bus       Opaque I2C bus handle.
 * @param dev_addr  7-bit slave address.
 * @param reg       Register offset to write to.
 * @param buf       Data to write.
 * @param n         Number of bytes to write.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_i2c_write_reg(hal_i2c_bus_t bus, uint8_t dev_addr, uint8_t reg,
                      const uint8_t *buf, uint16_t n);

#endif /* HAL_I2C_H */