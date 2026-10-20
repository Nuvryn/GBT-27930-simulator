/** @file test_sim_core.c @brief Independent wire vectors and controlled-time TP regression. */
#include "config/sim_config.h"
#include "transport/j1939_tp.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define TEST_CHECK(expr)                                            \
    do                                                              \
    {                                                               \
        if (!(expr))                                                \
        {                                                           \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            return 1;                                               \
        }                                                           \
    } while (0)
typedef struct
{
    sim_frame_t frame;
    bool sender;
} test_queued_t;
typedef struct
{
    j1939_tp_t *tp;
    bool sender;
    uint32_t now;
    size_t received;
    uint8_t payload[1785];
    int result;
    uint32_t last_dt;
    unsigned dt_count;
    bool fail_send;
} test_endpoint_t;
static test_queued_t s_test_queue[2048];
static size_t s_test_head, s_test_tail;
static int s_test_gap_error;
static int test_send(void *user, const sim_frame_t *frame)
{
    test_endpoint_t *endpoint = user;
    if (endpoint->fail_send)
    {
        return -ENETDOWN;
    }
    if (s_test_tail >= 2048u)
    {
        return -ENOSPC;
    }
    s_test_queue[s_test_tail++] = (test_queued_t){.frame = *frame, .sender = endpoint->sender};
    if (((frame->id >> 16u) & 255u) == 0xEBu)
    {
        if (endpoint->dt_count != 0u && (uint32_t) (endpoint->now - endpoint->last_dt) < 10u)
        {
            s_test_gap_error = 1;
        }
        endpoint->dt_count++;
        endpoint->last_dt = endpoint->now;
    }
    return 0;
}
static void test_recv(void *user, uint32_t pgn, const uint8_t *data, size_t len)
{
    test_endpoint_t *endpoint = user;
    (void) pgn;
    endpoint->received = len;
    memcpy(endpoint->payload, data, len);
}
static void test_event(void *user, bool tx, uint32_t pgn, int result)
{
    test_endpoint_t *endpoint = user;
    (void) tx;
    (void) pgn;
    endpoint->result = result;
}
static j1939_tp_t *test_create(test_endpoint_t *endpoint, bool sender)
{
    endpoint->sender = sender;
    j1939_tp_options_t options = {.source = sender ? 0xF4u : 0x56u, .peer = sender ? 0x56u : 0xF4u, .dt_interval_ms = 10u, .t1_ms = 750u, .t2_ms = 1250u, .t3_ms = 1250u, .t4_ms = 1050u, .send_cb = test_send, .recv_cb = test_recv, .event_cb = test_event, .cb_user = endpoint};
    return j1939_tp_create(&options);
}
static int test_tp(void)
{
    test_endpoint_t sender = {0}, receiver = {0};
    sender.tp = test_create(&sender, true);
    receiver.tp = test_create(&receiver, false);
    TEST_CHECK(sender.tp != NULL && receiver.tp != NULL);
    uint8_t payload[1785];
    for (size_t idx = 0u; idx < sizeof(payload); idx++)
    {
        payload[idx] = (uint8_t) idx;
    }
    TEST_CHECK(j1939_tp_send(sender.tp, 0x200u, payload, sizeof(payload), 0u) == 0);
    TEST_CHECK(j1939_tp_send(sender.tp, 0x200u, payload, 49u, 0u) == -EBUSY);
    for (uint32_t now = 0u; now < 4000u && (receiver.received == 0u || j1939_tp_busy(sender.tp)); now++)
    {
        sender.now = now;
        receiver.now = now;
        TEST_CHECK(j1939_tp_tick(sender.tp, now) == 0);
        TEST_CHECK(j1939_tp_tick(receiver.tp, now) == 0);
        while (s_test_head < s_test_tail)
        {
            test_queued_t item = s_test_queue[s_test_head++];
            TEST_CHECK(j1939_tp_receive(item.sender ? receiver.tp : sender.tp, &item.frame, now) == 0);
        }
    }
    TEST_CHECK(receiver.received == sizeof(payload) && memcmp(receiver.payload, payload, sizeof(payload)) == 0);
    TEST_CHECK(sender.dt_count == 255u && s_test_gap_error == 0 && !j1939_tp_busy(sender.tp));
    j1939_tp_reset(receiver.tp);
    s_test_head = s_test_tail = 0u;
    sim_frame_t rts = {.id = 0x1CEC56F4u, .len = 8u, .data = {0x10u, 9u, 0u, 2u, 1u, 0u, 2u, 0u}};
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == 0);
    sim_frame_t bad = {.id = 0x1CEB56F4u, .len = 8u, .data = {0u}};
    TEST_CHECK(j1939_tp_receive(receiver.tp, &bad, 1u) == -EBADMSG);
    j1939_tp_reset(sender.tp);
    TEST_CHECK(j1939_tp_send(sender.tp, 0x200u, payload, 49u, UINT32_MAX - 10u) == 0);
    TEST_CHECK(j1939_tp_tick(sender.tp, 1239u) == -ETIMEDOUT);
    j1939_tp_destroy(sender.tp);
    j1939_tp_destroy(receiver.tp);
    return 0;
}
static int test_codec(void)
{
    gbt27930_values_t values, decoded;
    gbt27930_defaults(&values);
    uint8_t data[1785];
    size_t len = 0u;
    const uint8_t bcl[] = {0x68u, 0x10u, 0xB8u, 0x0Bu, 2u};
    TEST_CHECK(gbt27930_encode(GBT27930_BCL, 2015u, &values, data, sizeof(data), &len) == 0);
    TEST_CHECK(len == sizeof(bcl) && memcmp(data, bcl, sizeof(bcl)) == 0);
    const uint8_t bcp[] = {0x6Du, 1u, 0xB8u, 0x0Bu, 0xF4u, 1u, 0x94u, 0x11u, 110u, 0xF4u, 1u, 0xA0u, 0x0Fu};
    TEST_CHECK(gbt27930_encode(GBT27930_BCP, 2015u, &values, data, sizeof(data), &len) == 0 && memcmp(data, bcp, sizeof(bcp)) == 0);
    TEST_CHECK(gbt27930_set(&values, "bcl_current_ma", 400001) == -ERANGE);
    TEST_CHECK(gbt27930_set(&values, "bcl_voltage_mv", 400001) == -ERANGE);
    for (unsigned version = 2015u; version <= 2023u; version += 8u)
    {
        for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT; idx++)
        {
            TEST_CHECK(gbt27930_encode((gbt27930_message_t) idx, version, &values, data, sizeof(data), &len) == 0);
            TEST_CHECK(gbt27930_decode((gbt27930_message_t) idx, version, data, len, &decoded) == 0);
            if (idx == GBT27930_CCS)
            {
                TEST_CHECK(len == (version == 2015u ? 8u : 7u));
            }
        }
    }
    TEST_CHECK(gbt27930_decode(GBT27930_BCL, 2015u, bcl, 4u, &decoded) == -EMSGSIZE);
    const uint8_t cts[] = {0u, 0u, 0u, 1u, 1u, 0x26u, 0x20u};
    TEST_CHECK(gbt27930_encode(GBT27930_CTS, 2015u, &values, data, sizeof(data), &len) == 0 && memcmp(data, cts, sizeof(cts)) == 0);
    data[0] = 0xFAu;
    TEST_CHECK(gbt27930_decode(GBT27930_CTS, 2015u, data, len, &decoded) == -EBADMSG);
    TEST_CHECK(gbt27930_set(&values, "bro_ready", 1) == -ERANGE);
    TEST_CHECK(gbt27930_set(&values, "battery_type", 17) == -ERANGE);
    values.cell_count = 1u;
    values.cells_mv[0] = 3650u;
    values.cell_groups[0] = 3u;
    TEST_CHECK(gbt27930_encode(GBT27930_BMV, 2015u, &values, data, sizeof(data), &len) == 0);
    TEST_CHECK(len == 2u && data[0] == 0x6Du && data[1] == 0x31u);
    TEST_CHECK(gbt27930_decode(GBT27930_BMV, 2015u, data, len, &decoded) == 0 && decoded.cell_groups[0] == 3u && decoded.cells_mv[0] == 3650u);
    return 0;
}
/** T1/T2/T4, short frames, session collision, early ACK and real send failures. */
static int test_tp_boundaries(void)
{
    test_endpoint_t receiver = {0}, sender = {0};
    receiver.tp = test_create(&receiver, false);
    sender.tp = test_create(&sender, true);
    TEST_CHECK(receiver.tp != NULL && sender.tp != NULL);
    s_test_head = s_test_tail = 0u;
    sim_frame_t rts = {.id = 0x1CEC56F4u, .len = 8u, .data = {0x10u, 21u, 0u, 3u, 3u, 0u, 2u, 0u}};
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == 0);
    TEST_CHECK(s_test_queue[s_test_tail - 1u].frame.data[1] == 3u);
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == -EBUSY);
    TEST_CHECK(s_test_queue[s_test_tail - 1u].frame.data[1] == 1u);
    sim_frame_t dt = {.id = 0x1CEB56F4u, .len = 8u, .data = {1u}};
    TEST_CHECK(j1939_tp_receive(receiver.tp, &dt, 1u) == 0);
    TEST_CHECK(j1939_tp_tick(receiver.tp, 750u) == 0);
    TEST_CHECK(j1939_tp_tick(receiver.tp, 751u) == -ETIMEDOUT);
    j1939_tp_reset(receiver.tp);
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == 0);
    TEST_CHECK(j1939_tp_tick(receiver.tp, 1249u) == 0 && j1939_tp_tick(receiver.tp, 1250u) == -ETIMEDOUT);
    rts.len = 7u;
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == -EMSGSIZE);
    rts.len = 8u;
    rts.data[1] = 0u;
    TEST_CHECK(j1939_tp_receive(receiver.tp, &rts, 0u) == -EBADMSG);
    uint8_t payload[49] = {0};
    TEST_CHECK(j1939_tp_send(sender.tp, 0x200u, payload, sizeof(payload), 0u) == 0);
    sim_frame_t ack = {.id = 0x1CECF456u, .len = 8u, .data = {0x13u, 49u, 0u, 7u, 255u, 0u, 2u, 0u}};
    TEST_CHECK(j1939_tp_receive(sender.tp, &ack, 1u) == 0 && j1939_tp_busy(sender.tp));
    sim_frame_t hold = {.id = 0x1CECF456u, .len = 8u, .data = {0x11u, 0u, 255u, 255u, 255u, 0u, 2u, 0u}};
    TEST_CHECK(j1939_tp_receive(sender.tp, &hold, 0u) == 0);
    TEST_CHECK(j1939_tp_tick(sender.tp, 1049u) == 0 && j1939_tp_tick(sender.tp, 1050u) == -ETIMEDOUT);
    sender.fail_send = true;
    TEST_CHECK(j1939_tp_send(sender.tp, 0x200u, payload, sizeof(payload), 0u) == -ENETDOWN && !j1939_tp_busy(sender.tp));
    j1939_tp_destroy(receiver.tp);
    j1939_tp_destroy(sender.tp);
    return 0;
}
static int test_config(void)
{
    sim_config_t config;
    char error[256];
    TEST_CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015}", &config, error, sizeof(error)) == 0 && config.source == 0xF4u);
    TEST_CHECK(!config.candump);
    TEST_CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015,\"cell_group\":[3],\"cell_voltage_mv\":[3650]}", &config, error, sizeof(error)) == 0 && config.values.cell_groups[0] == 3u);
    TEST_CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015,\"cell_group\":[3,4],\"cell_voltage_mv\":[3650]}", &config, error, sizeof(error)) != 0);
    const char *bad[] = {"{}", "{\"role\":\"bms\",\"role\":\"charger\",\"version\":2015}",
        "{\"role\":\"bms\",\"version\":2015,\"speed\":10}", "{\"role\":\"bms\",\"version\":2022}",
        "{\"role\":\"bms\",\"version\":2015,\"unknown\":1}", "{\"role\":\"bms\",\"version\":2015,\"log_path\":5}"};
    for (size_t idx = 0u; idx < sizeof(bad) / sizeof(bad[0]); idx++)
    {
        TEST_CHECK(sim_config_parse(bad[idx], &config, error, sizeof(error)) != 0);
    }
    return 0;
}
int main(void)
{
    if (test_codec() != 0 || test_config() != 0 || test_tp() != 0 || test_tp_boundaries() != 0)
    {
        return 1;
    }
    puts("PASS: codec vectors, configuration validation and bounded TP");
    return 0;
}
