/*
 * hal_uart.h - UART interface contract
 *
 * Configuration + blocking read/write API. Backend mapping:
 *   Zephyr: hal_uart_init -> uart_configure(dev, &uart_config),
 *           hal_uart_write -> uart_fifo_fill/uart_poll_out or uart_tx
 *   Linux : hal_uart_init -> termios tcsetattr(), hal_uart_write/read ->
 *           write()/read() on the tty fd
 *
 * Pure C99/C11 contract. No Zephyr/Linux headers included.
 * Error code convention: int return, 0 = success, negative = -errno.
 */

#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#include "hal.h"

/**
 * @brief UART parity setting.
 */
typedef enum {
    HAL_UART_PARITY_NONE = 0,
    HAL_UART_PARITY_ODD,
    HAL_UART_PARITY_EVEN,
} hal_uart_parity_t;

/**
 * @brief UART line configuration.
 */
typedef struct {
    uint32_t          baud;       /* Baud rate, e.g. 115200            */
    hal_uart_parity_t parity;     /* None / Odd / Even                 */
    uint8_t           data_bits;  /* 7 / 8 / 9                         */
    uint8_t           stop_bits;  /* 1 / 2                             */
    uint8_t           flow_ctrl;  /* 0 = none, 1 = RTS/CTS             */
} hal_uart_config_t;

/**
 * @brief Apply a line configuration to an opened UART device.
 *
 * @param dev Opaque UART device handle from hal_uart_get_bus().
 * @param cfg Line configuration to apply.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_uart_init(hal_uart_dev_t dev, const hal_uart_config_t *cfg);

/**
 * @brief Blocking write of len bytes.
 *
 * @param dev  Opaque UART device handle.
 * @param data Bytes to transmit.
 * @param len  Number of bytes to transmit.
 * @return Number of bytes written, or negative -errno on failure.
 */
int hal_uart_write(hal_uart_dev_t dev, const void *data, size_t len);

/**
 * @brief Blocking read of up to len bytes with a timeout.
 *
 * @param dev        Opaque UART device handle.
 * @param buf        Buffer to store received bytes.
 * @param len        Maximum number of bytes to read.
 * @param timeout_ms Timeout in milliseconds (0 = poll once,
 *                   UINT32_MAX = wait forever).
 * @return Number of bytes read, 0 on timeout, negative -errno on failure.
 */
int hal_uart_read(hal_uart_dev_t dev, void *buf, size_t len,
                  uint32_t timeout_ms);

#endif /* HAL_UART_H */