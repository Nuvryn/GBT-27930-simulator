/** @file test_sim_control.c @brief Controlled command, persistence and fault ownership checks. */
#define _POSIX_C_SOURCE 200809L
#include "ui/sim_control.h"
#include "fault_injection/sim_fault.h"
#include "support/config_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(expr)                                            \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); \
            exit(1);                                           \
        }                                                      \
    } while (0)
static unsigned sent;
static int send_frame(void *user, const sim_frame_t *frame)
{
    (void) user;
    (void) frame;
    sent++;
    return 0;
}
static int dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len)
{
    (void) user;
    (void) receive;
    (void) message;
    (void) id;
    (void) data;
    (void) len;
    sent++;
    return 0;
}
static void validate(void)
{
    sim_config_t config;
    char error[256];
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"start_mode\":\"manual\",\"ui\":{\"enabled\":true,\"input_device\":\"/dev/input/event2\",\"rotation\":90}}", &config, error, sizeof(error)) == 0);
    CHECK(config.ui_enabled && config.ui_rotation == 90u);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"enabled\":true}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"rotation\":91}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"width\":800}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"start_mode\":\"manual\",\"ui\":{\"enabled\":true,\"backend\":\"web\",\"web\":{\"width\":480,\"height\":272,\"fps\":30}}}", &config, error, sizeof(error)) == 0);
    CHECK(strcmp(config.ui_backend, "web") == 0 && config.ui_web_width == 480u && config.ui_web_height == 272u && config.ui_web_fps == 30u);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"width\":479}}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"height\":1081}}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"fps\":0}}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"port\":5900,\"vnc_port\":5900}}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"port\":65536}}}", &config, error, sizeof(error)) != 0);
    CHECK(sim_config_parse("{\"role\":\"charger\",\"version\":2015,\"ui\":{\"web\":{\"unknown\":1}}}", &config, error, sizeof(error)) != 0);
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        const gbt27930_field_t *field = gbt27930_field(idx);
        char text[64];
        int64_t value;
        sim_control_format_value(field, field->initial, text, sizeof(text));
        CHECK(sim_control_parse_value(field, text, &value) == 0 && value == field->initial);
    }
}
static void cancellation(void)
{
    sim_config_t config;
    sim_config_defaults(&config);
    config.fault_count = 1u;
    config.faults[0] = (sim_fault_rule_t){.stage = "*", .message = "BCL", .action = SIM_FAULT_DELAY, .count = 0u, .delay_ms = 100u};
    sim_fault_t *fault = sim_fault_create(&config, dispatch, NULL);
    CHECK(fault != NULL);
    uint8_t bytes[8] = {0};
    sent = 0u;
    CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 0u, 1u, bytes, 5u) == 0);
    sim_fault_rule_t manual = {.stage = "*", .message = "BCS", .action = SIM_FAULT_DELAY, .count = 1u, .delay_ms = 100u};
    CHECK(sim_fault_manual(fault, &manual, 0u) == 0);
    CHECK(sim_fault_apply(fault, false, "BCS", "charging", 0u, 0u, 2u, bytes, 8u) == 0);
    CHECK(sim_fault_manual(fault, NULL, 50u) == 0);
    CHECK(sim_fault_tick(fault, 100u) == 0 && sent == 1u);
    manual.message[2] = 'L';
    manual.action = SIM_FAULT_DROP;
    CHECK(sim_fault_manual(fault, &manual, 100u) == 0);
    CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 100u, 1u, bytes, 5u) == 0);
    bool enabled;
    uint32_t hits;
    sim_fault_manual_status(fault, &enabled, &hits);
    CHECK(!enabled && hits == 1u);
    CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 100u, 1u, bytes, 5u) == 0);
    CHECK(sim_fault_tick(fault, 200u) == 0 && sent == 2u);
    sim_fault_destroy(fault);
}
static void commands_and_save(void)
{
    char directory[] = "/tmp/gbt_ui_control_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[256];
    (void) snprintf(path, sizeof(path), "%s/config.json", directory);
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    CHECK(fputs("{}", file) >= 0);
    CHECK(fclose(file) == 0);
    sim_config_t config;
    sim_config_defaults(&config);
    config.manual_start = true;
    strcpy(config.ui_backend, "web");
    config.ui_web_width = 1024u;
    config.ui_web_port = 8123u;
    config.retries = 0u;
    config.reconnect_allowed = false;
    config.fault_count = 1u;
    config.faults[0] = (sim_fault_rule_t){.stage = "*", .message = "BMV", .action = SIM_FAULT_DROP, .count = 2u, .delay_ms = 100u};
    config.event_count = 1u;
    config.events[0] = (sim_event_t){.stage = "charging", .action = "set_parameter", .parameter = "ccs_current_ma", .after_ms = 100u, .value = 50000};
    memcpy(config.values.manufacturer, "\"ABC", 4u);
    sim_session_t *session = sim_session_create(&config, send_frame, NULL);
    CHECK(session != NULL);
    gbt27930_values_t invalid = config.values;
    invalid.cell_count = SIZE_MAX;
    CHECK(sim_session_values(session, &invalid) == -EINVAL);
    invalid = config.values;
    CHECK(gbt27930_set(&invalid, "bcl_voltage_mv", 410000) == 0);
    CHECK(sim_session_values(session, &invalid) == -EPERM);
    sim_control_t *control = sim_control_create(&config, path);
    CHECK(control != NULL);
    sim_control_request_t request = {.kind = SIM_CONTROL_APPLY, .values = config.values};
    CHECK(gbt27930_set(&request.values, "cml_max_voltage_mv", 700000) == 0);
    CHECK(sim_control_submit(control, &request) == 0);
    CHECK(sim_control_step(control, session, 100000u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_control_result_t result;
    CHECK(sim_control_result(control, &result) == 1 && result.result == 0);
    sim_control_view_t view;
    CHECK(sim_control_read(control, &view) == 0 && view.status.revision == 2u);
    CHECK(sim_control_save(control) == 0);
    CHECK(sim_control_save(control) == -EBUSY);
    sim_control_work(control);
    CHECK(sim_control_read(control, &view) == 0);
    if (view.save_result != 0)
    {
        char diagnostic[1152];
        uint32_t diagnostic_cursor = 0u;
        while (sim_control_log_read(control, &diagnostic_cursor, diagnostic, sizeof(diagnostic)) == 1)
        {
            fprintf(stderr, "%s\n", diagnostic);
        }
    }
    CHECK(view.save_result == 0 && view.saved_revision == 2u);
    sim_config_t restored;
    char error[256];
    CHECK(sim_config_load(path, &restored, error, sizeof(error)) == 0);
    CHECK(strcmp(restored.ui_backend, "web") == 0 && restored.ui_web_width == 1024u && restored.ui_web_port == 8123u);
    CHECK(restored.fault_count == 1u && restored.faults[0].count == 2u && strcmp(restored.faults[0].message, "BMV") == 0);
    CHECK(restored.event_count == 1u && restored.events[0].value == 50000 && strcmp(restored.events[0].parameter, "ccs_current_ma") == 0);
    CHECK(memcmp(restored.values.manufacturer, config.values.manufacturer, 4u) == 0 && restored.values.cell_count == config.values.cell_count);
    CHECK(gbt27930_get(&restored.values, "cml_max_voltage_mv") == 700000);
    request.kind = SIM_CONTROL_START;
    CHECK(sim_control_submit(control, &request) == 0);
    CHECK(sim_control_step(control, session, 100000u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    CHECK(sim_control_result(control, &result) == 1 && result.result == 0);
    sim_control_work(control);
    CHECK(sim_session_active(session));
    sim_control_stop(control, 0u);
    CHECK(sim_control_step(control, session, 100001u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    CHECK(sim_control_read(control, &view) == 0 && view.stop_result == -ESTALE && view.status.stage == SIM_STAGE_HANDSHAKE);

    request.kind = SIM_CONTROL_APPLY;
    request.session_id = 0u;
    CHECK(sim_control_submit(control, &request) == 0);
    CHECK(sim_control_step(control, session, 100001u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    CHECK(sim_control_result(control, &result) == 1 && result.result == -ESTALE);
    request.session_id = 1u;
    for (unsigned idx = 0u; idx < SIM_CONTROL_QUEUE; idx++)
    {
        CHECK(sim_control_submit(control, &request) == 0);
    }
    CHECK(sim_control_submit(control, &request) == -EBUSY);
    sim_control_stop(control, 1u);
    for (unsigned idx = 0u; idx < SIM_CONTROL_QUEUE; idx++)
    {
        CHECK(sim_control_step(control, session, 100002u) == 0);
        CHECK(sim_control_publish(control, session) == 0);
        CHECK(sim_control_result(control, &result) == 1 && result.result == -EBUSY);
    }
    CHECK(sim_control_read(control, &view) == 0 && view.status.stage == SIM_STAGE_STOPPING);
    for (unsigned idx = 0u; idx < SIM_UI_LOG_LINES + 10u; idx++)
    {
        sim_control_log(control, "TEST", "message", idx);
    }
    CHECK(sim_control_read(control, &view) == 0 && view.log_dropped == 10u);
    uint32_t cursor = 0u;
    char line[SIM_UI_LOG_LINE_SIZE];
    CHECK(sim_control_log_read(control, &cursor, line, sizeof(line)) == 1 && cursor == 11u);
    sim_control_log(control, "DETAIL", "hidden", 0u);
    CHECK(sim_control_read(control, &view) == 0 && view.log_sequence == SIM_UI_LOG_LINES + 10u);
    CHECK(config_store_write(directory, "bad", 3u) != 0);
    CHECK(sim_config_load(path, &restored, error, sizeof(error)) == 0);
    sim_control_destroy(control);
    sim_session_destroy(session);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
}
int main(void)
{
    validate();
    cancellation();
    commands_and_save();
    puts("PASS: UI controls, exact values, save roundtrip, rule cancellation and bounded logs");
    return 0;
}
