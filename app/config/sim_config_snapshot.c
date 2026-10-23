/** @file sim_config_snapshot.c @brief cJSON serialization of the validated effective startup configuration. */
#include "config/sim_config.h"
#include "cJSON.h"
#include <errno.h>
#include <string.h>

typedef struct
{
    bool valid;
} sim_config_builder_t;

/** Takes node ownership on every path. */
static void add_node(sim_config_builder_t *builder, cJSON *parent, const char *key, cJSON *node)
{
    if (!builder->valid || parent == NULL || node == NULL)
    {
        builder->valid = false;
        cJSON_Delete(node);
        return;
    }
    cJSON_bool added = key != NULL ? cJSON_AddItemToObject(parent, key, node) : cJSON_AddItemToArray(parent, node);
    if (!added)
    {
        builder->valid = false;
        cJSON_Delete(node);
    }
}
static void add_number(sim_config_builder_t *builder, cJSON *parent, const char *key, int64_t value)
{
    add_node(builder, parent, key, cJSON_CreateNumber((double) value));
}
static void add_string(sim_config_builder_t *builder, cJSON *parent, const char *key, const char *value)
{
    add_node(builder, parent, key, cJSON_CreateString(value));
}
static cJSON *add_container(sim_config_builder_t *builder, cJSON *parent, const char *key, bool array)
{
    cJSON *node = array ? cJSON_CreateArray() : cJSON_CreateObject();
    add_node(builder, parent, key, node);
    return builder->valid ? node : NULL;
}
static void add_bytes(sim_config_builder_t *builder, cJSON *root, const char *key, const uint8_t *bytes, size_t count)
{
    cJSON *array = add_container(builder, root, key, true);
    for (size_t idx = 0u; idx < count && builder->valid; idx++)
    {
        add_number(builder, array, NULL, bytes[idx]);
    }
}
static void add_fixed_string(sim_config_builder_t *builder, cJSON *root, const char *key, const char *value, size_t len)
{
    char text[18];
    memcpy(text, value, len);
    text[len] = '\0';
    add_string(builder, root, key, text);
}
static void add_root(sim_config_builder_t *builder, cJSON *root, const sim_config_t *config)
{
    cJSON *ui = add_container(builder, root, "ui", false);
    add_node(builder, ui, "enabled", cJSON_CreateBool(config->ui_enabled));
    add_string(builder, ui, "framebuffer", config->ui_framebuffer);
    add_string(builder, ui, "input_device", config->ui_input_device);
    add_number(builder, ui, "rotation", (int64_t) config->ui_rotation);
    add_string(builder, root, "role", config->charger ? "charger" : "bms");
    add_string(builder, root, "start_mode", config->manual_start ? "manual" : "auto");
    add_string(builder, root, "can_interface", config->can_interface);
    add_string(builder, root, "log_path", config->log_path);
    add_string(builder, root, "candump_path", config->candump_path);
#define ADD_NUMBER(member) add_number(builder, root, #member, (int64_t) config->member)
    ADD_NUMBER(version);
    ADD_NUMBER(source);
    ADD_NUMBER(peer);
    ADD_NUMBER(speed);
    ADD_NUMBER(aux_power_off_delay_ms);
    ADD_NUMBER(ready_delay_ms);
    ADD_NUMBER(ready_timeout_ms);
    ADD_NUMBER(stop_after_ms);
    ADD_NUMBER(max_session_ms);
    ADD_NUMBER(recovery_wait_ms);
    ADD_NUMBER(retries);
    ADD_NUMBER(dt_interval_ms);
    ADD_NUMBER(tp_t1_ms);
    ADD_NUMBER(tp_t2_ms);
    ADD_NUMBER(tp_t3_ms);
    ADD_NUMBER(tp_t4_ms);
    ADD_NUMBER(tp_rx_window);
#undef ADD_NUMBER
#define ADD_BOOL(member) add_node(builder, root, #member, cJSON_CreateBool(config->member))
    ADD_BOOL(software_test);
    ADD_BOOL(file_log);
    ADD_BOOL(candump);
    ADD_BOOL(self_test_pass);
    ADD_BOOL(parameters_match);
    ADD_BOOL(precharge_pass);
    ADD_BOOL(ready);
    ADD_BOOL(reconnect_allowed);
    ADD_BOOL(auto_aux_power_off);
#undef ADD_BOOL
}
static void add_messages(sim_config_builder_t *builder, cJSON *root, const sim_config_t *config)
{
    cJSON *messages = add_container(builder, root, "messages", false);
    for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT && builder->valid; idx++)
    {
        const gbt27930_descriptor_t *desc = gbt27930_descriptor((gbt27930_message_t) idx);
        cJSON *message = add_container(builder, messages, desc->name, false);
        add_number(builder, message, "period_ms", config->period_ms[idx]);
        add_number(builder, message, "first_timeout_ms", config->first_timeout_ms[idx]);
        add_number(builder, message, "timeout_ms", config->timeout_ms[idx]);
        add_node(builder, message, "enabled", cJSON_CreateBool(config->enabled[idx]));
    }
}
static void add_parameters(sim_config_builder_t *builder, cJSON *root, const sim_config_t *config)
{
    cJSON *parameters = add_container(builder, root, "parameters", false);
    for (size_t idx = 0u; gbt27930_field(idx) != NULL && builder->valid; idx++)
    {
        const gbt27930_field_t *field = gbt27930_field(idx);
        add_number(builder, parameters, field->key, config->values.field[idx]);
    }
    add_fixed_string(builder, root, "manufacturer", config->values.manufacturer, sizeof(config->values.manufacturer));
    add_fixed_string(builder, root, "vin", config->values.vin, sizeof(config->values.vin));
    add_fixed_string(builder, root, "region", config->values.region, sizeof(config->values.region));
    add_bytes(builder, root, "software_bytes", config->values.software, sizeof(config->values.software));
    add_bytes(builder, root, "reserved_bytes", config->values.reserved, sizeof(config->values.reserved));
    cJSON *cells = add_container(builder, root, "cell_voltage_mv", true);
    cJSON *groups = add_container(builder, root, "cell_group", true);
    for (size_t idx = 0u; idx < config->values.cell_count && builder->valid; idx++)
    {
        add_number(builder, cells, NULL, config->values.cells_mv[idx]);
        add_number(builder, groups, NULL, config->values.cell_groups[idx]);
    }
    cJSON *temperatures = add_container(builder, root, "temperature_degc", true);
    for (size_t idx = 0u; idx < config->values.temperature_count && builder->valid; idx++)
    {
        add_number(builder, temperatures, NULL, config->values.temperatures_degc[idx]);
    }
}
static void add_rules(sim_config_builder_t *builder, cJSON *root, const sim_config_t *config)
{
    const char *actions[] = {"drop", "delay", "duplicate", "corrupt", "length", "id"};
    cJSON *faults = add_container(builder, root, "faults", true);
    for (size_t idx = 0u; idx < config->fault_count && builder->valid; idx++)
    {
        const sim_fault_rule_t *rule = &config->faults[idx];
        cJSON *item = add_container(builder, faults, NULL, false);
        add_string(builder, item, "stage", rule->stage);
        add_string(builder, item, "message", rule->message);
        add_string(builder, item, "direction", rule->receive ? "rx" : "tx");
        add_string(builder, item, "action", actions[rule->action]);
        add_number(builder, item, "after_ms", rule->after_ms);
        add_number(builder, item, "duration_ms", rule->duration_ms);
        add_number(builder, item, "count", rule->count);
        add_number(builder, item, "delay_ms", rule->delay_ms);
        add_number(builder, item, "byte", rule->byte);
        add_number(builder, item, "value", rule->value);
    }
    cJSON *events = add_container(builder, root, "events", true);
    for (size_t idx = 0u; idx < config->event_count && builder->valid; idx++)
    {
        const sim_event_t *event = &config->events[idx];
        cJSON *item = add_container(builder, events, NULL, false);
        add_string(builder, item, "stage", event->stage);
        add_string(builder, item, "action", event->action);
        add_number(builder, item, "after_ms", event->after_ms);
        if (strcmp(event->action, "set_parameter") == 0)
        {
            add_string(builder, item, "parameter", event->parameter);
            add_number(builder, item, "value", event->value);
        }
    }
}
int sim_config_snapshot(const sim_config_t *config, sim_config_emit_cb_t emit_cb, void *cb_user)
{
    if (config == NULL || emit_cb == NULL)
    {
        return -EINVAL;
    }
    cJSON *root = cJSON_CreateObject();
    sim_config_builder_t builder = {.valid = root != NULL};
    add_root(&builder, root, config);
    add_messages(&builder, root, config);
    add_parameters(&builder, root, config);
    add_rules(&builder, root, config);
    char *json = builder.valid ? cJSON_Print(root) : NULL;
    cJSON_Delete(root);
    if (json == NULL)
    {
        return -ENOMEM;
    }
    int rc = 0;
    const char *cursor = json;
    while (*cursor != '\0' && rc == 0)
    {
        size_t len = strcspn(cursor, "\r\n");
        if (len > 512u)
        {
            len = 512u;
            /* Keep a UTF-8 code point on one log record when a long string is split. */
            while (len != 0u && ((unsigned char) cursor[len] & 0xC0u) == 0x80u)
            {
                len--;
            }
        }
        if (len != 0u)
        {
            rc = emit_cb(cursor, len, cb_user);
            cursor += len;
        }
        else
        {
            cursor++;
        }
    }
    cJSON_free(json);
    return rc;
}
