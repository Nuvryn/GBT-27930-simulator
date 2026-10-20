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
    uint32_t sent_ms[GB27930_MESSAGE_COUNT], received_ms[GB27930_MESSAGE_COUNT];
    bool has_sent[GB27930_MESSAGE_COUNT], has_received[GB27930_MESSAGE_COUNT];
    bool event_fired[SIM_CONFIG_EVENT_CAPACITY];
    bool recognized, peer_ready, own_ready, failed, paused, blocked, reset_pending, unplugged;
    bool selftest_logged, precharge_logged, completed_stats;
    unsigned attempts;
    unsigned next_message;
    int io_error;
};
static int sim_engine_dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len);
static void sim_engine_deliver(sim_engine_t *engine, gb27930_message_t message, const uint8_t *data, size_t len);

/** State changes restart periodic schedules, never physical hardware. */
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
        engine->parameter_start_ms = engine->now_ms;
    }
    memset(engine->has_sent, 0, sizeof(engine->has_sent));
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
        sim_log_event("ACTION", "模拟电流降至安全值、继电器断开，不进行真实采集");
    }
    if (stage == SIM_STAGE_DONE)
    {
        sim_log_event("ACTION", "模拟辅助电源断开、电子锁解锁");
    }
}
/** New protocol attempt invalidates stale receive evidence and delayed transmissions. */
static void sim_engine_restart(sim_engine_t *engine, sim_stage_t stage)
{
    if (engine->stage == SIM_STAGE_RECOVERY)
    {
        engine->attempts++;
        sim_log_event("RETRY", "attempt=%u", engine->attempts);
    }
    memset(engine->has_received, 0, sizeof(engine->has_received));
    engine->recognized = false;
    engine->peer_ready = false;
    engine->own_ready = false;
    engine->paused = false;
    engine->reset_pending = true;
    engine->selftest_logged = false;
    engine->precharge_logged = false;
    sim_fault_cancel(engine->fault);
    sim_engine_stage(engine, stage, "restart");
}
/** Distinguish timeout message bits from transport/parameter errors in evidence. */
static void sim_engine_failure(sim_engine_t *engine, const char *reason, gb27930_message_t message)
{
    if (engine->stage == SIM_STAGE_DONE || engine->stage == SIM_STAGE_RECOVERY)
    {
        return;
    }
    engine->failed = true;
    engine->reset_pending = true;
    sim_log_event("ERROR", "reason=%s message=%s", reason, gb27930_descriptor(message) != NULL ? gb27930_descriptor(message)->name : "transport");
    const char *keys_charger[GB27930_MESSAGE_COUNT] = {[GB27930_BRM] = "cem_brm", [GB27930_BCP] = "cem_bcp", [GB27930_BRO] = "cem_bro", [GB27930_BCS] = "cem_bcs", [GB27930_BCL] = "cem_bcl", [GB27930_BST] = "cem_bst", [GB27930_BSD] = "cem_bsd", [GB27930_BSM] = "cem_bsm"};
    const char *keys_bms[GB27930_MESSAGE_COUNT] = {[GB27930_CRM] = "bem_crm_initial", [GB27930_CML] = "bem_cml", [GB27930_CRO] = "bem_cro", [GB27930_CCS] = "bem_ccs", [GB27930_CST] = "bem_cst", [GB27930_CSD] = "bem_csd"};
    if ((unsigned) message < GB27930_MESSAGE_COUNT)
    {
        const char *key = engine->config.charger ? keys_charger[message] : keys_bms[message];
        if (message == GB27930_CRM && engine->stage == SIM_STAGE_IDENTIFY && !engine->config.charger)
        {
            key = "bem_crm_confirm";
        }
        if (key != NULL)
        {
            (void) gb27930_set(&engine->config.values, key, 1);
        }
    }
    if (engine->config.version == 2023u && message == GB27930_BSM)
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
        sim_engine_failure(engine, "tp_error", gb27930_find_pgn(pgn));
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
    gb27930_message_t message = gb27930_find_pgn(pgn);
    if (message != GB27930_MESSAGE_COUNT)
    {
        int rc = sim_engine_inject(engine, true, gb27930_descriptor(message)->name, gb27930_can_id(message, engine->config.peer, engine->config.source), data, len);
        if (rc != 0)
        {
            engine->io_error = rc;
        }
    }
}
/** Shared payload/frame dispatch after injection. No recursive injection of duplicates. */
static int sim_engine_dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len)
{
    sim_engine_t *engine = user;
    bool transport = strncmp(message, "TP_", 3u) == 0;
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
        gb27930_message_t received = gb27930_find_pgn((id >> 8u) & 0x3FF00u);
        if (received != GB27930_MESSAGE_COUNT)
        {
            sim_engine_deliver(engine, received, data, len);
        }
        return 0;
    }
    if (!transport && len > 8u)
    {
        uint32_t pgn = (id >> 8u) & 0x3FF00u;
        return j1939_tp_send(engine->tp, pgn, data, len, engine->now_ms);
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
        engine->now_ms = now_ms;
        sim_engine_stage(engine, SIM_STAGE_STOPPING, "operator_stop");
    }
}
/** Validate parameters before transitioning or refreshing any timeout. */
static void sim_engine_payload(sim_engine_t *engine, gb27930_message_t message, const gb27930_values_t *values)
{
    if (message == GB27930_BST || message == GB27930_CST)
    {
        const char *key = engine->config.charger ? "cst_reason" : "bst_reason";
        (void) gb27930_set(&engine->config.values, key, 64);
        if (engine->stage < SIM_STAGE_STOPPING)
        {
            sim_engine_stage(engine, SIM_STAGE_STOPPING, "peer_stop");
        }
        return;
    }
    if (message == GB27930_BEM || message == GB27930_CEM)
    {
        sim_engine_failure(engine, "peer_error", message);
        return;
    }
    if (engine->stage == SIM_STAGE_HANDSHAKE)
    {
        if (engine->config.charger && message == GB27930_BHM)
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
        if (!engine->config.charger && message == GB27930_CRM && gb27930_get(values, "crm_result") == 0)
        {
            sim_engine_stage(engine, SIM_STAGE_IDENTIFY, "crm_initial");
        }
    }
    if (engine->stage == SIM_STAGE_RECOVERY && engine->config.reconnect_allowed && !engine->blocked && engine->attempts < engine->config.retries)
    {
        if (!engine->config.charger && message == GB27930_CRM && gb27930_get(values, "crm_result") == 0)
        {
            sim_engine_restart(engine, SIM_STAGE_IDENTIFY);
        }
        else if (engine->config.charger && message == GB27930_BRM)
        {
            sim_engine_restart(engine, SIM_STAGE_IDENTIFY);
        }
    }
    if (engine->stage == SIM_STAGE_IDENTIFY)
    {
        if (engine->config.charger && message == GB27930_BRM)
        {
            if (!engine->recognized)
            {
                engine->confirm_start_ms = engine->now_ms;
            }
            engine->recognized = true;
            engine->has_sent[GB27930_CRM] = false;
        }
        if (!engine->config.charger && message == GB27930_CRM && gb27930_get(values, "crm_result") == 170)
        {
            sim_engine_stage(engine, SIM_STAGE_CONFIG, "identified");
        }
        if (engine->config.charger && message == GB27930_BCP && engine->recognized)
        {
            if (!engine->config.parameters_match || gb27930_get(values, "bcp_voltage_mv") < gb27930_get(&engine->config.values, "cml_min_voltage_mv") ||
                gb27930_get(values, "bcp_voltage_mv") > gb27930_get(&engine->config.values, "cml_max_voltage_mv"))
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
        if (!engine->config.charger && message == GB27930_CML)
        {
            int64_t voltage = gb27930_get(&engine->config.values, "bcp_voltage_mv");
            if (engine->config.parameters_match && voltage >= gb27930_get(values, "cml_min_voltage_mv") && voltage <= gb27930_get(values, "cml_max_voltage_mv") &&
                gb27930_get(values, "cml_min_voltage_mv") <= gb27930_get(values, "cml_max_voltage_mv") && gb27930_get(values, "cml_min_current_ma") <= gb27930_get(values, "cml_max_current_ma"))
            {
                sim_engine_stage(engine, SIM_STAGE_READY, "charger_parameters_accepted");
            }
            else
            {
                sim_engine_failure(engine, "charger_parameters_mismatch", message);
            }
        }
        if (engine->config.charger && message == GB27930_BRO)
        {
            sim_engine_stage(engine, SIM_STAGE_READY, "bro_received");
        }
    }
    if (engine->stage == SIM_STAGE_READY)
    {
        if (engine->config.charger && message == GB27930_BRO && gb27930_get(values, "bro_ready") == 170)
        {
            engine->peer_ready = true;
        }
        if (!engine->config.charger && message == GB27930_CRO && gb27930_get(values, "cro_ready") == 170)
        {
            engine->peer_ready = true;
            sim_engine_stage(engine, SIM_STAGE_CHARGING, "both_ready");
        }
        if (engine->config.charger && (message == GB27930_BCL || message == GB27930_BCS) && engine->own_ready && engine->has_received[GB27930_BCL] && engine->has_received[GB27930_BCS])
        {
            sim_engine_stage(engine, SIM_STAGE_CHARGING, "bcl_bcs_received");
        }
    }
    if (engine->stage == SIM_STAGE_CHARGING)
    {
        if (engine->config.charger && message == GB27930_BCL)
        {
            int64_t voltage = gb27930_get(values, "bcl_voltage_mv"), current = gb27930_get(values, "bcl_current_ma");
            if (engine->config.version == 2023u && voltage < gb27930_get(&engine->config.values, "cml_min_voltage_mv"))
            {
                sim_engine_stop(engine, engine->now_ms);
                return;
            }
            int64_t max_voltage = gb27930_get(&engine->config.values, "cml_max_voltage_mv"), max_current = gb27930_get(&engine->config.values, "cml_max_current_ma");
            int64_t min_current = gb27930_get(&engine->config.values, "cml_min_current_ma");
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
            (void) gb27930_set(&engine->config.values, "ccs_voltage_mv", voltage);
            (void) gb27930_set(&engine->config.values, "ccs_current_ma", engine->paused ? 0 : current);
        }
        if (engine->config.charger && message == GB27930_BSM)
        {
            const char *fault_keys[] = {"bsm_cell_voltage_state", "bsm_soc_state", "bsm_current_state", "bsm_temperature_state", "bsm_insulation_state", "bsm_connector_state"};
            for (size_t idx = 0u; idx < 6u; idx++)
            {
                if (gb27930_get(values, fault_keys[idx]) != 0)
                {
                    sim_engine_stop(engine, engine->now_ms);
                    return;
                }
            }
            bool paused = gb27930_get(values, "bsm_allow") == 0;
            if (paused != engine->paused)
            {
                engine->paused = paused;
                engine->pause_start_ms = engine->now_ms;
                sim_log_event("ACTION", "模拟充电%s", paused ? "暂停" : "恢复");
            }
            (void) gb27930_set(&engine->config.values, "ccs_allow", paused ? 0 : 1);
        }
    }
}
/** Reject out-of-stage traffic before it can refresh an application's receive timer. */
static bool sim_engine_expected(const sim_engine_t *engine, gb27930_message_t message)
{
    if (message == GB27930_BST || message == GB27930_CST || message == GB27930_BEM || message == GB27930_CEM)
    {
        return true;
    }
    bool charger = engine->config.charger;
    switch (engine->stage)
    {
        case SIM_STAGE_HANDSHAKE:
            return charger ? message == GB27930_BHM : message == GB27930_CHM || message == GB27930_CRM;
        case SIM_STAGE_IDENTIFY:
            return charger ? message == GB27930_BRM || (message == GB27930_BCP && engine->recognized) : message == GB27930_CRM;
        case SIM_STAGE_CONFIG:
            return charger ? message == GB27930_BCP || message == GB27930_BRO : message == GB27930_CML || message == GB27930_CTS;
        case SIM_STAGE_READY:
            return charger ? message == GB27930_BRO || (engine->own_ready && (message == GB27930_BCL || message == GB27930_BCS)) : message == GB27930_CRO || message == GB27930_CML || message == GB27930_CTS;
        case SIM_STAGE_CHARGING:
            return charger ? message == GB27930_BCL || message == GB27930_BCS || message == GB27930_BSM || message == GB27930_BMV || message == GB27930_BMT || message == GB27930_BSP : message == GB27930_CCS || message == GB27930_CML;
        case SIM_STAGE_STOPPING:
        case SIM_STAGE_STATISTICS:
            return charger ? message == GB27930_BSD : message == GB27930_CSD;
        case SIM_STAGE_RECOVERY:
            return charger ? message == GB27930_BRM : message == GB27930_CRM;
        default:
            return false;
    }
}
static void sim_engine_deliver(sim_engine_t *engine, gb27930_message_t message, const uint8_t *data, size_t len)
{
    const gb27930_descriptor_t *desc = gb27930_descriptor(message);
    if (desc == NULL || desc->charger == engine->config.charger || engine->unplugged || engine->stage == SIM_STAGE_DONE)
    {
        return;
    }
    if (!sim_engine_expected(engine, message))
    {
        sim_log_event("REJECT", "message=%s reason=stage", desc->name);
        return;
    }
    gb27930_values_t decoded;
    int rc = gb27930_decode(message, engine->config.version, data, len, &decoded);
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
        gb27930_message_t message = gb27930_find_pgn(pgn);
        if (message != GB27930_MESSAGE_COUNT)
        {
            name = gb27930_descriptor(message)->name;
        }
    }
    return name == NULL ? 0 : sim_engine_inject(engine, true, name, frame->id, frame->data, frame->len);
}
/** Stage-specific message selection; optional data never changes mandatory timing rules. */
static bool sim_engine_active(const sim_engine_t *engine, gb27930_message_t message)
{
    bool charger = engine->config.charger;
    switch (engine->stage)
    {
        case SIM_STAGE_HANDSHAKE:
            return charger ? message == GB27930_CHM : message == GB27930_BHM && engine->has_received[GB27930_CHM];
        case SIM_STAGE_IDENTIFY:
            return charger ? message == GB27930_CRM : message == GB27930_BRM;
        case SIM_STAGE_CONFIG:
            return charger ? (message == GB27930_CML || message == GB27930_CTS) : message == GB27930_BCP;
        case SIM_STAGE_READY:
            return charger ? ((message == GB27930_CML || message == GB27930_CTS) && !engine->peer_ready) || (message == GB27930_CRO && engine->peer_ready) : message == GB27930_BRO;
        case SIM_STAGE_CHARGING:
            return charger ? message == GB27930_CCS : (message == GB27930_BCL || message == GB27930_BCS || ((message == GB27930_BSM || message == GB27930_BMV || message == GB27930_BMT || message == GB27930_BSP) && engine->has_received[GB27930_CCS]));
        case SIM_STAGE_STOPPING:
            return charger ? message == GB27930_CST : message == GB27930_BST;
        case SIM_STAGE_STATISTICS:
            return charger ? message == GB27930_CSD && (engine->config.version == 2015u || engine->has_received[GB27930_BSD]) : message == GB27930_BSD;
        case SIM_STAGE_RECOVERY:
            return charger ? (message == GB27930_CEM || (message == GB27930_CRM && engine->config.reconnect_allowed && !engine->blocked && engine->attempts < engine->config.retries)) : message == GB27930_BEM;
        case SIM_STAGE_DONE:
            return engine->completed_stats && (charger ? message == GB27930_CSD : message == GB27930_BSD);
        default:
            return false;
    }
}
/** Fill runtime fields, then use validated application codec before injection. */
static int sim_engine_send_message(sim_engine_t *engine, gb27930_message_t message)
{
    if (message == GB27930_CRM)
    {
        (void) gb27930_set(&engine->config.values, "crm_result", engine->recognized ? 170 : 0);
    }
    if (message == GB27930_BRO)
    {
        (void) gb27930_set(&engine->config.values, "bro_ready", engine->own_ready ? 170 : 0);
    }
    if (message == GB27930_CRO)
    {
        (void) gb27930_set(&engine->config.values, "cro_ready", engine->own_ready ? 170 : 0);
    }
    uint8_t data[GB27930_PAYLOAD_CAPACITY];
    size_t len;
    int rc = gb27930_encode(message, engine->config.version, &engine->config.values, data, sizeof(data), &len);
    if (rc != 0)
    {
        return rc;
    }
    if (len > 8u && j1939_tp_busy(engine->tp))
    {
        return -EBUSY;
    }
    const gb27930_descriptor_t *desc = gb27930_descriptor(message);
    sim_log_event("MESSAGE", "direction=tx name=%s len=%u", desc->name, (unsigned) len);
    return sim_engine_inject(engine, false, desc->name, gb27930_can_id(message, engine->config.source, engine->config.peer), data, len);
}
/** Timeouts begin at a stage trigger until the first valid message, then last reception. */
static bool sim_engine_expired(sim_engine_t *engine, gb27930_message_t message, uint32_t first_ms)
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
            (void) sim_engine_expired(engine, charger ? GB27930_BHM : GB27930_CHM, start);
            break;
        case SIM_STAGE_IDENTIFY:
            if (!charger)
            {
                if (engine->now_ms - start >= engine->config.first_timeout_ms[GB27930_CRM])
                {
                    sim_engine_failure(engine, "message_timeout", GB27930_CRM);
                }
            }
            else
            {
                (void) sim_engine_expired(engine, engine->recognized ? GB27930_BCP : GB27930_BRM, engine->recognized ? engine->confirm_start_ms : start);
            }
            break;
        case SIM_STAGE_CONFIG:
            (void) sim_engine_expired(engine, charger ? GB27930_BRO : GB27930_CML, start);
            break;
        case SIM_STAGE_READY:
            uint32_t ready_origin = charger ? engine->parameter_start_ms : (engine->own_ready ? engine->own_ready_ms : start);
            if (engine->now_ms - ready_origin >= engine->config.ready_timeout_ms)
            {
                sim_engine_failure(engine, "ready_timeout", charger ? GB27930_BRO : GB27930_CRO);
                break;
            }
            if (charger)
            {
                if (sim_engine_expired(engine, GB27930_BRO, start))
                {
                    break;
                }
                if (engine->own_ready && (sim_engine_expired(engine, GB27930_BCL, engine->own_ready_ms) || sim_engine_expired(engine, GB27930_BCS, engine->own_ready_ms)))
                {
                    break;
                }
            }
            else if (engine->own_ready)
            {
                (void) sim_engine_expired(engine, GB27930_CRO, engine->own_ready_ms);
            }
            break;
        case SIM_STAGE_CHARGING:
            if (charger)
            {
                if (sim_engine_expired(engine, GB27930_BCL, start) || sim_engine_expired(engine, GB27930_BCS, start))
                {
                    break;
                }
                if (engine->config.version == 2015u || engine->has_received[GB27930_BSM])
                {
                    (void) sim_engine_expired(engine, GB27930_BSM, start);
                }
            }
            else
            {
                (void) sim_engine_expired(engine, GB27930_CCS, start);
            }
            break;
        case SIM_STAGE_STOPPING:
            (void) sim_engine_expired(engine, charger ? GB27930_BST : GB27930_CST, engine->stop_start_ms);
            break;
        case SIM_STAGE_STATISTICS:
            (void) sim_engine_expired(engine, charger ? GB27930_BSD : GB27930_CSD, engine->stop_start_ms);
            break;
        default:
            break;
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
            (void) gb27930_set(&engine->config.values, engine->config.charger ? "ccs_allow" : "bsm_allow", strcmp(event->action, "pause") == 0 ? 0 : 1);
        }
        else if (strcmp(event->action, "unplug") == 0)
        {
            engine->unplugged = true;
            engine->reset_pending = true;
            sim_log_event("ACTION", "模拟拔枪、断开输出");
        }
        else if (strcmp(event->action, "replug") == 0)
        {
            engine->unplugged = false;
            engine->blocked = false;
            engine->attempts = 0u;
            sim_engine_restart(engine, SIM_STAGE_HANDSHAKE);
        }
        else if (strcmp(event->action, "set_parameter") == 0)
        {
            (void) gb27930_set(&engine->config.values, event->parameter, event->value);
        }
    }
}
/** Stage-local completion/ready decisions are separate from incoming-frame parsing. */
static void sim_engine_progress(sim_engine_t *engine)
{
    if (engine->stage == SIM_STAGE_CHARGING && !engine->config.charger && gb27930_get(&engine->config.values, "bcs_soc_pct") >= 100)
    {
        (void) gb27930_set(&engine->config.values, "bst_reason", 1);
        sim_engine_stage(engine, SIM_STAGE_STOPPING, "soc_target");
    }
    if (engine->stage == SIM_STAGE_READY && !engine->own_ready && engine->config.ready && engine->now_ms - engine->stage_start_ms >= engine->config.ready_delay_ms &&
        (!engine->config.charger || engine->peer_ready))
    {
        if (!engine->config.precharge_pass)
        {
            sim_engine_failure(engine, "precharge_failed", GB27930_CRO);
            return;
        }
        engine->own_ready = true;
        engine->own_ready_ms = engine->now_ms;
        sim_log_event("ACTION", "模拟就绪及预充校验 result=pass，测量值来自配置");
        engine->has_sent[engine->config.charger ? GB27930_CRO : GB27930_BRO] = false;
    }
    if (engine->stage == SIM_STAGE_CHARGING && engine->config.stop_after_ms != 0u && engine->now_ms - engine->stage_start_ms >= engine->config.stop_after_ms)
    {
        sim_engine_stop(engine, engine->now_ms);
    }
    if (engine->stage == SIM_STAGE_CHARGING && engine->paused && engine->config.version == 2023u && engine->now_ms - engine->pause_start_ms >= 600000u)
    {
        sim_engine_stop(engine, engine->now_ms);
    }
    if (engine->stage == SIM_STAGE_STOPPING && engine->has_sent[engine->config.charger ? GB27930_CST : GB27930_BST] && engine->has_received[engine->config.charger ? GB27930_BST : GB27930_CST])
    {
        sim_engine_stage(engine, SIM_STAGE_STATISTICS, "stop_acknowledged");
    }
    if (engine->stage == SIM_STAGE_STATISTICS && engine->has_sent[engine->config.charger ? GB27930_CSD : GB27930_BSD] && engine->has_received[engine->config.charger ? GB27930_BSD : GB27930_CSD])
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
    for (unsigned offset = 0u; offset < GB27930_MESSAGE_COUNT; offset++)
    {
        unsigned idx = (first_message + offset) % GB27930_MESSAGE_COUNT;
        gb27930_message_t message = (gb27930_message_t) idx;
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
        if (gb27930_descriptor(message)->len > 8u || message == GB27930_BMV || message == GB27930_BMT)
        {
            engine->next_message = (idx + 1u) % GB27930_MESSAGE_COUNT;
        }
    }
    if (engine->stage == SIM_STAGE_DONE && now_ms - engine->stage_start_ms >= 1000u)
    {
        return engine->failed ? -EPROTO : 1;
    }
    return 0;
}
