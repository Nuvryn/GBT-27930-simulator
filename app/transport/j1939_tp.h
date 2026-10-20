/** @file j1939_tp.h
 * @brief Bounded CMDT adapter around Open-SAE-J1939. One peer per instance.
 * @note Single-thread only. Calls across instances must never be nested.
 */
#ifndef APP_TRANSPORT_J1939_TP_H
#define APP_TRANSPORT_J1939_TP_H
#include "common/sim_frame.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define J1939_TP_CAPACITY 1785u
typedef struct j1939_tp_s j1939_tp_t;
typedef int (*j1939_tp_send_cb_t)(void *cb_user, const sim_frame_t *frame);
typedef void (*j1939_tp_recv_cb_t)(void *cb_user, uint32_t pgn, const uint8_t *data, size_t len);
typedef void (*j1939_tp_event_cb_t)(void *cb_user, bool transmit, uint32_t pgn, int result);
typedef struct
{
    uint8_t source;
    uint8_t peer;
    uint32_t dt_interval_ms;
    uint32_t t1_ms;
    uint32_t t2_ms;
    uint32_t t3_ms;
    uint32_t t4_ms;
    uint8_t rx_window;
    j1939_tp_send_cb_t send_cb;
    j1939_tp_recv_cb_t recv_cb;
    j1939_tp_event_cb_t event_cb;
    void *cb_user;
} j1939_tp_options_t;
/** Allocate at startup; NULL on invalid options/allocation failure. Buffers owned by adapter. */
j1939_tp_t *j1939_tp_create(const j1939_tp_options_t *options);
/** Release an idle instance; NULL allowed. No callback is invoked. */
void j1939_tp_destroy(j1939_tp_t *tp);
/** Copy 9..1785 bytes; 0 accepted, negative errno otherwise. No replacement of active TX. */
int j1939_tp_send(j1939_tp_t *tp, uint32_t pgn, const uint8_t *data, size_t len, uint32_t now_ms);
/** Feed a full frame after CAN routing. Foreign traffic ignored. Negative errno on rejection. */
int j1939_tp_receive(j1939_tp_t *tp, const sim_frame_t *frame, uint32_t now_ms);
/** Non-blocking timers and at most one DT send. Use monotonic milliseconds with wraparound. */
int j1939_tp_tick(j1939_tp_t *tp, uint32_t now_ms);
/** Query whether the single peer-directed transmit session is occupied. NULL returns false. */
bool j1939_tp_busy(const j1939_tp_t *tp);
/** Cancel sessions on protocol restart. No callback; NULL allowed. */
void j1939_tp_reset(j1939_tp_t *tp);
#endif
