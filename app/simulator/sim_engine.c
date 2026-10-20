/** @file sim_engine.c
 * @brief CAN-only GB/T 27930 simulator. Physical actions are evidence logs only.
 * @note Local protocol notes are the field/flow reference; unknown frames never refresh timers.
 */
#include "simulator/sim_engine.h"
#include "transport/j1939_tp.h"
#include "fault_injection/sim_fault.h"
#include "logging/sim_log.h"
#include "osal/osal.h"
#include <errno.h>
#include <string.h>
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
    SIM_STAGE_DONE
} sim_stage_t;
static const char *const s_sim_stage_names[] = {"handshake", "identify", "config", "ready", "charging", "stopping", "statistics", "recovery", "done"};
struct sim_engine_s
{
    sim_config_t config;
    sim_engine_send_cb_t send_cb;
    void *cb_user;
    j1939_tp_t *tp;
    sim_fault_t *fault;
    sim_stage_t stage;
    uint32_t now_ms, stage_start_ms, stop_start_ms, own_ready_ms, pause_start_ms;
    uint32_t confirm_start_ms, parameter_start_ms;
    uint32_t ready_start_ms;
    uint32_t sent_ms[GBT27930_MESSAGE_COUNT], received_ms[GBT27930_MESSAGE_COUNT];
    bool has_sent[GBT27930_MESSAGE_COUNT], has_received[GBT27930_MESSAGE_COUNT];
    bool event_fired[SIM_CONFIG_EVENT_CAPACITY];
    bool recognized, peer_ready, own_ready, failed, paused, blocked, reset_pending, unplugged;
    bool selftest_logged, precharge_logged, completed_stats;
    bool own_ready_sent, local_stop_requested, auxiliary_power_on, statistics_started, error_active;
    bool recovering_attempt;
    unsigned bcp_update_remaining;
    bool bcp_update_inflight, bro_initial_pending;
    unsigned bst_sent_count;
    bool cst_sent, bsd_sent, csd_sent;
    unsigned attempts;
    unsigned next_message;
    int io_error;
};
static int sim_engine_dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len);
static void sim_engine_deliver(sim_engine_t *engine, gbt27930_message_t message, const uint8_t *data, size_t len);
static bool sim_engine_active(const sim_engine_t *engine, gbt27930_message_t message);

