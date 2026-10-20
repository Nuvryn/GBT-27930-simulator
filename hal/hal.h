/*
 * hal.h - HAL base types and common definitions
 *
 * Cross-platform Hardware Abstraction Layer interface contract.
 * Pure C99/C11 function-style API: no device model, no vtable, no
 * Zephyr/Linux headers. Backends (ports/hal/<zephyr|linux>/) wrap the
 * native device objects behind opaque void* handles.
 *
 * Error code convention (shared with osal):
 *   int return, 0 = success, negative = -errno (e.g. -EINVAL/-ENOMEM/-EIO).
 */

#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include <stddef.h>

/*
 * Opaque device handles.
 *
 * Backends wrap the platform-native device object behind these void*:
 *   Zephyr   -> (void *)const struct device *   (e.g. DEVICE_DT_GET(...))
 *   Linux    -> (void *)(uintptr_t)fd            (e.g. open("/dev/i2c-1"))
 * NULL means "device not available / not opened yet".
 */
typedef void *hal_i2c_bus_t;
typedef void *hal_spi_bus_t;
typedef void *hal_uart_dev_t;

/* Generic success code (negative values are -errno). */
#define HAL_OK 0

/*
 * I2C message flags (bitmask for hal_i2c_msg_t.flags).
 */
#define HAL_I2C_RD         (1u << 0)   /* Read (bus -> host)              */
#define HAL_I2C_WR         (1u << 1)   /* Write (host -> bus)             */
#define HAL_I2C_NO_STOP    (1u << 2)   /* Do not emit STOP; keep bus for
                                          repeated start across messages  */
#define HAL_I2C_ADDR_10BIT (1u << 3)   /* 10-bit slave addressing mode    */

#endif /* HAL_H */