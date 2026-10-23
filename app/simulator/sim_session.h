/** @file sim_session.h @brief Reusable charging sessions, independent of terminal/UI input. */
#ifndef APP_SIMULATOR_SIM_SESSION_H
#define APP_SIMULATOR_SIM_SESSION_H
#include "simulator/sim_engine.h"

typedef struct sim_session_s sim_session_t;
/** Copies validated config. Caller owns the instance; NULL on allocation failure. */
sim_session_t *sim_session_create(const sim_config_t *config, sim_engine_send_cb_t send_cb, void *cb_user);
void sim_session_destroy(sim_session_t *session);
/** now_ms is a wrapping monotonic protocol-millisecond clock shared by all calls.
 * Start only while idle: zero on success, -EBUSY if already running.
 */
int sim_session_start(sim_session_t *session, uint32_t now_ms);
/** Idle manual BMS starts only on a valid peer CHM; idle charger never transmits.
 * Following a failed handshake, a continuous old CHM stream does not retrigger.
 */
int sim_session_receive(sim_session_t *session, const sim_frame_t *frame, uint32_t now_ms);
/** 0=idle/running, 1=completed normally, negative=failed. Completion destroys the
 * engine and leaves the session idle. Transport resources belong to the caller.
 */
int sim_session_tick(sim_session_t *session, uint32_t now_ms);
void sim_session_stop(sim_session_t *session, uint32_t now_ms);
bool sim_session_active(const sim_session_t *session);
/** First underlying CAN send failure, zero if none; reset on each start. */
int sim_session_io_error(const sim_session_t *session);
/** Caller is the protocol thread. Only values may change; lifecycle resources stay fixed. */
int sim_session_values(sim_session_t *session, const gbt27930_values_t *values);
int sim_session_manual_fault(sim_session_t *session, const sim_fault_rule_t *rule);
void sim_session_status(sim_session_t *session, sim_status_t *status);
#endif
