/** @file sim_status.h @brief Value-only protocol observations; never parse logs for UI state. */
#ifndef SIM_STATUS_H
#define SIM_STATUS_H
#include "config/sim_config.h"
#include "fault_injection/sim_fault.h"
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

typedef enum
{
    SIM_TIMEOUT_NONE,
    SIM_TIMEOUT_FIRST,
    SIM_TIMEOUT_CONTINUOUS,
    SIM_TIMEOUT_STAGE,
    SIM_TIMEOUT_SESSION
} sim_timeout_kind_t;
typedef struct
{
    sim_stage_t stage;
    uint32_t entered_ms;
    char reason[64];
    bool closed, failed, has_last_rx, has_error_rx;
    uint32_t exited_ms, failed_ms, last_rx_ms, error_rx_ms;
    gbt27930_message_t last_rx_message, error_message;
    char exit_reason[64], error[64];
    sim_timeout_kind_t timeout_kind;
    uint32_t timeout_origin_ms, timeout_limit_ms, timeout_wait_ms;
    uint32_t manual_hits, config_hits;
    sim_fault_evidence_t last_fault;
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
