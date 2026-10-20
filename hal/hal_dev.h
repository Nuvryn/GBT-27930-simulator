/*
 * hal_dev.h - device discovery interface contract
 *
 * Logical device lookup for every peripheral class. A caller names the
 * device it wants with a logical string (e.g. "i2c1", "uart3", "gpioa")
 * and receives the opaque handle that class's bus/dev API then operates on.
 *
 * Backend mapping:
 *   Zephyr: name is a devicetree node-label
 *           -> DEVICE_DT_GET(DT_NODELABEL(xxx)) wrapped in the opaque handle
 *   Linux : name is a /dev device name
 *           -> open("/dev/xxx", O_RDWR), fd wrapped in the opaque handle
 *
 * Handle convention (C1): every hal_xxx_bus_t / hal_xxx_dev_t is an opaque
 * void* — the backend wraps the platform-native device object behind it.
 *   Zephyr -> (void *)const struct device *   (e.g. DEVICE_DT_GET(...))
 *   Linux  -> (void *)(uintptr_t)fd            (e.g. open("/dev/i2c-1"))
 * NULL means "device not ready / not found / not opened".
 *
 * Pure C99/C11 contract. No Zephyr/Linux headers included.
 * Backends (ports/hal/<zephyr|linux>/) provide these functions.
 */

#ifndef HAL_DEV_H
#define HAL_DEV_H

#include "hal.h"
#include "hal_i2c.h"
#include "hal_spi.h"
#include "hal_uart.h"
#include "hal_can.h"
#include "hal_gpio.h"
#include "hal_adc.h"
#include "hal_pwm.h"
#include "hal_flash.h"
#include "hal_rng.h"

/**
 * @brief Discover an I2C bus by logical name.
 * @param name Logical device name, e.g. "i2c1"/"i2c2".
 * @return Opaque bus handle, or NULL if the device is not ready.
 */
hal_i2c_bus_t hal_i2c_get_bus(const char *name);

/**
 * @brief Discover an SPI bus by logical name.
 * @param name Logical device name, e.g. "spi1"/"spi2".
 * @return Opaque bus handle, or NULL if the device is not ready.
 */
hal_spi_bus_t hal_spi_get_bus(const char *name);

/**
 * @brief Discover a UART device by logical name.
 * @param name Logical device name, e.g. "uart1"/"uart3".
 * @return Opaque device handle, or NULL if the device is not ready.
 */
hal_uart_dev_t hal_uart_get_bus(const char *name);

/**
 * @brief Discover a CAN controller by logical name.
 * @param name Logical device name, e.g. "can0"/"can1".
 * @return Opaque bus handle, or NULL if the device is not ready.
 */
hal_can_bus_t hal_can_get_bus(const char *name);

/**
 * @brief Discover a GPIO port by logical name.
 * @param name Logical port name, e.g. "gpioa"/"gpiob".
 * @return Opaque port handle, or NULL if the port is not ready.
 */
hal_gpio_port_t hal_gpio_get_port(const char *name);

/**
 * @brief Discover an ADC device by logical name.
 * @param name Logical device name, e.g. "adc1"/"adc2".
 * @return Opaque device handle, or NULL if the device is not ready.
 */
hal_adc_dev_t hal_adc_get_dev(const char *name);

/**
 * @brief Discover a PWM device by logical name.
 * @param name Logical device name, e.g. "pwm1"/"pwm2".
 * @return Opaque device handle, or NULL if the device is not ready.
 */
hal_pwm_dev_t hal_pwm_get_dev(const char *name);

/**
 * @brief Discover a flash device by logical name.
 * @param name Logical device name, e.g. "flash0"/"w25q128".
 * @return Opaque device handle, or NULL if the device is not ready.
 */
hal_flash_dev_t hal_flash_get_dev(const char *name);

/**
 * @brief Discover a random number generator by logical name.
 * @param name Logical device name, e.g. "rng0"/"trng".
 * @return Opaque device handle, or NULL if the device is not ready.
 */
hal_rng_dev_t hal_rng_get_dev(const char *name);

#endif /* HAL_DEV_H */