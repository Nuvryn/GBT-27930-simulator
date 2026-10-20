/*
 * hal_flash.h
 *
 * HAL (Hardware Abstraction Layer) NOR flash interface contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The device is an opaque `void *` handle; the concrete object lives in
 *    the backend (Zephyr `const struct device` / Linux MTD fd or path).
 *    The application never dereferences it.
 *  - Addresses are absolute byte offsets into the flash device address space
 *    (0 .. total_size-1).
 *  - Erase granularity is the sector: @ref hal_flash_erase range must be
 *    sector-aligned (addr % sector_size == 0, len % sector_size == 0).
 *    Query sector/geometry via @ref hal_flash_get_info.
 *  - Writes are byte-wise from the caller's perspective; the backend merges
 *    partial sectors into a read-modify-write when the device requires it.
 *    Flash is overwrite-restricted: a region must be erased before it can be
 *    rewritten.
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-EIO`, ...) on failure.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_flash.h
 */
#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** Flash device handle (backend: Zephyr `const struct device` / Linux MTD). */
typedef void *hal_flash_dev_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Read bytes from flash.
 *
 * @param dev Flash device handle.
 * @param addr Absolute byte offset to read from.
 * @param buf  Destination buffer (at least @p len bytes).
 * @param len  Number of bytes to read.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_flash_read(hal_flash_dev_t dev, uint32_t addr, uint8_t *buf, size_t len);

/**
 * @brief Write bytes to flash (region must be erased beforehand).
 *
 * @param dev Flash device handle.
 * @param addr Absolute byte offset to write to.
 * @param buf  Source buffer (at least @p len bytes).
 * @param len  Number of bytes to write.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_flash_write(hal_flash_dev_t dev, uint32_t addr, const uint8_t *buf, size_t len);

/**
 * @brief Erase a sector-aligned range of flash.
 *
 * @p addr must be a multiple of sector_size and @p len a multiple of
 * sector_size; the backend erases the covering sectors. After erasing, all
 * bytes in the range read back as 0xFF.
 *
 * @param dev  Flash device handle.
 * @param addr Absolute byte offset of the first sector to erase.
 * @param len  Total number of bytes to erase (sector-aligned).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_flash_erase(hal_flash_dev_t dev, uint32_t addr, size_t len);

/**
 * @brief Query flash geometry.
 *
 * @param dev         Flash device handle.
 * @param sector_size Out parameter receiving the erase sector size in bytes
 *                    (may be NULL if not needed).
 * @param total_size  Out parameter receiving the total flash size in bytes
 *                    (may be NULL if not needed).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_flash_get_info(hal_flash_dev_t dev, uint32_t *sector_size, uint32_t *total_size);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */