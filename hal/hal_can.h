/*
 * hal_can.h
 *
 * HAL (Hardware Abstraction Layer) CAN bus interface contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The bus is an opaque `void *` handle; the concrete object lives in the
 *    backend (Zephyr `const struct device` / Linux SocketCAN socket fd).
 *    The application never dereferences it.
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-ENOMEM`, ...) on
 *    failure.
 *  - Sending is asynchronous: `hal_can_send` returns once the frame is
 *    queued; `done_cb` is invoked when the transmission actually completes
 *    (Zephyr: `can_send` + `can_send_async` callback; Linux: a worker thread
 *    simulates the async completion on top of the blocking socket).
 *  - Not callable from ISR context (M7): no `_from_isr` variants.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_can.h
 */
#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** CAN bus handle (backend: Zephyr `const struct device` / Linux can fd). */
typedef void *hal_can_bus_t;

/* ------------------------------------------------------------------------- */
/* Message types                                                             */
/* ------------------------------------------------------------------------- */

/** Message flags (bitmask, see @ref HAL_CAN_EXT_ID / @ref HAL_CAN_RTR). */
#define HAL_CAN_EXT_ID (1 << 0) /**< @brief The @ref hal_can_msg_t.id is a 29-bit extended ID. */
#define HAL_CAN_RTR    (1 << 1) /**< @brief Remote Transmission Request frame (no payload). */

/**
 * @brief A single CAN frame (CAN 2.0).
 *
 * @p id is 11-bit (standard) or 29-bit (extended when @ref HAL_CAN_EXT_ID).
 * @p data holds up to 8 payload bytes; @p len is 0-8. For RTR frames
 * @p len is 0 and @p data is ignored.
 */
typedef struct {
	/** Frame identifier: 11-bit standard or 29-bit extended. */
	uint32_t id;
	/** Payload bytes (8 max for CAN 2.0). */
	uint8_t  data[8];
	/** Payload length in bytes, 0-8. */
	uint8_t  len;
	/** Flag bitmask: @ref HAL_CAN_EXT_ID | @ref HAL_CAN_RTR. */
	uint8_t  flags;
} hal_can_msg_t;

/** RX filter: a frame matches when (frame_id & mask) == (filter.id & mask). */
typedef struct {
	/** Filter id. */
	uint32_t id;
	/** Acceptance mask; 0xFFFFFFFF matches exactly, 0x0 matches all. */
	uint32_t mask;
} hal_can_filter_t;

/* ------------------------------------------------------------------------- */
/* Callbacks                                                                 */
/* ------------------------------------------------------------------------- */

/**
 * @brief RX callback, invoked from backend context when a matching frame
 *        arrives.
 *
 * The message pointer is valid only for the duration of the callback.
 * Keep the callback short; do real work in a task (M7).
 */
typedef void (*hal_can_rx_cb_t)(void *user_data, const hal_can_msg_t *msg);

/**
 * @brief Async send completion callback.
 *
 * @p status is 0 on success or a negative errno (`-ETIMEDOUT`, `-EIO`, ...).
 * @p arg is the `cb_arg` passed to @ref hal_can_send.
 */
typedef void (*hal_can_done_cb_t)(void *arg, int status);

/* ------------------------------------------------------------------------- */
/* Controller state                                                          */
/* ------------------------------------------------------------------------- */

/** CAN controller error state. */
typedef enum {
	HAL_CAN_STATE_ERROR_ACTIVE = 0, /**< Controller is in the normal error-active state. */
	HAL_CAN_STATE_ERROR_PASSIVE,     /**< Controller is in the error-passive state. */
	HAL_CAN_STATE_BUS_OFF,           /**< Controller is bus-off and no longer participates on the bus. */
} hal_can_state_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Queue a CAN frame for asynchronous transmission.
 *
 * Returns once the frame is accepted by the driver. When the transmission
 * completes (or fails), @p done_cb is invoked with @p cb_arg and the
 * completion status.
 *
 * @param bus         CAN bus handle.
 * @param msg         Frame to send.
 * @param timeout_ms  Max time to wait for the send to complete, in ms
 *                    (a dedicated TX timeout, not a lifetime bound).
 * @param done_cb     Completion callback; may be NULL on backends that
 *                    only support synchronous sending.
 * @param cb_arg      Opaque argument forwarded to @p done_cb.
 *
 * @return 0 on success (frame queued), negative errno on failure.
 */
int hal_can_send(hal_can_bus_t bus, const hal_can_msg_t *msg, uint32_t timeout_ms,
		 hal_can_done_cb_t done_cb, void *cb_arg);

/**
 * @brief Register an RX filter with its callback.
 *
 * Each frame matching @p filter is delivered to @p cb with @p user_data.
 *
 * @param bus       CAN bus handle.
 * @param filter    Acceptance filter (may be NULL = accept all).
 * @param cb        Receive callback.
 * @param user_data Opaque argument forwarded to @p cb.
 *
 * @return Filter id (>= 0) on success, negative errno on failure.
 *         The returned id is passed to @ref hal_can_remove_filter.
 */
int hal_can_add_rx_filter(hal_can_bus_t bus, const hal_can_filter_t *filter,
			  hal_can_rx_cb_t cb, void *user_data);

/**
 * @brief Remove a previously registered RX filter.
 *
 * @param bus       CAN bus handle.
 * @param filter_id Filter id returned by @ref hal_can_add_rx_filter.
 *
 * @return 0 on success, negative errno on failure (e.g. -EINVAL if the
 *         filter id is unknown).
 */
int hal_can_remove_filter(hal_can_bus_t bus, int filter_id);

/**
 * @brief Read the current controller error state.
 *
 * @param bus   CAN bus handle.
 * @param state Out parameter receiving @ref hal_can_state_t.
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_can_get_state(hal_can_bus_t bus, hal_can_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */