/*
 * can.h - MOCK STUB for the Zephyr CAN driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_can.c (Zephyr v4.x):
 *   struct can_frame { id, dlc, flags, data[] }
 *   CAN_FRAME_IDE / CAN_FRAME_RTR / CAN_MAX_DLC
 *   struct can_filter { id, mask, flags }
 *   enum can_state
 *   can_send / can_add_rx_filter / can_remove_rx_filter / can_get_state
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_CAN_H_
#define ZEPHYR_INCLUDE_DRIVERS_CAN_H_

#include <stdint.h>
#include <stddef.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_STD_ID_MASK 0x7FFU
#define CAN_EXT_ID_MASK 0x1FFFFFFFU
#define CAN_MAX_DLC     8U

#define CAN_FRAME_IDE  BIT(0)
#define CAN_FRAME_RTR  BIT(1)

enum can_state {
	CAN_STATE_ERROR_ACTIVE,
	CAN_STATE_ERROR_WARNING,
	CAN_STATE_ERROR_PASSIVE,
	CAN_STATE_BUS_OFF,
	CAN_STATE_STOPPED,
};

struct can_frame {
	uint32_t id;
	uint8_t dlc;
	uint8_t flags;
	uint8_t data[8];
};

struct can_filter {
	uint32_t id;
	uint32_t mask;
	uint8_t flags;
};

typedef void (*can_tx_callback_t)(const struct device *dev, int error,
				  void *user_data);
typedef void (*can_rx_callback_t)(const struct device *dev,
				  struct can_frame *frame, void *user_data);

extern int can_send(const struct device *dev, const struct can_frame *frame,
		    int64_t timeout, can_tx_callback_t callback,
		    void *user_data);
extern int can_add_rx_filter(const struct device *dev,
			     can_rx_callback_t callback, void *user_data,
			     const struct can_filter *filter);
extern void can_remove_rx_filter(const struct device *dev, int filter_id);
extern int can_get_state(const struct device *dev, enum can_state *state,
			 void *err_cnt);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CAN_H_ */