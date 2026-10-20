/**
 * @file can_bus.h
 * Project supplementary layer: deterministic CAN adapter over the framework
 * asynchronous hal_can contract.
 *
 * WHY THIS SHAPE:
 *   The application needs DETERMINISTIC frame ordering: one
 *   send must complete before the next frame is queued. The framework Linux
 *   hal_can backend implements hal_can_send(bus, msg, timeout, done_cb, arg);
 *   the Linux backend reads:
 *
 *     - done_cb == NULL  -> synchronous fallback; the call blocks on an
 *       osal_sem until the deferred-flush path reports the outcome.  Order of
 *       TX frames is preserved: the caller is never ahead of the hardware.
 *     - done_cb != NULL  -> completion is signalled on the SocketCAN worker
 *       pthread; the call returns while the frame may not be flushed yet,
 *       which breaks the "one outstanding at a time" ordering.
 *
 *   Therefore can_bus_send() ALWAYS passes a NULL done_cb. Never pass one.
 *
 * DESIGN (D-A in .omo/plans/framework-port.md):
 *   A multi-role in-process topology maps onto ONE shared
 *   hal_can bus with ID-based routing, so can_bus_open() registers a single
 *   accept-all RX filter; routing happens in the application main loop, not in
 *   the RX callback.
 *
 * DEVIATION RECORD (vs plan todo #9, recorded in task-9 evidence):
 *   The plan named a can_bus_poll(); we implement can_bus_recv() instead so
 *   the caller's drain loop shape:
 *   "recv; if (_rc <= 0) break;") is preserved verbatim. can_bus_recv is
 *   non-blocking: 1 = frame returned, 0 = no frame, -1/-errno = error.
 *
 * Include style: "support/can_bus.h", "osal/osal.h", "hal/hal_can.h" are
 * repo-root-relative; compile with -I <repo> -I <repo>/src.
 */
#ifndef SUPPORT_CAN_BUS_H
#define SUPPORT_CAN_BUS_H

#include <stdbool.h>  /* bool (can_bus_open) */
#include <stdint.h>   /* uint32_t / uint8_t */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the CAN adapter.
 *
 * @param ifname      Interface name (e.g. "can0"); ignored when
 *                    @p use_virtual is true.
 * @param use_virtual If true, the resolved interface is the compile-time
 *                    macro CAN_BUS_VIRTUAL_IF (default "vcan0") instead of
 *                    @p ifname.
 *
 * Registers one accept-all hal_can RX filter whose callback only copies
 * frames into the 256-frame project-owned ring. Idempotent: repeated open
 * with the same resolved interface is a no-op (no second filter / socket).
 *
 * @return 0 on success, negative errno (-ENODEV/-EINVAL/-ENOMEM) on failure.
 */
int  can_bus_open(const char *ifname, bool use_virtual);

/**
 * @brief Send one CAN 2.0 extended (29-bit) frame.
 *
 * Builds a hal_can_msg_t with flags = HAL_CAN_EXT_ID and calls
 * hal_can_send(bus, &m, CAN_BUS_TX_TIMEOUT_MS, NULL, NULL) -- done_cb is
 * ALWAYS NULL to preserve deterministic send ordering (see file header).
 *
 * A non-zero hal_can_send return is counted (drop counter) and logged once
 * per open session; never silently ignored.
 *
 * @param id  29-bit extended frame id.
 * @param data Payload, 0..8 bytes. Must be non-NULL when dlc > 0.
 * @param dlc  Payload length, clamped to 0..8.
 *
 * @return 0 on success, negative errno on failure.
 */
int  can_bus_send(uint32_t id, const uint8_t *data, uint8_t dlc);

/**
 * @brief Non-blocking receive of one frame from the RX ring.
 *
 * @param id   Out: frame id.
 * @param data Out: 0..8 payload bytes.
 * @param dlc  Out: returned payload length.
 *
 * @retval 1  A frame was returned.
 * @retval 0  No frame pending (never blocks).
 * @retval <0 Negative errno on error (-EINVAL bad args, -ENODEV never
 *            opened / already closed).
 */
int  can_bus_recv(uint32_t *id, uint8_t *data, uint8_t *dlc);

/**
 * @brief Close the adapter: reset project state and detach the RX filter.
 *
 * The framework hal_can contract has NO destroy/close API (framework issue
 * I6); do not invent one. Project-owned state (ring, counters, filter id) is
 * reset here; the underlying SocketCAN socket is reclaimed by the backend
 * per its own lifecycle (worker idle self-exit / process exit). Calling
 * can_bus_close() when not open is a safe no-op.
 */
void can_bus_close(void);

/**
 * @brief RX-ring overflow drops + TX errors, for diagnostics.
 */
uint32_t can_bus_drop_count(void);

#ifdef __cplusplus
}
#endif

#endif /* SUPPORT_CAN_BUS_H */