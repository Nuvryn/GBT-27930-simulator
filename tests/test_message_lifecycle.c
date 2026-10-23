/** @file test_message_lifecycle.c @brief One real engine against a scripted peer; wire assertions follow appendix tables. */
#include "simulator/sim_engine.h"
#include "transport/j1939_tp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define LIFECYCLE_CAPACITY 8192u
#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            fprintf(stderr, "FAIL %d: %s (time=%u result=%d)\n", __LINE__, #expr, s_fixture.now_ms, s_fixture.result); \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
typedef struct
{
    sim_engine_t *engine;
    j1939_tp_t *peer;
    sim_config_t config;
    sim_frame_t frames[LIFECYCLE_CAPACITY], pending[LIFECYCLE_CAPACITY];
    uint32_t times[LIFECYCLE_CAPACITY];
    size_t count, drained, head, tail;
    uint32_t now_ms;
    int result, transport_error;
    unsigned updated_bcp_received;
    uint32_t last_updated_bcp_ms;
} lifecycle_fixture_t;
static lifecycle_fixture_t s_fixture;
static int fixture_output(void *user, const sim_frame_t *frame)
{
    (void) user;
    if (s_fixture.count >= LIFECYCLE_CAPACITY)
    {
        return -ENOSPC;
    }
    s_fixture.frames[s_fixture.count] = *frame;
    s_fixture.times[s_fixture.count++] = s_fixture.now_ms;
    return 0;
}
static int fixture_peer_output(void *user, const sim_frame_t *frame)
{
    (void) user;
    if (s_fixture.tail >= LIFECYCLE_CAPACITY)
    {
        return -ENOSPC;
    }
    s_fixture.pending[s_fixture.tail++] = *frame;
    return 0;
}
static void fixture_peer_payload(void *user, uint32_t pgn, const uint8_t *data, size_t len)
{
    (void) user;
    if (pgn == 0x0600u && len == 13u && data[6] == 0x30u && data[7] == 0x11u)
    {
        s_fixture.updated_bcp_received++;
        s_fixture.last_updated_bcp_ms = s_fixture.now_ms;
    }
}
static int fixture_pump(void)
{
    while (s_fixture.drained < s_fixture.count || s_fixture.head < s_fixture.tail)
    {
        while (s_fixture.drained < s_fixture.count)
        {
            sim_frame_t *frame = &s_fixture.frames[s_fixture.drained++];
            uint32_t pgn = (frame->id >> 8u) & 0x3FF00u;
            if (pgn == 0xEC00u || pgn == 0xEB00u)
            {
                int rc = j1939_tp_receive(s_fixture.peer, frame, s_fixture.now_ms);
                if (rc != 0)
                {
                    return rc;
                }
            }
        }
        while (s_fixture.head < s_fixture.tail)
        {
            int rc = sim_engine_receive(s_fixture.engine, &s_fixture.pending[s_fixture.head++], s_fixture.now_ms);
            if (rc != 0)
            {
                return rc;
            }
        }
    }
    s_fixture.head = s_fixture.tail = 0u;
    return 0;
}
static int fixture_advance(uint32_t duration_ms)
{
    for (uint32_t elapsed = 0u; elapsed < duration_ms && s_fixture.result == 0; elapsed++)
    {
        s_fixture.now_ms++;
        s_fixture.result = sim_engine_tick(s_fixture.engine, s_fixture.now_ms);
        int rc = j1939_tp_tick(s_fixture.peer, s_fixture.now_ms);
        if (rc != 0)
        {
            return rc;
        }
        rc = fixture_pump();
        if (rc != 0)
        {
            return rc;
        }
    }
    return 0;
}
static int fixture_feed(gbt27930_message_t message, const uint8_t *data, size_t len)
{
    if (len > 8u)
    {
        int rc = j1939_tp_send(s_fixture.peer, gbt27930_descriptor(message)->pgn, data, len, s_fixture.now_ms);
        return rc == 0 ? fixture_pump() : rc;
    }
    sim_frame_t frame = {.id = gbt27930_can_id(message, s_fixture.config.peer, s_fixture.config.source), .len = (uint8_t) len};
    memcpy(frame.data, data, len);
    return sim_engine_receive(s_fixture.engine, &frame, s_fixture.now_ms);
}
static int fixture_long(gbt27930_message_t message)
{
    gbt27930_values_t values;
    gbt27930_defaults(&values);
    uint8_t data[GBT27930_PAYLOAD_CAPACITY];
    size_t len;
    int rc = gbt27930_encode(message, s_fixture.config.version, &values, data, sizeof(data), &len);
    return rc == 0 ? fixture_feed(message, data, len) : rc;
}
static void fixture_dispose(void)
{
    sim_engine_destroy(s_fixture.engine);
    j1939_tp_destroy(s_fixture.peer);
    s_fixture.engine = NULL;
    s_fixture.peer = NULL;
}
static int fixture_recreate(void)
{
    sim_config_t *config = &s_fixture.config;
    s_fixture.engine = sim_engine_create(config, fixture_output, NULL);
    j1939_tp_options_t options = {.source = config->peer, .peer = config->source, .dt_interval_ms = 10u, .t1_ms = 750u, .t2_ms = 1250u, .t3_ms = 1250u, .t4_ms = 1050u, .send_cb = fixture_peer_output, .recv_cb = fixture_peer_payload};
    s_fixture.peer = j1939_tp_create(&options);
    return s_fixture.engine != NULL && s_fixture.peer != NULL ? 0 : -ENOMEM;
}
static int fixture_start(unsigned version, bool charger, bool ready, bool drop_ready)
{
    memset(&s_fixture, 0, sizeof(s_fixture));
    sim_config_defaults(&s_fixture.config);
    sim_config_t *config = &s_fixture.config;
    config->version = version;
    config->charger = charger;
    config->source = charger ? 0x56u : 0xF4u;
    config->peer = charger ? 0xF4u : 0x56u;
    config->ready = ready;
    config->ready_delay_ms = 0u;
    config->stop_after_ms = 0u;
    config->max_session_ms = 20000u;
    config->auto_aux_power_off = false;
    config->file_log = false;
    config->event_count = 1u;
    memcpy(config->events[0].stage, "done", 5u);
    memcpy(config->events[0].action, "aux_power_off", 14u);
    config->events[0].after_ms = 1200u;
    if (drop_ready)
    {
        config->fault_count = 1u;
        memcpy(config->faults[0].stage, "ready", 6u);
        memcpy(config->faults[0].message, "BRO", 4u);
        config->faults[0].action = SIM_FAULT_DROP;
        config->faults[0].count = 0u;
    }
    return fixture_recreate();
}
static bool frame_is(size_t index, gbt27930_message_t message)
{
    return ((s_fixture.frames[index].id >> 8u) & 0x3FF00u) == gbt27930_descriptor(message)->pgn;
}
static size_t count_since(gbt27930_message_t message, uint32_t time_ms)
{
    size_t count = 0u;
    for (size_t idx = 0u; idx < s_fixture.count; idx++)
    {
        if (frame_is(idx, message) && s_fixture.times[idx] >= time_ms)
        {
            count++;
        }
    }
    return count;
}
static uint32_t first_time(gbt27930_message_t message)
{
    for (size_t idx = 0u; idx < s_fixture.count; idx++)
    {
        if (frame_is(idx, message))
        {
            return s_fixture.times[idx];
        }
    }
    return UINT32_MAX;
}
static const uint8_t s_chm[] = {1u, 1u, 0u};
static const uint8_t s_crm_initial[] = {0u, 1u, 0u, 0u, 0u, 48u, 48u, 48u};
static const uint8_t s_crm_confirm[] = {170u, 1u, 0u, 0u, 0u, 48u, 48u, 48u};
static const uint8_t s_cml[] = {0x4Cu, 0x1Du, 0xD0u, 0x07u, 0xD0u, 0x07u, 0xA0u, 0x0Fu};
static const uint8_t s_ready[] = {170u};
static const uint8_t s_stop[] = {1u, 0u, 0u, 0xF0u};
static const uint8_t s_csd[] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
static int fixture_bms_ready(void)
{
    CHECK(fixture_feed(GBT27930_CHM, s_chm, sizeof(s_chm)) == 0);
    CHECK(fixture_advance(5u) == 0);
    CHECK(fixture_feed(GBT27930_CRM, s_crm_initial, sizeof(s_crm_initial)) == 0);
    CHECK(fixture_advance(150u) == 0);
    CHECK(fixture_feed(GBT27930_CRM, s_crm_confirm, sizeof(s_crm_confirm)) == 0);
    CHECK(fixture_advance(60u) == 0);
    CHECK(fixture_feed(GBT27930_CML, s_cml, sizeof(s_cml)) == 0);
    CHECK(fixture_advance(2u) == 0);
    return 0;
}
static int test_ready_gate(unsigned version, bool ready, bool drop_ready)
{
    CHECK(fixture_start(version, false, ready, drop_ready) == 0);
    CHECK(fixture_bms_ready() == 0);
    uint32_t early = s_fixture.now_ms;
    CHECK(fixture_feed(GBT27930_CRO, s_ready, sizeof(s_ready)) == 0);
    CHECK(fixture_advance(400u) == 0 && s_fixture.result == 0);
    CHECK(count_since(GBT27930_BCL, early) == 0u);
    /* A dropped AA does not satisfy the "sent BRO=AA" condition. */
    if (!drop_ready)
    {
        CHECK(count_since(GBT27930_BRO, early) > 0u);
    }
    fixture_dispose();
    return 0;
}
static int test_bms_stop(unsigned version, bool local)
{
    CHECK(fixture_start(version, false, true, false) == 0);
    CHECK(fixture_bms_ready() == 0);
    CHECK(fixture_feed(GBT27930_CRO, s_ready, sizeof(s_ready)) == 0);
    CHECK(fixture_advance(30u) == 0);
    CHECK(count_since(GBT27930_BCL, 0u) > 0u);
    if (local)
    {
        sim_engine_stop(s_fixture.engine, s_fixture.now_ms);
        CHECK(fixture_advance(40u) == 0);
        CHECK(count_since(GBT27930_BST, 0u) >= 1u);
    }
    uint32_t cst_received = s_fixture.now_ms;
    CHECK(fixture_feed(GBT27930_CST, s_stop, sizeof(s_stop)) == 0);
    CHECK(fixture_advance(100u) == 0);
    CHECK(count_since(GBT27930_BSD, 0u) > 0u);
    CHECK(count_since(GBT27930_BCL, cst_received + 1u) == 0u);
    CHECK(count_since(GBT27930_BST, first_time(GBT27930_BSD)) == 0u);
    if (!local)
    {
        CHECK(count_since(GBT27930_BST, 0u) == (version == 2023u ? 5u : 1u));
    }
    if (local)
    {
        CHECK(count_since(GBT27930_BST, cst_received + 1u) == 0u);
    }
    CHECK(fixture_feed(GBT27930_CSD, s_csd, sizeof(s_csd)) == 0);
    uint32_t csd_received = s_fixture.now_ms;
    CHECK(fixture_advance(1000u) == 0);
    if (version == 2015u)
    {
        CHECK(s_fixture.result == 0 && count_since(GBT27930_BSD, csd_received + 1u) >= 3u);
        CHECK(fixture_advance(300u) == 0 && s_fixture.result == 1);
    }
    else
    {
        CHECK(s_fixture.result == 1 && count_since(GBT27930_BSD, csd_received + 1u) == 0u);
    }
    fixture_dispose();
    return 0;
}
static int fixture_charger_charging(unsigned version)
{
    const uint8_t bhm[] = {0x4Cu, 0x1Du}, bcl[] = {0x68u, 0x10u, 0xB8u, 0x0Bu, 2u};
    CHECK(fixture_start(version, true, true, false) == 0);
    CHECK(fixture_feed(GBT27930_BHM, bhm, sizeof(bhm)) == 0);
    CHECK(fixture_advance(2u) == 0);
    CHECK(fixture_long(GBT27930_BRM) == 0 && fixture_advance(150u) == 0);
    CHECK(fixture_long(GBT27930_BCP) == 0 && fixture_advance(80u) == 0);
    CHECK(fixture_feed(GBT27930_BRO, s_ready, sizeof(s_ready)) == 0 && fixture_advance(2u) == 0);
    uint32_t bro_received = s_fixture.now_ms;
    CHECK(fixture_feed(GBT27930_BCL, bcl, sizeof(bcl)) == 0 && fixture_advance(300u) == 0);
    CHECK((count_since(GBT27930_CCS, 0u) > 0u) == (version == 2023u));
    CHECK(count_since(GBT27930_CML, bro_received + 1u) == 0u && count_since(GBT27930_CTS, bro_received + 1u) == 0u);
    CHECK(fixture_long(GBT27930_BCS) == 0 && fixture_advance(60u) == 0);
    CHECK(count_since(GBT27930_CCS, 0u) > 0u);
    return 0;
}
static int test_charger_stop(unsigned version, bool local)
{
    CHECK(fixture_charger_charging(version) == 0);
    if (local)
    {
        sim_engine_stop(s_fixture.engine, s_fixture.now_ms);
        CHECK(fixture_advance(5u) == 0);
    }
    CHECK(fixture_feed(GBT27930_BST, s_stop, sizeof(s_stop)) == 0);
    uint32_t bst_received = s_fixture.now_ms;
    CHECK(fixture_advance(50u) == 0);
    CHECK(count_since(GBT27930_CST, bst_received + 1u) >= 4u && count_since(GBT27930_CSD, 0u) == 0u);
    const uint8_t bsd[] = {50u, 0x4Au, 1u, 0x5Eu, 1u, 75u, 80u};
    CHECK(fixture_feed(GBT27930_BSD, bsd, sizeof(bsd)) == 0);
    uint32_t bsd_received = s_fixture.now_ms;
    CHECK(fixture_advance(1000u) == 0 && s_fixture.result == 0);
    CHECK(count_since(GBT27930_CST, bsd_received + 1u) == 0u);
    CHECK(count_since(GBT27930_CSD, bsd_received + 1u) >= 4u);
    CHECK(fixture_advance(300u) == 0 && s_fixture.result == 1);
    fixture_dispose();
    return 0;
}
static int test_error_until_crm(unsigned version)
{
    CHECK(fixture_start(version, false, true, false) == 0);
    /* Failure at a deterministic boundary, then a recovery handshake without CRM yet. */
    fixture_dispose();
    s_fixture.config.first_timeout_ms[GBT27930_CHM] = 10u;
    s_fixture.config.timeout_ms[GBT27930_CHM] = 10u;
    s_fixture.config.recovery_wait_ms = 20u;
    s_fixture.config.retries = 1u;
    s_fixture.config.reconnect_allowed = true;
    s_fixture.engine = sim_engine_create(&s_fixture.config, fixture_output, NULL);
    j1939_tp_options_t options = {.source = 0x56u, .peer = 0xF4u, .dt_interval_ms = 10u, .t1_ms = 750u, .t2_ms = 1250u, .t3_ms = 1250u, .t4_ms = 1050u, .send_cb = fixture_peer_output, .recv_cb = fixture_peer_payload};
    s_fixture.peer = j1939_tp_create(&options);
    CHECK(s_fixture.engine != NULL && s_fixture.peer != NULL);
    CHECK(fixture_advance(35u) == 0);
    /* Receive CHM before the restarted handshake times out. */
    CHECK(fixture_feed(GBT27930_CHM, s_chm, sizeof(s_chm)) == 0);
    CHECK(fixture_advance(270u) == 0);
    if (version == 2015u)
    {
        CHECK(count_since(GBT27930_BEM, 31u) > 0u);
    }
    else
    {
        CHECK(count_since(GBT27930_BEM, 31u) == 0u);
    }
    CHECK(fixture_feed(GBT27930_CRM, s_crm_initial, sizeof(s_crm_initial)) == 0);
    uint32_t crm_received = s_fixture.now_ms;
    CHECK(fixture_advance(300u) == 0 && count_since(GBT27930_BEM, crm_received + 1u) == 0u);
    fixture_dispose();
    return 0;
}
static int test_bsd_until_crm(void)
{
    CHECK(fixture_start(2015u, false, true, false) == 0);
    CHECK(fixture_bms_ready() == 0);
    CHECK(fixture_feed(GBT27930_CRO, s_ready, sizeof(s_ready)) == 0 && fixture_advance(30u) == 0);
    CHECK(fixture_feed(GBT27930_CST, s_stop, sizeof(s_stop)) == 0 && fixture_advance(30u) == 0);
    CHECK(fixture_feed(GBT27930_CSD, s_csd, sizeof(s_csd)) == 0 && fixture_advance(300u) == 0);
    CHECK(count_since(GBT27930_BSD, 0u) >= 2u);
    CHECK(fixture_feed(GBT27930_CRM, s_crm_initial, sizeof(s_crm_initial)) == 0);
    uint32_t crm_received = s_fixture.now_ms;
    CHECK(fixture_advance(300u) == 0 && s_fixture.result == 0);
    CHECK(count_since(GBT27930_BSD, crm_received + 1u) == 0u);
    fixture_dispose();
    return 0;
}
static int test_bcp_update(void)
{
    CHECK(fixture_start(2023u, false, true, false) == 0);
    fixture_dispose();
    s_fixture.config.event_count = 2u;
    sim_event_t *update = &s_fixture.config.events[1];
    memcpy(update->stage, "ready", 6u);
    memcpy(update->action, "set_parameter", 14u);
    memcpy(update->parameter, "bcp_max_voltage_mv", 19u);
    update->value = 440000;
    s_fixture.engine = sim_engine_create(&s_fixture.config, fixture_output, NULL);
    j1939_tp_options_t options = {.source = 0x56u, .peer = 0xF4u, .dt_interval_ms = 10u, .t1_ms = 750u, .t2_ms = 1250u, .t3_ms = 1250u, .t4_ms = 1050u, .send_cb = fixture_peer_output, .recv_cb = fixture_peer_payload};
    s_fixture.peer = j1939_tp_create(&options);
    CHECK(s_fixture.engine != NULL && s_fixture.peer != NULL);
    CHECK(fixture_bms_ready() == 0);
    CHECK(count_since(GBT27930_BRO, 0u) == 0u);
    CHECK(fixture_advance(700u) == 0 && s_fixture.updated_bcp_received == 2u);
    CHECK(first_time(GBT27930_BRO) > s_fixture.last_updated_bcp_ms);
    for (size_t idx = 0u; idx < s_fixture.count; idx++)
    {
        if (frame_is(idx, GBT27930_BRO))
        {
            CHECK(s_fixture.frames[idx].data[0] == 0u);
            break;
        }
    }
    fixture_dispose();
    return 0;
}
static int test_charger_bcp_update(void)
{
    const uint8_t bhm[] = {0x4Cu, 0x1Du}, bro_initial[] = {0u};
    CHECK(fixture_start(2023u, true, true, false) == 0);
    fixture_dispose();
    CHECK(gbt27930_set(&s_fixture.config.values, "cml_max_voltage_mv", 450000) == 0);
    CHECK(fixture_recreate() == 0);
    CHECK(fixture_feed(GBT27930_BHM, bhm, sizeof(bhm)) == 0 && fixture_advance(2u) == 0);
    CHECK(fixture_long(GBT27930_BRM) == 0 && fixture_advance(150u) == 0);
    CHECK(fixture_long(GBT27930_BCP) == 0 && fixture_advance(80u) == 0);
    CHECK(fixture_feed(GBT27930_BRO, bro_initial, sizeof(bro_initial)) == 0 && fixture_advance(2u) == 0);
    CHECK(fixture_long(GBT27930_BCP) == 0 && fixture_advance(80u) == 0);
    CHECK(count_since(GBT27930_CEM, 0u) == 0u);
    gbt27930_values_t values;
    gbt27930_defaults(&values);
    CHECK(gbt27930_set(&values, "bcp_voltage_mv", 500000) == 0);
    uint8_t data[GBT27930_PAYLOAD_CAPACITY];
    size_t len;
    CHECK(gbt27930_encode(GBT27930_BCP, 2023u, &values, data, sizeof(data), &len) == 0);
    CHECK(fixture_feed(GBT27930_BCP, data, len) == 0 && fixture_advance(80u) == 0);
    CHECK(count_since(GBT27930_CEM, 0u) > 0u);
    fixture_dispose();
    return 0;
}
static int test_failed_statistics_until_crm(void)
{
    CHECK(fixture_start(2015u, false, true, false) == 0);
    fixture_dispose();
    s_fixture.config.first_timeout_ms[GBT27930_CSD] = 100u;
    s_fixture.config.recovery_wait_ms = 20u;
    s_fixture.config.retries = 1u;
    CHECK(fixture_recreate() == 0);
    CHECK(fixture_bms_ready() == 0);
    CHECK(fixture_feed(GBT27930_CRO, s_ready, sizeof(s_ready)) == 0 && fixture_advance(30u) == 0);
    CHECK(fixture_feed(GBT27930_CST, s_stop, sizeof(s_stop)) == 0 && fixture_advance(700u) == 0);
    CHECK(count_since(GBT27930_BSD, 500u) > 0u && count_since(GBT27930_BEM, 500u) > 0u);
    CHECK(fixture_feed(GBT27930_CRM, s_crm_initial, sizeof(s_crm_initial)) == 0);
    uint32_t crm_received = s_fixture.now_ms;
    CHECK(fixture_advance(300u) == 0 && s_fixture.result == 0);
    CHECK(count_since(GBT27930_BSD, crm_received + 1u) == 0u && count_since(GBT27930_BEM, crm_received + 1u) == 0u);
    fixture_dispose();
    return 0;
}
static int test_cem_until_crm(void)
{
    CHECK(fixture_start(2015u, true, true, false) == 0);
    fixture_dispose();
    s_fixture.config.recovery_wait_ms = 20u;
    s_fixture.config.retries = 1u;
    CHECK(fixture_recreate() == 0);
    const uint8_t bem[] = {1u, 0u, 0u, 0u}, bhm[] = {0x4Cu, 0x1Du};
    CHECK(fixture_feed(GBT27930_BEM, bem, sizeof(bem)) == 0 && fixture_advance(300u) == 0);
    CHECK(count_since(GBT27930_CEM, 30u) > 0u);
    CHECK(fixture_feed(GBT27930_BEM, bem, sizeof(bem)) == 0);
    CHECK(fixture_feed(GBT27930_BHM, bhm, sizeof(bhm)) == 0);
    uint32_t before_crm = s_fixture.now_ms;
    CHECK(fixture_advance(300u) == 0 && s_fixture.result == 0);
    CHECK(count_since(GBT27930_CRM, before_crm + 1u) > 0u && count_since(GBT27930_CEM, before_crm + 1u) == 0u);
    fixture_dispose();
    return 0;
}
static int test_timeout_evidence(void)
{
    CHECK(fixture_start(2015u, false, true, false) == 0);
    fixture_dispose();
    s_fixture.config.timeout_ms[GBT27930_CRO] = 40u;
    CHECK(fixture_recreate() == 0);
    CHECK(fixture_bms_ready() == 0 && fixture_advance(300u) == 0);
    const uint8_t not_ready[] = {0u};
    CHECK(fixture_feed(GBT27930_CRO, not_ready, sizeof(not_ready)) == 0);
    uint32_t received = s_fixture.now_ms;
    CHECK(fixture_advance(39u) == 0);
    sim_status_t status;
    sim_engine_status(s_fixture.engine, &status);
    CHECK(status.stage == SIM_STAGE_READY && !status.failed);
    CHECK(fixture_advance(1u) == 0);
    sim_engine_status(s_fixture.engine, &status);
    CHECK(status.stage == SIM_STAGE_RECOVERY && status.failed);
    bool found = false;
    for (unsigned idx = 0u; idx < status.history_count; idx++)
    {
        const sim_transition_t *entry = &status.history[idx % SIM_STAGE_HISTORY];
        if (entry->stage == SIM_STAGE_READY)
        {
            found = true;
            CHECK(entry->closed && entry->failed && entry->exited_ms == received + 40u);
            CHECK(entry->timeout_kind == SIM_TIMEOUT_CONTINUOUS && entry->timeout_limit_ms == 40u);
            CHECK(entry->timeout_origin_ms == received && entry->timeout_wait_ms == 40u);
            CHECK(entry->has_error_rx && entry->error_rx_ms == received && entry->error_message == GBT27930_CRO);
            CHECK(entry->has_last_rx && entry->last_rx_ms == received && entry->last_rx_message == GBT27930_CRO);
        }
    }
    CHECK(found);
    fixture_dispose();
    return 0;
}
int main(void)
{
    if (test_timeout_evidence() != 0 || test_bsd_until_crm() != 0 || test_bcp_update() != 0 || test_charger_bcp_update() != 0 || test_failed_statistics_until_crm() != 0 || test_cem_until_crm() != 0)
    {
        fixture_dispose();
        return 1;
    }
    for (unsigned version = 2015u; version <= 2023u; version += 8u)
    {
        if (test_ready_gate(version, false, false) != 0 || test_ready_gate(version, true, true) != 0 || test_bms_stop(version, true) != 0 ||
            test_bms_stop(version, false) != 0 || test_charger_stop(version, true) != 0 || test_charger_stop(version, false) != 0 || test_error_until_crm(version) != 0)
        {
            fixture_dispose();
            return 1;
        }
    }
    puts("PASS: appendix message boundaries, early CRO, dropped BRO AA, CST/BSD/CSD ordering, versioned BST burst/CCS and auxiliary-power event");
    return 0;
}
