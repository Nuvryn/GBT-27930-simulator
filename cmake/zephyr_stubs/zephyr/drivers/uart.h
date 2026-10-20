/*
 * uart.h - MOCK STUB for the Zephyr UART driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_uart.c (Zephyr v4.x):
 *   struct uart_config + UART_CFG_* enums
 *   uart_configure(dev, &cfg)
 *   uart_poll_out(dev, byte)  [void in Zephyr 4.x]
 *   uart_poll_in(dev, &byte)  [-1 = no data]
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_UART_H_
#define ZEPHYR_INCLUDE_DRIVERS_UART_H_

#include <stdint.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

enum uart_config_parity {
	UART_CFG_PARITY_NONE,
	UART_CFG_PARITY_ODD,
	UART_CFG_PARITY_EVEN,
};

enum uart_config_stop_bits {
	UART_CFG_STOP_BITS_0_5,
	UART_CFG_STOP_BITS_1,
	UART_CFG_STOP_BITS_1_5,
	UART_CFG_STOP_BITS_2,
};

enum uart_config_data_bits {
	UART_CFG_DATA_BITS_5,
	UART_CFG_DATA_BITS_6,
	UART_CFG_DATA_BITS_7,
	UART_CFG_DATA_BITS_8,
	UART_CFG_DATA_BITS_9,
};

enum uart_config_flow_control {
	UART_CFG_FLOW_CTRL_NONE,
	UART_CFG_FLOW_CTRL_RTS_CTS,
};

struct uart_config {
	uint32_t baudrate;
	uint8_t parity;
	uint8_t stop_bits;
	uint8_t data_bits;
	uint8_t flow_ctrl;
};

extern int uart_configure(const struct device *dev,
			  const struct uart_config *cfg);
extern void uart_poll_out(const struct device *dev, unsigned char out_char);
extern int uart_poll_in(const struct device *dev, unsigned char *p_char);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_UART_H_ */