/** State changes preserve continuing message schedules; physical actions are logs only. */
static void sim_engine_stage(sim_engine_t *engine, sim_stage_t stage, const char *reason)
{
    if (stage == SIM_STAGE_DONE)
    {
        engine->completed_stats = engine->stage == SIM_STAGE_STATISTICS;
    }
    sim_log_event("STATE", "from=%s to=%s reason=%s", s_sim_stage_names[engine->stage], s_sim_stage_names[stage], reason);
    engine->stage = stage;
    engine->stage_start_ms = engine->now_ms;
    if (stage == SIM_STAGE_CONFIG)
    {
        engine->recovering_attempt = false;
        engine->parameter_start_ms = engine->now_ms;
    }
    if (stage == SIM_STAGE_READY)
    {
        engine->ready_start_ms = engine->now_ms;
    }
    if (stage == SIM_STAGE_CHARGING)
    {
        sim_log_event("ACTION", "模拟主接触器闭合；开始电能传输，不操作硬件");
    }
    if (stage == SIM_STAGE_STOPPING)
    {
        engine->reset_pending = true;
        sim_fault_cancel(engine->fault);
        engine->stop_start_ms = engine->now_ms;
        sim_log_event("ACTION", "模拟停止电压电流输出，不操作硬件");
    }
    if (stage == SIM_STAGE_STATISTICS)
    {
        engine->statistics_started = true;
        sim_log_event("ACTION", "模拟电流降至安全值、继电器断开，不进行真实采集");
    }
    if (stage == SIM_STAGE_DONE && engine->config.version == 2023u)
    {
        engine->error_active = false;
    }
}
/** New protocol attempt invalidates stale receive evidence and delayed transmissions. */
static void sim_engine_restart(sim_engine_t *engine, sim_stage_t stage)
{
    gbt27930_message_t statistics = engine->config.charger ? GBT27930_CSD : GBT27930_BSD;
    gbt27930_message_t peer_statistics = engine->config.charger ? GBT27930_BSD : GBT27930_CSD;
    bool keep_statistics = engine->statistics_started && stage == SIM_STAGE_HANDSHAKE;
    bool statistics_sent = engine->has_sent[statistics];
    bool statistics_received = engine->has_received[peer_statistics];
    if (engine->stage == SIM_STAGE_RECOVERY)
    {
        engine->recovering_attempt = true;
        engine->attempts++;
        sim_log_event("RETRY", "attempt=%u", engine->attempts);
    }
    memset(engine->has_received, 0, sizeof(engine->has_received));
    memset(engine->has_sent, 0, sizeof(engine->has_sent));
    if (keep_statistics)
    {
        engine->has_sent[statistics] = statistics_sent;
        engine->has_received[peer_statistics] = statistics_received;
    }
    engine->recognized = false;
    engine->peer_ready = false;
    engine->own_ready = false;
    engine->paused = false;
    engine->own_ready_sent = false;
    engine->local_stop_requested = false;
    engine->statistics_started = keep_statistics;
    engine->completed_stats = false;
    engine->bst_sent_count = 0u;
    engine->cst_sent = false;
    engine->bsd_sent = false;
    engine->csd_sent = false;
    engine->bcp_update_remaining = 0u;
    engine->bcp_update_inflight = false;
    engine->bro_initial_pending = false;
    if (engine->config.version == 2023u)
    {
        engine->error_active = false;
    }
    engine->reset_pending = true;
    engine->selftest_logged = false;
    engine->precharge_logged = false;
    sim_fault_cancel(engine->fault);
    sim_engine_stage(engine, stage, "restart");
}
/** Distinguish timeout message bits from transport/parameter errors in evidence. */
static void sim_engine_failure(sim_engine_t *engine, const char *reason, gbt27930_message_t message)
{
    if (engine->stage == SIM_STAGE_DONE || engine->stage == SIM_STAGE_RECOVERY)
    {
        return;
    }
    engine->failed = true;
    engine->error_active = true;
    engine->reset_pending = true;
    sim_log_event("ERROR", "reason=%s message=%s", reason, gbt27930_descriptor(message) != NULL ? gbt27930_descriptor(message)->name : "transport");
    const char *keys_charger[GBT27930_MESSAGE_COUNT] = {[GBT27930_BRM] = "cem_brm", [GBT27930_BCP] = "cem_bcp", [GBT27930_BRO] = "cem_bro", [GBT27930_BCS] = "cem_bcs", [GBT27930_BCL] = "cem_bcl", [GBT27930_BST] = "cem_bst", [GBT27930_BSD] = "cem_bsd", [GBT27930_BSM] = "cem_bsm"};
    const char *keys_bms[GBT27930_MESSAGE_COUNT] = {[GBT27930_CRM] = "bem_crm_initial", [GBT27930_CML] = "bem_cml", [GBT27930_CRO] = "bem_cro", [GBT27930_CCS] = "bem_ccs", [GBT27930_CST] = "bem_cst", [GBT27930_CSD] = "bem_csd"};
    if ((unsigned) message < GBT27930_MESSAGE_COUNT)
    {
        const char *key = engine->config.charger ? keys_charger[message] : keys_bms[message];
        if (message == GBT27930_CRM && engine->stage == SIM_STAGE_IDENTIFY && !engine->config.charger)
        {
            key = "bem_crm_confirm";
        }
        if (key != NULL)
        {
            (void) gbt27930_set(&engine->config.values, key, 1);
        }
    }
    if (engine->config.version == 2023u && message == GBT27930_BSM)
    {
        engine->blocked = true;
        sim_log_event("LOCKOUT", "BSM 超时，等待模拟重新插拔枪事件");
    }
    sim_engine_stage(engine, SIM_STAGE_RECOVERY, reason);
    sim_log_event("ACTION", "模拟停止输出并断开接触器，进入通信故障处理");
}
/** Callback errors are transport evidence; process resource errors remain fatal. */
static void sim_engine_tp_event(void *user, bool transmit, uint32_t pgn, int result)
{
    sim_engine_t *engine = user;
    sim_log_event("TP", "direction=%s pgn=%06X result=%d", transmit ? "tx" : "rx", (unsigned) pgn, result);
    if (result != 0)
    {
        sim_engine_failure(engine, "tp_error", gbt27930_find_pgn(pgn));
    }
    else if (transmit && pgn == gbt27930_descriptor(GBT27930_BCP)->pgn && engine->bcp_update_inflight)
    {
        engine->bcp_update_inflight = false;
        if (engine->bcp_update_remaining != 0u)
        {
            engine->bcp_update_remaining--;
        }
    }
}
static int sim_engine_inject(sim_engine_t *engine, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len)
{
    return sim_fault_apply(engine->fault, receive, message, s_sim_stage_names[engine->stage], engine->now_ms - engine->stage_start_ms, engine->now_ms, id, data, len);
}
static int sim_engine_tp_send(void *user, const sim_frame_t *frame)
{
    sim_engine_t *engine = user;
    const char *name = ((frame->id >> 16u) & 255u) == 0xEBu ? "TP_DT" : "TP_CM";
    return sim_engine_inject(engine, false, name, frame->id, frame->data, frame->len);
}
static void sim_engine_tp_receive(void *user, uint32_t pgn, const uint8_t *data, size_t len)
{
    sim_engine_t *engine = user;
    gbt27930_message_t message = gbt27930_find_pgn(pgn);
    if (message != GBT27930_MESSAGE_COUNT)
    {
        int rc = sim_engine_inject(engine, true, gbt27930_descriptor(message)->name, gbt27930_can_id(message, engine->config.peer, engine->config.source), data, len);
        if (rc != 0)
        {
            engine->io_error = rc;
        }
    }
}
/** Wire evidence is updated after injection and successful CAN submission, never on a drop. */
static void sim_engine_wire_sent(sim_engine_t *engine, uint32_t id, const uint8_t *data, size_t len)
{
    if ((uint8_t) id != engine->config.source || (uint8_t) (id >> 8u) != engine->config.peer)
    {
        return;
    }
    gbt27930_message_t message = gbt27930_find_pgn((id >> 8u) & 0x3FF00u);
    const gbt27930_descriptor_t *desc = gbt27930_descriptor(message);
    if (desc == NULL || desc->charger != engine->config.charger || len != desc->len)
    {
        return;
    }
    if ((message == GBT27930_BRO || message == GBT27930_CRO) && data[0] == 170u)
    {
        if (!engine->own_ready_sent)
        {
            engine->own_ready_ms = engine->now_ms;
        }
        engine->own_ready_sent = true;
    }
    if (message == GBT27930_BRO && data[0] == 0u && engine->bro_initial_pending)
    {
        engine->bro_initial_pending = false;
        engine->ready_start_ms = engine->now_ms;
    }
    if (message == GBT27930_BST)
    {
        if (engine->bst_sent_count == 0u)
        {
            engine->stop_start_ms = engine->now_ms;
        }
        if (engine->bst_sent_count < 10u)
        {
            engine->bst_sent_count++;
        }
    }
    if (message == GBT27930_CST)
    {
        if (!engine->cst_sent)
        {
            engine->stop_start_ms = engine->now_ms;
        }
        engine->cst_sent = true;
    }
    if (message == GBT27930_BSD)
    {
        engine->bsd_sent = true;
    }
    if (message == GBT27930_CSD)
    {
        engine->csd_sent = true;
    }
    if (message == GBT27930_CRM)
    {
        engine->error_active = false;
        engine->statistics_started = false;
        engine->has_received[GBT27930_BSD] = false;
    }
}
/** Shared payload/frame dispatch after injection. No recursive injection of duplicates. */
static int sim_engine_dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len)
{
    sim_engine_t *engine = user;
    bool transport = strncmp(message, "TP_", 3u) == 0;
    if (!receive && !transport && !sim_engine_active(engine, gbt27930_find_message(message)))
    {
        /* A delayed/duplicated frame cannot outlive its documented stop condition. */
        return 0;
    }
    if (receive)
    {
        if ((uint8_t) id != engine->config.peer || (uint8_t) (id >> 8u) != engine->config.source)
        {
            return 0;
        }
        if (transport)
        {
            if (len > 8u)
            {
                return -EMSGSIZE;
            }
            sim_frame_t frame = {.id = id, .len = (uint8_t) len};
            memcpy(frame.data, data, len);
            int rc = j1939_tp_receive(engine->tp, &frame, engine->now_ms);
            if (rc != 0 && rc != -ENOENT)
            {
                sim_log_event("REJECT", "transport rc=%d", rc);
            }
            return 0;
        }
        gbt27930_message_t received = gbt27930_find_pgn((id >> 8u) & 0x3FF00u);
        if (received != GBT27930_MESSAGE_COUNT)
        {
            sim_engine_deliver(engine, received, data, len);
        }
        return 0;
    }
    if (!transport && len > 8u)
    {
        uint32_t pgn = (id >> 8u) & 0x3FF00u;
        int rc = j1939_tp_send(engine->tp, pgn, data, len, engine->now_ms);
        if (rc == 0 && pgn == gbt27930_descriptor(GBT27930_BCP)->pgn && engine->bcp_update_remaining != 0u)
        {
            engine->bcp_update_inflight = true;
        }
        return rc;
    }
    if (len > 8u)
    {
        return -EMSGSIZE;
    }
    sim_frame_t frame = {.id = id, .len = (uint8_t) len};
    memcpy(frame.data, data, len);
    int rc = engine->send_cb(engine->cb_user, &frame);
    if (rc != 0)
    {
        engine->io_error = rc;
    }
    else if (!transport)
    {
        sim_engine_wire_sent(engine, id, data, len);
    }
    return rc;
}
sim_engine_t *sim_engine_create(const sim_config_t *config, sim_engine_send_cb_t send_cb, void *cb_user)
{
    if (config == NULL || send_cb == NULL)
    {
        return NULL;
    }
    sim_engine_t *engine = osal_malloc(sizeof(*engine));
    if (engine == NULL)
    {
        return NULL;
    }
    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    engine->send_cb = send_cb;
    engine->cb_user = cb_user;
    engine->auxiliary_power_on = true;
    j1939_tp_options_t options = {.source = config->source, .peer = config->peer, .dt_interval_ms = config->dt_interval_ms, .rx_window = config->tp_rx_window, .t1_ms = config->tp_t1_ms, .t2_ms = config->tp_t2_ms, .t3_ms = config->tp_t3_ms, .t4_ms = config->tp_t4_ms, .send_cb = sim_engine_tp_send, .recv_cb = sim_engine_tp_receive, .event_cb = sim_engine_tp_event, .cb_user = engine};
    engine->tp = j1939_tp_create(&options);
    engine->fault = sim_fault_create(&engine->config, sim_engine_dispatch, engine);
    if (engine->tp == NULL || engine->fault == NULL)
    {
        sim_engine_destroy(engine);
        return NULL;
    }
    sim_log_event("ACTION", "模拟插枪、电子锁锁定、辅助电源接通；测量值来自配置");
    return engine;
}
void sim_engine_destroy(sim_engine_t *engine)
{
    if (engine != NULL)
    {
        j1939_tp_destroy(engine->tp);
        sim_fault_destroy(engine->fault);
        osal_free(engine);
    }
}
void sim_engine_stop(sim_engine_t *engine, uint32_t now_ms)
{
    if (engine != NULL && engine->stage < SIM_STAGE_STOPPING)
    {
        engine->local_stop_requested = true;
        engine->now_ms = now_ms;
        sim_engine_stage(engine, SIM_STAGE_STOPPING, "operator_stop");
    }
}
/** Validate parameters before transitioning or refreshing any timeout. */
static void sim_engine_payload(sim_engine_t *engine, gbt27930_message_t message, const gbt27930_values_t *values)
{
    if (engine->config.charger && engine->config.version == 2023u && message == GBT27930_BCP &&
        (engine->stage == SIM_STAGE_CONFIG || engine->stage == SIM_STAGE_READY))
    {
        if (!engine->config.parameters_match || gbt27930_get(values, "bcp_voltage_mv") < gbt27930_get(&engine->config.values, "cml_min_voltage_mv") ||
            gbt27930_get(values, "bcp_voltage_mv") > gbt27930_get(&engine->config.values, "cml_max_voltage_mv"))
        {
            sim_engine_failure(engine, "battery_parameters_mismatch", message);
        }
        else
        {
            sim_log_event("PARAMETER", "updated BCP accepted before BRO=AA");
        }
        return;
    }
    if (!engine->config.charger && message == GBT27930_CRM)
    {
        engine->error_active = false;
        if (engine->statistics_started)
        {
            sim_engine_restart(engine, SIM_STAGE_IDENTIFY);
        }
    }
    if (message == GBT27930_BST || message == GBT27930_CST)
    {
        if (!engine->config.charger && message == GBT27930_CST && engine->bst_sent_count == 0u)
        {
            engine->local_stop_requested = false;
        }
        const char *key = engine->config.charger ? "cst_reason" : "bst_reason";
        (void) gbt27930_set(&engine->config.values, key, 64);
        if (engine->stage < SIM_STAGE_STOPPING)
        {
            sim_engine_stage(engine, SIM_STAGE_STOPPING, "peer_stop");
        }
        return;
    }
    if (message == GBT27930_BEM || message == GBT27930_CEM)
    {
        if (engine->recovering_attempt && (engine->stage == SIM_STAGE_HANDSHAKE || engine->stage == SIM_STAGE_IDENTIFY))
        {
            /* 2015 keeps the previous error periodic until CRM. New-attempt timers still apply. */
            sim_log_event("RECOVERY", "ignore_previous_error message=%s", gbt27930_descriptor(message)->name);
            return;
        }
        sim_engine_failure(engine, "peer_error", message);
        return;
    }
    if (engine->stage == SIM_STAGE_HANDSHAKE)
    {
        if (engine->config.charger && message == GBT27930_BHM)
        {
            sim_log_event("ACTION", "模拟绝缘自检 result=%s", engine->config.self_test_pass ? "pass" : "fail");
            if (engine->config.self_test_pass)
            {
                sim_engine_stage(engine, SIM_STAGE_IDENTIFY, "handshake_complete");
            }
            else
            {
                sim_engine_failure(engine, "self_test_failed", message);
            }
        }
        if (!engine->config.charger && message == GBT27930_CRM && (engine->config.version == 2015u || gbt27930_get(values, "crm_result") == 0))
        {
            sim_engine_stage(engine, SIM_STAGE_IDENTIFY, "crm_initial");
        }
    }
    if (engine->stage == SIM_STAGE_RECOVERY && engine->config.reconnect_allowed && !engine->blocked && engine->attempts < engine->config.retries)
    {
        if (!engine->config.charger && message == GBT27930_CRM && (engine->config.version == 2015u || gbt27930_get(values, "crm_result") == 0))
        {
            sim_engine_restart(engine, SIM_STAGE_IDENTIFY);
        }
        else if (engine->config.charger && message == GBT27930_BRM)
        {
            sim_engine_restart(engine, SIM_STAGE_IDENTIFY);
        }
    }
    if (engine->stage == SIM_STAGE_IDENTIFY)
    {
        if (engine->config.charger && message == GBT27930_BRM)
        {
            if (!engine->recognized)
            {
                engine->confirm_start_ms = engine->now_ms;
            }
            engine->recognized = true;
            engine->has_sent[GBT27930_CRM] = false;
        }
        if (!engine->config.charger && message == GBT27930_CRM && gbt27930_get(values, "crm_result") == 170)
        {
            sim_engine_stage(engine, SIM_STAGE_CONFIG, "identified");
        }
        if (engine->config.charger && message == GBT27930_BCP && engine->recognized)
        {
            if (!engine->config.parameters_match || gbt27930_get(values, "bcp_voltage_mv") < gbt27930_get(&engine->config.values, "cml_min_voltage_mv") ||
                gbt27930_get(values, "bcp_voltage_mv") > gbt27930_get(&engine->config.values, "cml_max_voltage_mv"))
            {
                sim_engine_failure(engine, "battery_parameters_mismatch", message);
            }
            else
            {
                sim_engine_stage(engine, SIM_STAGE_CONFIG, "battery_parameters_accepted");
            }
        }
    }
    if (engine->stage == SIM_STAGE_CONFIG)
    {
        if (!engine->config.charger && message == GBT27930_CML)
        {
            int64_t voltage = gbt27930_get(&engine->config.values, "bcp_voltage_mv");
            if (engine->config.parameters_match && voltage >= gbt27930_get(values, "cml_min_voltage_mv") && voltage <= gbt27930_get(values, "cml_max_voltage_mv") &&
                gbt27930_get(values, "cml_min_voltage_mv") <= gbt27930_get(values, "cml_max_voltage_mv") && gbt27930_get(values, "cml_min_current_ma") <= gbt27930_get(values, "cml_max_current_ma"))
            {
                sim_engine_stage(engine, SIM_STAGE_READY, "charger_parameters_accepted");
            }
            else
            {
                sim_engine_failure(engine, "charger_parameters_mismatch", message);
            }
        }
        if (engine->config.charger && message == GBT27930_BRO)
        {
            sim_engine_stage(engine, SIM_STAGE_READY, "bro_received");
        }
    }
    if (engine->stage == SIM_STAGE_READY)
    {
        if (engine->config.charger && message == GBT27930_BRO && gbt27930_get(values, "bro_ready") == 170)
        {
            engine->peer_ready = true;
        }
        if (!engine->config.charger && message == GBT27930_CRO && gbt27930_get(values, "cro_ready") == 170)
        {
            engine->peer_ready = true;
        }
        if (engine->config.charger && (message == GBT27930_BCL || message == GBT27930_BCS) && engine->own_ready_sent && engine->has_received[GBT27930_BCL] && engine->has_received[GBT27930_BCS])
        {
            sim_engine_stage(engine, SIM_STAGE_CHARGING, "bcl_bcs_received");
        }
    }
    if (engine->stage == SIM_STAGE_CHARGING)
    {
        if (engine->config.charger && message == GBT27930_BCL)
        {
            int64_t voltage = gbt27930_get(values, "bcl_voltage_mv"), current = gbt27930_get(values, "bcl_current_ma");
            if (engine->config.version == 2023u && voltage < gbt27930_get(&engine->config.values, "cml_min_voltage_mv"))
            {
                sim_engine_stop(engine, engine->now_ms);
                return;
            }
            int64_t max_voltage = gbt27930_get(&engine->config.values, "cml_max_voltage_mv"), max_current = gbt27930_get(&engine->config.values, "cml_max_current_ma");
            int64_t min_current = gbt27930_get(&engine->config.values, "cml_min_current_ma");
            if (voltage > max_voltage)
            {
                voltage = max_voltage;
            }
            if (current > max_current)
            {
                current = max_current;
            }
            if (current < min_current)
            {
                current = min_current;
            }
            (void) gbt27930_set(&engine->config.values, "ccs_voltage_mv", voltage);
            (void) gbt27930_set(&engine->config.values, "ccs_current_ma", engine->paused ? 0 : current);
        }
        if (engine->config.charger && message == GBT27930_BSM)
        {
            const char *fault_keys[] = {"bsm_cell_voltage_state", "bsm_soc_state", "bsm_current_state", "bsm_temperature_state", "bsm_insulation_state", "bsm_connector_state"};
            for (size_t idx = 0u; idx < 6u; idx++)
            {
                if (gbt27930_get(values, fault_keys[idx]) != 0)
                {
                    sim_engine_stop(engine, engine->now_ms);
                    return;
                }
            }
            bool paused = gbt27930_get(values, "bsm_allow") == 0;
            if (paused != engine->paused)
            {
                engine->paused = paused;
                engine->pause_start_ms = engine->now_ms;
                sim_log_event("ACTION", "模拟充电%s", paused ? "暂停" : "恢复");
            }
            (void) gbt27930_set(&engine->config.values, "ccs_allow", paused ? 0 : 1);
        }
    }
}
/** Reject out-of-stage traffic before it can refresh an application's receive timer. */
static bool sim_engine_expected(const sim_engine_t *engine, gbt27930_message_t message)
{
    if (message == GBT27930_BST || message == GBT27930_CST || message == GBT27930_BEM || message == GBT27930_CEM)
    {
        return true;
    }
    bool charger = engine->config.charger;
    switch (engine->stage)
    {
        case SIM_STAGE_HANDSHAKE:
            return charger ? message == GBT27930_BHM : message == GBT27930_CHM || message == GBT27930_CRM;
        case SIM_STAGE_IDENTIFY:
            return charger ? message == GBT27930_BRM || (message == GBT27930_BCP && engine->recognized) : message == GBT27930_CRM;
        case SIM_STAGE_CONFIG:
            return charger ? message == GBT27930_BCP || message == GBT27930_BRO : message == GBT27930_CML || message == GBT27930_CTS;
        case SIM_STAGE_READY:
            return charger ? message == GBT27930_BRO || (message == GBT27930_BCP && engine->config.version == 2023u && !engine->peer_ready) || (engine->own_ready_sent && (message == GBT27930_BCL || message == GBT27930_BCS)) : message == GBT27930_CRO || message == GBT27930_CML || message == GBT27930_CTS;
        case SIM_STAGE_CHARGING:
            return charger ? message == GBT27930_BCL || message == GBT27930_BCS || message == GBT27930_BSM || message == GBT27930_BMV || message == GBT27930_BMT || message == GBT27930_BSP : message == GBT27930_CCS || message == GBT27930_CML;
        case SIM_STAGE_STOPPING:
            return charger ? message == GBT27930_BSD && engine->cst_sent && engine->has_received[GBT27930_BST] : message == GBT27930_CSD && engine->bsd_sent;
        case SIM_STAGE_STATISTICS:
            return charger ? message == GBT27930_BSD : message == GBT27930_CSD;
        case SIM_STAGE_RECOVERY:
            return charger ? message == GBT27930_BRM : message == GBT27930_CRM || (engine->statistics_started && message == GBT27930_CSD);
        case SIM_STAGE_DONE:
            return engine->auxiliary_power_on && (charger ? message == GBT27930_BSD : message == GBT27930_CRM || message == GBT27930_CSD);
        default:
            return false;
    }
}
static void sim_engine_deliver(sim_engine_t *engine, gbt27930_message_t message, const uint8_t *data, size_t len)
{
    const gbt27930_descriptor_t *desc = gbt27930_descriptor(message);
    if (desc == NULL || desc->charger == engine->config.charger || engine->unplugged || !engine->auxiliary_power_on)
    {
        return;
    }
    if (!sim_engine_expected(engine, message))
    {
        sim_log_event("REJECT", "message=%s reason=stage", desc->name);
        return;
    }
    gbt27930_values_t decoded;
    int rc = gbt27930_decode(message, engine->config.version, data, len, &decoded);
    if (rc != 0)
    {
        sim_log_event("REJECT", "message=%s reason=decode rc=%d", desc->name, rc);
        return;
    }
    engine->received_ms[message] = engine->now_ms;
    engine->has_received[message] = true;
    sim_log_event("MESSAGE", "direction=rx name=%s len=%u", desc->name, (unsigned) len);
    sim_engine_payload(engine, message, &decoded);
}
int sim_engine_receive(sim_engine_t *engine, const sim_frame_t *frame, uint32_t now_ms)
{
    if (engine == NULL || frame == NULL || frame->len > 8u || frame->id > 0x1FFFFFFFu)
    {
        return -EINVAL;
    }
    engine->now_ms = now_ms;
    if (engine->unplugged)
    {
        return 0;
    }
    if ((uint8_t) frame->id != engine->config.peer || (uint8_t) (frame->id >> 8u) != engine->config.source)
    {
        return 0;
    }
    uint32_t pgn = (frame->id >> 8u) & 0x3FF00u;
    const char *name = NULL;
    if (pgn == 0xEC00u)
    {
        name = "TP_CM";
    }
    else if (pgn == 0xEB00u)
    {
        name = "TP_DT";
    }
    else
    {
        gbt27930_message_t message = gbt27930_find_pgn(pgn);
        if (message != GBT27930_MESSAGE_COUNT)
        {
            name = gbt27930_descriptor(message)->name;
        }
    }
    return name == NULL ? 0 : sim_engine_inject(engine, true, name, frame->id, frame->data, frame->len);
}
/** Stage-specific message selection; optional data never changes mandatory timing rules. */
static bool sim_engine_active(const sim_engine_t *engine, gbt27930_message_t message)
{
    if (!engine->auxiliary_power_on || engine->unplugged)
    {
        return false;
    }
    bool charger = engine->config.charger;
    if (message == GBT27930_BEM || message == GBT27930_CEM)
    {
        return engine->error_active && (charger ? message == GBT27930_CEM : message == GBT27930_BEM);
    }
    if (message == GBT27930_BSD || message == GBT27930_CSD)
    {
        return engine->statistics_started && (charger ? message == GBT27930_CSD && engine->has_received[GBT27930_BSD] : message == GBT27930_BSD && (engine->config.version == 2015u || !engine->has_received[GBT27930_CSD]));
    }
    switch (engine->stage)
    {
        case SIM_STAGE_HANDSHAKE:
            return charger ? message == GBT27930_CHM : message == GBT27930_BHM && engine->has_received[GBT27930_CHM];
        case SIM_STAGE_IDENTIFY:
            return charger ? message == GBT27930_CRM : message == GBT27930_BRM;
        case SIM_STAGE_CONFIG:
            return charger ? (message == GBT27930_CML || message == GBT27930_CTS) : message == GBT27930_BCP;
        case SIM_STAGE_READY:
            if (!charger && engine->bcp_update_remaining != 0u)
            {
                return message == GBT27930_BCP;
            }
            return charger ? ((message == GBT27930_CML || message == GBT27930_CTS) && !engine->peer_ready) ||
                                 (message == GBT27930_CRO && engine->peer_ready && !(engine->has_received[GBT27930_BCL] && engine->has_received[GBT27930_BCS])) ||
                                 (message == GBT27930_CCS && engine->config.version == 2023u && engine->has_received[GBT27930_BCL])
                           : message == GBT27930_BRO && !(engine->own_ready_sent && engine->peer_ready);
        case SIM_STAGE_CHARGING:
            return charger ? message == GBT27930_CCS : (message == GBT27930_BCL || message == GBT27930_BCS || ((message == GBT27930_BSM || message == GBT27930_BMV || message == GBT27930_BMT || message == GBT27930_BSP) && engine->has_received[GBT27930_CCS]));
        case SIM_STAGE_STOPPING:
            return charger ? message == GBT27930_CST && !engine->has_received[GBT27930_BSD] : message == GBT27930_BST && (engine->local_stop_requested ? !engine->has_received[GBT27930_CST] : engine->bst_sent_count < (engine->config.version == 2023u ? 5u : 1u));
        case SIM_STAGE_STATISTICS:
        case SIM_STAGE_RECOVERY:
        case SIM_STAGE_DONE:
            return false;
        default:
            return false;
    }
}
/** Fill runtime fields, then use validated application codec before injection. */
static int sim_engine_send_message(sim_engine_t *engine, gbt27930_message_t message)
{
    if (message == GBT27930_CRM)
    {
        (void) gbt27930_set(&engine->config.values, "crm_result", engine->recognized ? 170 : 0);
    }
    if (message == GBT27930_BRO)
    {
        (void) gbt27930_set(&engine->config.values, "bro_ready", engine->own_ready ? 170 : 0);
    }
    if (message == GBT27930_CRO)
    {
        (void) gbt27930_set(&engine->config.values, "cro_ready", engine->own_ready ? 170 : 0);
    }
    uint8_t data[GBT27930_PAYLOAD_CAPACITY];
    size_t len;
    int rc = gbt27930_encode(message, engine->config.version, &engine->config.values, data, sizeof(data), &len);
    if (rc != 0)
    {
        return rc;
    }
    if (len > 8u && j1939_tp_busy(engine->tp))
    {
        return -EBUSY;
    }
    const gbt27930_descriptor_t *desc = gbt27930_descriptor(message);
    sim_log_event("MESSAGE", "direction=tx name=%s len=%u", desc->name, (unsigned) len);
    return sim_engine_inject(engine, false, desc->name, gbt27930_can_id(message, engine->config.source, engine->config.peer), data, len);
}
/** Timeouts begin at a stage trigger until the first valid message, then last reception. */
static bool sim_engine_expired(sim_engine_t *engine, gbt27930_message_t message, uint32_t first_ms)
{
    uint32_t origin = engine->has_received[message] ? engine->received_ms[message] : first_ms;
    uint32_t timeout_ms = engine->has_received[message] ? engine->config.timeout_ms[message] : engine->config.first_timeout_ms[message];
    if (engine->now_ms - origin >= timeout_ms)
    {
        sim_engine_failure(engine, "message_timeout", message);
        return true;
    }
    return false;
}
static void sim_engine_timeouts(sim_engine_t *engine)
{
    bool charger = engine->config.charger;
    uint32_t start = engine->stage_start_ms;
    switch (engine->stage)
    {
        case SIM_STAGE_HANDSHAKE:
            if (!charger && engine->has_received[GBT27930_CHM])
            {
                (void) sim_engine_expired(engine, GBT27930_CRM, engine->received_ms[GBT27930_CHM]);
            }
            else
            {
                (void) sim_engine_expired(engine, charger ? GBT27930_BHM : GBT27930_CHM, start);
            }
            break;
        case SIM_STAGE_IDENTIFY:
            if (!charger)
            {
                if (engine->now_ms - start >= engine->config.first_timeout_ms[GBT27930_CRM])
                {
                    sim_engine_failure(engine, "message_timeout", GBT27930_CRM);
                }
            }
            else
            {
                (void) sim_engine_expired(engine, engine->recognized ? GBT27930_BCP : GBT27930_BRM, engine->recognized ? engine->confirm_start_ms : start);
            }
            break;
        case SIM_STAGE_CONFIG:
            (void) sim_engine_expired(engine, charger ? GBT27930_BRO : GBT27930_CML, start);
            break;
        case SIM_STAGE_READY:
            uint32_t ready_origin = charger ? engine->parameter_start_ms : (engine->own_ready_sent ? engine->own_ready_ms : start);
            if (engine->now_ms - ready_origin >= engine->config.ready_timeout_ms)
            {
                sim_engine_failure(engine, "ready_timeout", charger ? GBT27930_BRO : GBT27930_CRO);
                break;
            }
            if (charger)
            {
                if (!engine->peer_ready && sim_engine_expired(engine, GBT27930_BRO, start))
                {
                    break;
                }
                if (engine->own_ready_sent && (sim_engine_expired(engine, GBT27930_BCL, engine->own_ready_ms) || sim_engine_expired(engine, GBT27930_BCS, engine->own_ready_ms)))
                {
                    break;
                }
            }
            else if (engine->own_ready_sent)
            {
                (void) sim_engine_expired(engine, GBT27930_CRO, engine->own_ready_ms);
            }
            break;
        case SIM_STAGE_CHARGING:
            if (charger)
            {
                if (sim_engine_expired(engine, GBT27930_BCL, start) || sim_engine_expired(engine, GBT27930_BCS, start))
                {
                    break;
                }
                if (engine->config.version == 2015u || engine->has_received[GBT27930_BSM])
                {
                    (void) sim_engine_expired(engine, GBT27930_BSM, start);
                }
            }
            else
            {
                (void) sim_engine_expired(engine, GBT27930_CCS, start);
            }
            break;
        case SIM_STAGE_STOPPING:
            if (charger && engine->cst_sent)
            {
                (void) sim_engine_expired(engine, engine->has_received[GBT27930_BST] ? GBT27930_BSD : GBT27930_BST, engine->stop_start_ms);
            }
            else if (!charger && engine->bst_sent_count != 0u && !engine->has_received[GBT27930_CST])
            {
                (void) sim_engine_expired(engine, GBT27930_CST, engine->stop_start_ms);
            }
            break;
        case SIM_STAGE_STATISTICS:
            (void) sim_engine_expired(engine, charger ? GBT27930_BSD : GBT27930_CSD, engine->stop_start_ms);
            break;
        default:
            break;
    }
}
static void sim_engine_aux_power_off(sim_engine_t *engine, const char *reason)
{
    if (engine->auxiliary_power_on)
    {
        engine->auxiliary_power_on = false;
        engine->reset_pending = true;
        sim_fault_cancel(engine->fault);
        sim_log_event("ACTION", "模拟辅助电源断开、电子锁解锁 reason=%s", reason);
    }
}
/** Scene events use stage-relative time; '*' uses session elapsed time. */
static void sim_engine_events(sim_engine_t *engine)
{
    for (size_t idx = 0u; idx < engine->config.event_count; idx++)
    {
        const sim_event_t *event = &engine->config.events[idx];
        bool all = strcmp(event->stage, "*") == 0;
        if (engine->event_fired[idx] || (!all && strcmp(event->stage, s_sim_stage_names[engine->stage]) != 0) ||
            (all ? engine->now_ms : engine->now_ms - engine->stage_start_ms) < event->after_ms)
        {
            continue;
        }
        engine->event_fired[idx] = true;
        sim_log_event("EVENT", "index=%u action=%s", (unsigned) idx, event->action);
        if (strcmp(event->action, "stop") == 0)
        {
            sim_engine_stop(engine, engine->now_ms);
        }
        else if (strcmp(event->action, "pause") == 0 || strcmp(event->action, "resume") == 0)
        {
            (void) gbt27930_set(&engine->config.values, engine->config.charger ? "ccs_allow" : "bsm_allow", strcmp(event->action, "pause") == 0 ? 0 : 1);
        }
        else if (strcmp(event->action, "unplug") == 0)
        {
            sim_engine_aux_power_off(engine, "unplug");
            engine->unplugged = true;
            engine->reset_pending = true;
            sim_log_event("ACTION", "模拟拔枪、断开输出");
        }
        else if (strcmp(event->action, "replug") == 0)
        {
            engine->auxiliary_power_on = true;
            engine->unplugged = false;
            engine->blocked = false;
            engine->attempts = 0u;
            sim_engine_restart(engine, SIM_STAGE_HANDSHAKE);
        }
        else if (strcmp(event->action, "set_parameter") == 0)
        {
            (void) gbt27930_set(&engine->config.values, event->parameter, event->value);
            if (!engine->config.charger && engine->config.version == 2023u && engine->stage == SIM_STAGE_READY && !engine->own_ready_sent)
            {
                for (size_t field = 0u; gbt27930_field(field) != NULL; field++)
                {
                    const gbt27930_field_t *desc = gbt27930_field(field);
                    if (desc->message == GBT27930_BCP && strcmp(desc->key, event->parameter) == 0)
                    {
                        engine->bcp_update_remaining = 2u;
                        engine->bro_initial_pending = true;
                        engine->own_ready = false;
                        engine->has_sent[GBT27930_BCP] = false;
                        sim_fault_cancel(engine->fault);
                        sim_log_event("PARAMETER", "BCP update requires two completed transmissions before BRO=00");
                        break;
                    }
                }
            }
        }
        else if (strcmp(event->action, "aux_power_off") == 0)
        {
            sim_engine_aux_power_off(engine, "scenario");
        }
    }
}
/** Stage-local completion/ready decisions are separate from incoming-frame parsing. */
static void sim_engine_progress(sim_engine_t *engine)
{
    if (engine->stage == SIM_STAGE_CHARGING && !engine->config.charger && gbt27930_get(&engine->config.values, "bcs_soc_pct") >= 100)
    {
        engine->local_stop_requested = true;
        (void) gbt27930_set(&engine->config.values, "bst_reason", 1);
        sim_engine_stage(engine, SIM_STAGE_STOPPING, "soc_target");
    }
    if (engine->stage == SIM_STAGE_READY && !engine->own_ready && engine->config.ready && engine->bcp_update_remaining == 0u && !engine->bro_initial_pending && engine->now_ms - engine->ready_start_ms >= engine->config.ready_delay_ms &&
        (!engine->config.charger || engine->peer_ready))
    {
        if (!engine->config.precharge_pass)
        {
            sim_engine_failure(engine, "precharge_failed", GBT27930_CRO);
            return;
        }
        engine->own_ready = true;
        engine->own_ready_ms = engine->now_ms;
        sim_log_event("ACTION", "模拟就绪及预充校验 result=pass，测量值来自配置");
        engine->has_sent[engine->config.charger ? GBT27930_CRO : GBT27930_BRO] = false;
    }
    if (engine->stage == SIM_STAGE_READY && !engine->config.charger && engine->own_ready && engine->own_ready_sent && engine->peer_ready)
    {
        sim_engine_stage(engine, SIM_STAGE_CHARGING, "both_ready");
    }
    if (engine->stage == SIM_STAGE_CHARGING && engine->config.stop_after_ms != 0u && engine->now_ms - engine->stage_start_ms >= engine->config.stop_after_ms)
    {
        sim_engine_stop(engine, engine->now_ms);
    }
    if (engine->stage == SIM_STAGE_CHARGING && engine->paused && engine->config.version == 2023u && engine->now_ms - engine->pause_start_ms >= 600000u)
    {
        sim_engine_stop(engine, engine->now_ms);
    }
    bool stop_complete = engine->config.charger ? engine->cst_sent && engine->has_received[GBT27930_BST] && engine->has_received[GBT27930_BSD] : engine->has_received[GBT27930_CST] && (engine->local_stop_requested || engine->bst_sent_count >= (engine->config.version == 2023u ? 5u : 1u));
    if (engine->stage == SIM_STAGE_STOPPING && stop_complete)
    {
        sim_engine_stage(engine, SIM_STAGE_STATISTICS, "stop_acknowledged");
    }
    if (engine->stage == SIM_STAGE_STATISTICS && (engine->config.charger ? engine->csd_sent : engine->bsd_sent) && engine->has_received[engine->config.charger ? GBT27930_BSD : GBT27930_CSD])
    {
        sim_engine_stage(engine, SIM_STAGE_DONE, "statistics_complete");
    }
    if (engine->stage == SIM_STAGE_RECOVERY && engine->now_ms - engine->stage_start_ms >= engine->config.recovery_wait_ms)
    {
        if (engine->attempts >= engine->config.retries || engine->blocked || !engine->config.reconnect_allowed)
        {
            sim_engine_stage(engine, SIM_STAGE_DONE, "retries_exhausted");
        }
        else
        {
            sim_engine_restart(engine, SIM_STAGE_HANDSHAKE);
        }
    }
}
int sim_engine_tick(sim_engine_t *engine, uint32_t now_ms)
{
    if (engine == NULL)
    {
        return -EINVAL;
    }
    engine->now_ms = now_ms;
    if (engine->io_error != 0)
    {
        return engine->io_error;
    }
    if (now_ms >= engine->config.max_session_ms)
    {
        sim_log_event("ERROR", "reason=session_timeout");
        return -ETIMEDOUT;
    }
    sim_engine_events(engine);
    if (engine->reset_pending)
    {
        j1939_tp_reset(engine->tp);
        engine->reset_pending = false;
    }
    if (engine->unplugged)
    {
        return 0;
    }
    if (!engine->auxiliary_power_on)
    {
        return engine->completed_stats && !engine->failed ? 1 : -ENOLINK;
    }
    sim_engine_progress(engine);
    sim_engine_timeouts(engine);
    int rc = sim_fault_tick(engine->fault, now_ms);
    if (rc != 0)
    {
        return rc;
    }
    (void) j1939_tp_tick(engine->tp, now_ms);
    if (engine->io_error != 0)
    {
        return engine->io_error;
    }
    unsigned first_message = engine->next_message;
    for (unsigned offset = 0u; offset < GBT27930_MESSAGE_COUNT; offset++)
    {
        unsigned idx = (first_message + offset) % GBT27930_MESSAGE_COUNT;
        gbt27930_message_t message = (gbt27930_message_t) idx;
        if (!engine->config.enabled[idx] || !sim_engine_active(engine, message) || (engine->has_sent[idx] && now_ms - engine->sent_ms[idx] < engine->config.period_ms[idx]))
        {
            continue;
        }
        rc = sim_engine_send_message(engine, message);
        if (rc == -EBUSY)
        {
            continue;
        }
        if (rc != 0)
        {
            return rc;
        }
        engine->has_sent[idx] = true;
        engine->sent_ms[idx] = now_ms;
        if (gbt27930_descriptor(message)->len > 8u || message == GBT27930_BMV || message == GBT27930_BMT)
        {
            engine->next_message = (idx + 1u) % GBT27930_MESSAGE_COUNT;
        }
    }
    if (engine->stage == SIM_STAGE_DONE)
    {
        if (engine->completed_stats && !engine->config.charger && engine->config.version == 2023u)
        {
            return engine->failed ? -EPROTO : 1;
        }
        if (engine->config.auto_aux_power_off && now_ms - engine->stage_start_ms >= engine->config.aux_power_off_delay_ms)
        {
            sim_engine_aux_power_off(engine, "configured_delay");
            return engine->failed ? -EPROTO : 1;
        }
    }
    return 0;
}
