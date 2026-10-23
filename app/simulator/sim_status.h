/** @file sim_status.h @brief Value-only protocol observations; never parse logs for UI state. */
#ifndef SIM_STATUS_H
#define SIM_STATUS_H
#include "config/sim_config.h"
#define SIM_STAGE_HISTORY 64u
typedef enum
{
    SIM_STAGE_HANDSHAKE,
    SIM_STAGE_IDENTIFY,
    SIM_STAGE_CONFIG,
    SIM_STAGE_READY,
    SIM_STAGE_CHARGING,
    SIM_STAGE_STOPPING,
    SIM_STAGE_STATISTICS,
    SIM_STAGE_RECOVERY,
    SIM_STAGE_DONE,
    SIM_STAGE_IDLE
} sim_stage_t;
typedef struct
{
    sim_stage_t stage;
    uint32_t entered_ms;
    char reason[64];
} sim_transition_t;
typedef struct
{
    bool active, failed, paused, fault_enabled;
    unsigned session_id, revision;
    int result;
    sim_stage_t stage, error_stage;
    uint32_t elapsed_ms, stage_elapsed_ms, waiting_messages, conditions;
    uint32_t fault_hits, history_count;
    char error[64];
    gbt27930_message_t error_message;
    sim_transition_t history[SIM_STAGE_HISTORY];
    gbt27930_values_t local, peer;
    uint32_t peer_valid_messages;
} sim_status_t;
const char *sim_stage_name(sim_stage_t stage);
/* conditions: 1=self-test, 2=precharge, 4=local-ready, 8=peer-ready,
 * 16=local-ready-sent, 32=two BCP updates, 64=auxiliary-off, 128=recovery wait. */
#endif
