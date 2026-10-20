/** @file sim_engine.h @brief Role/version state machines driven by explicit monotonic time. */
#ifndef APP_SIMULATOR_SIM_ENGINE_H
#define APP_SIMULATOR_SIM_ENGINE_H
#include "config/sim_config.h"
#include "common/sim_frame.h"
typedef struct sim_engine_s sim_engine_t;
typedef int (*sim_engine_send_cb_t)(void *cb_user, const sim_frame_t *frame);
/** Copy config and allocate all runtime buffers. NULL on failure. */
sim_engine_t *sim_engine_create(const sim_config_t *config, sim_engine_send_cb_t send_cb, void *cb_user);
void sim_engine_destroy(sim_engine_t *engine);
/** Thread-context, non-reentrant. Foreign frames ignored; malformed frames are logged. */
int sim_engine_receive(sim_engine_t *engine, const sim_frame_t *frame, uint32_t now_ms);
/** Advance schedules/events/timeouts. 0 running, 1 normal completion, negative failure. */
int sim_engine_tick(sim_engine_t *engine, uint32_t now_ms);
/** Operator stop request; no hardware actions. */
void sim_engine_stop(sim_engine_t *engine, uint32_t now_ms);
#endif
