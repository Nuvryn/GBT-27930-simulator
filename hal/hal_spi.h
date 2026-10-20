/*
 * hal_spi.h - SPI interface contract
 *
 * Message-based and bulk transceive API. Field mapping to the platform
 * native SPI APIs:
 *   Zephyr: tx_buf->spi_buf_set.buffers[0].buf, rx_buf->spi_buf_set.buffers[1].buf,
 *           len->spi_buf.len, speed_hz->spi_config.frequency,
 *           cs_gpio->spi_config.cs.gpio, mode->CPOL/CPHA encoding
 *   Linux : tx_buf->spi_ioc_transfer.tx_buf, rx_buf->.rx_buf, len->.len,
 *           speed_hz->.speed_hz, cs_gpio->.cs_change, mode->CPOL/CPHA encoding
 *
 * Pure C99/C11 contract. No Zephyr/Linux headers included.
 * Error code convention: int return, 0 = success, negative = -errno.
 */

#ifndef HAL_SPI_H
#define HAL_SPI_H

#include <stdint.h>
#include <stddef.h>

#include "hal.h"

/**
 * @brief A single SPI transfer segment.
 *
 * tx_buf may be NULL for a pure read; rx_buf may be NULL for a pure write.
 * mode is 0-3 (CPOL/CPHA), shared by both backends.
 */
typedef struct {
    uint8_t  *tx_buf;    /* Transmit payload (NULL = read-only)   */
    uint8_t  *rx_buf;    /* Receive payload  (NULL = write-only)  */
    size_t    len;       /* Transfer length in bytes              */
    uint32_t  speed_hz;  /* Bus clock frequency for this segment  */
    uint8_t   cs_gpio;   /* Chip-select pin / cs_change           */
    uint8_t   mode;      /* SPI mode 0-3 (CPOL/CPHA encoding)     */
} hal_spi_msg_t;

/**
 * @brief Execute a multi-segment SPI transaction.
 *
 * Segments execute back-to-back on a single chip-select assertion
 * (deasserted only after the last segment), matching the backend
 * ioc_transfer / spi_transceive semantics.
 *
 * @param bus  Opaque SPI bus handle from hal_spi_get_bus().
 * @param msgs Array of transfer segments.
 * @param n    Number of segments in msgs.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_spi_transfer(hal_spi_bus_t bus, hal_spi_msg_t *msgs, uint16_t n);

/**
 * @brief Single full-duplex transceive: write tx bytes, read rx bytes.
 *
 * Convenience one-shot form of hal_spi_transfer() with one segment of
 * length len and the backend default speed/mode/cs.
 *
 * @param bus Opaque SPI bus handle.
 * @param tx  Transmit buffer (NULL = read-only).
 * @param rx  Receive buffer  (NULL = write-only).
 * @param len Number of bytes to exchange.
 * @return HAL_OK on success, negative -errno otherwise.
 */
int hal_spi_transceive(hal_spi_bus_t bus, const uint8_t *tx, uint8_t *rx,
                       size_t len);

#endif /* HAL_SPI_H */