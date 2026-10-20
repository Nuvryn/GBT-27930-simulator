/*
 * flash.h - MOCK STUB for the Zephyr flash driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_flash.c (Zephyr v4.x):
 *   flash_read / flash_write / flash_erase
 *   struct flash_pages_info + flash_get_page_info_by_offs
 *   flash_get_page_count / flash_get_parameters
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_FLASH_H_
#define ZEPHYR_INCLUDE_DRIVERS_FLASH_H_

#include <stdint.h>
#include <stddef.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zephyr's flash API uses off_t (from <zephyr/types.h>/<sys/types.h>); the
 * self-contained stub mirrors it as int64_t for the (off_t) casts in
 * hal_flash.c. */
typedef int64_t off_t;

struct flash_pages_info {
	uint32_t start_offset;
	size_t size;
	uint32_t index;
};

struct flash_parameters {
	size_t write_block_size;
	uint8_t erase_value;
	uint8_t caps;
};

extern int flash_read(const struct device *dev, int64_t offset, void *data,
		      size_t len);
extern int flash_write(const struct device *dev, int64_t offset,
		       const void *data, size_t len);
extern int flash_erase(const struct device *dev, int64_t offset, size_t size);
extern int flash_get_page_info_by_offs(const struct device *dev,
				       uint32_t offset,
				       struct flash_pages_info *info);
extern size_t flash_get_page_count(const struct device *dev);
extern const struct flash_parameters *flash_get_parameters(
	const struct device *dev);

static inline int flash_get_page_info_by_idx(const struct device *dev,
					     uint32_t page_index,
					     struct flash_pages_info *info)
{
	return flash_get_page_info_by_offs(dev, page_index, info);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_FLASH_H_ */