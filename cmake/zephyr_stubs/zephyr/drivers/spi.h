/*
 * spi.h - MOCK STUB for the Zephyr SPI driver API (host-side testing only).
 *
 * Mirrors the Zephyr SPI API surface used by the backend:
 *   struct spi_config { frequency, operation, slave, cs }
 *   struct spi_cs_control { gpio (gpio_dt_spec), delay }
 *   struct spi_buf { buf, len } / struct spi_buf_set { buffers, count }
 *   SPI_OP_MODE_MASTER, SPI_MODE_CPOL/CPHA, SPI_WORD_SET()
 *   spi_transceive(dev, &config, &tx_bufs, &rx_bufs)
 *
 * Also provides gpio_dt_spec (normally pulled in from <zephyr/drivers/gpio.h>):
 * keeping it here keeps the stub self-contained.
 *
 * The link-time mock (tests/i2c_mock/) provides spi_transceive() and records
 * the arguments so the field mapping can be asserted.
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_SPI_H_
#define ZEPHYR_INCLUDE_DRIVERS_SPI_H_

#include <stdint.h>
#include <stddef.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- operations --- */
#define SPI_OP_MODE_MASTER	0U
#define SPI_MODE_CPOL		BIT(1)
#define SPI_MODE_CPHA		BIT(2)
#define SPI_WORD_SIZE_SHIFT	(5U)
#define SPI_WORD_SIZE_MASK	(0x3FU << SPI_WORD_SIZE_SHIFT)
#define SPI_WORD_SET(word)	((word) << SPI_WORD_SIZE_SHIFT)

typedef uint16_t spi_operation_t;

/* Minimal GPIO devicetree spec (from drivers/gpio.h in real Zephyr). */
struct gpio_dt_spec {
	const struct device *port;
	uint32_t pin;
	uint32_t dt_flags;
};

struct spi_cs_control {
	struct gpio_dt_spec gpio;
	uint32_t delay;
};

struct spi_config {
	uint32_t frequency;
	spi_operation_t operation;
	uint16_t slave;
	struct spi_cs_control cs;
};

struct spi_buf {
	void *buf;
	size_t len;
};

struct spi_buf_set {
	const struct spi_buf *buffers;
	size_t count;
};

/* Defined by the link-time mock (tests/i2c_mock/). */
extern int spi_transceive(const struct device *dev,
			  const struct spi_config *config,
			  const struct spi_buf_set *tx_bufs,
			  const struct spi_buf_set *rx_bufs);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SPI_H_ */