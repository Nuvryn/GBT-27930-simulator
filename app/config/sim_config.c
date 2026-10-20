/** @file sim_config.c
 * @brief Strict cJSON configuration parsing; allocation occurs only at startup.
 */
#include "config/sim_config.h"
#include "support/config_source.h"
#include "cJSON.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** Keep errors bounded and deterministic; do not echo untrusted configuration contents. */
static int sim_config_error(char *error, size_t capacity, const char *key)
{
    (void) snprintf(error, capacity, "invalid configuration key/value: %s", key != NULL ? key : "JSON");
    return -EINVAL;
}
/** Reject duplicate names recursively: cJSON otherwise accepts ambiguous objects. */
static bool sim_config_unique(const cJSON *node)
{
    if (!cJSON_IsObject(node) && !cJSON_IsArray(node))
    {
        return true;
    }
    for (const cJSON *item = node->child; item != NULL; item = item->next)
    {
        if (cJSON_IsObject(node))
        {
            for (const cJSON *other = item->next; other != NULL; other = other->next)
            {
                if (strcmp(item->string, other->string) == 0)
                {
                    return false;
                }
            }
        }
        if (!sim_config_unique(item))
        {
            return false;
        }
    }
    return true;
}
/** JSON integer values are bounded below 2^53 before conversion. */
static bool sim_config_integer(const cJSON *node, int64_t minimum, int64_t maximum, int64_t *out_value)
{
    if (!cJSON_IsNumber(node) || !isfinite(node->valuedouble) || node->valuedouble < (double) minimum ||
        node->valuedouble > (double) maximum || floor(node->valuedouble) != node->valuedouble)
    {
        return false;
    }
    *out_value = (int64_t) node->valuedouble;
    return true;
}
static bool sim_config_string(const cJSON *node, char *out, size_t capacity)
{
    if (!cJSON_IsString(node) || strlen(node->valuestring) >= capacity)
    {
        return false;
    }
    memcpy(out, node->valuestring, strlen(node->valuestring) + 1u);
    return true;
}
void sim_config_defaults(sim_config_t *out_config)
{
    memset(out_config, 0, sizeof(*out_config));
    out_config->charger = true;
    out_config->version = 2015u;
    out_config->source = 0x56u;
    out_config->peer = 0xF4u;
    out_config->speed = 1u;
    memcpy(out_config->can_interface, "can0", 5u);
    out_config->self_test_pass = true;
    out_config->parameters_match = true;
    out_config->precharge_pass = true;
    out_config->ready = true;
    out_config->reconnect_allowed = true;
    out_config->ready_delay_ms = 250u;
    out_config->ready_timeout_ms = 60000u;
    out_config->stop_after_ms = 5000u;
    out_config->max_session_ms = 180000u;
    out_config->recovery_wait_ms = 30000u;
    out_config->retries = 3u;
    out_config->dt_interval_ms = 10u;
    out_config->tp_t1_ms = 750u;
    out_config->tp_t2_ms = 1250u;
    out_config->tp_t3_ms = 1250u;
    out_config->tp_t4_ms = 1050u;
    out_config->tp_rx_window = 16u;
    gb27930_defaults(&out_config->values);
    for (unsigned idx = 0u; idx < GB27930_MESSAGE_COUNT; idx++)
    {
        const gb27930_descriptor_t *desc = gb27930_descriptor((gb27930_message_t) idx);
        out_config->period_ms[idx] = desc->period_ms;
        out_config->timeout_ms[idx] = desc->timeout_ms;
        out_config->first_timeout_ms[idx] = desc->timeout_ms;
        out_config->enabled[idx] = idx != GB27930_BMV && idx != GB27930_BMT && idx != GB27930_BSP;
    }
}
/** Read named physical values through the codec's single field registry. */
static bool sim_config_parameters(const cJSON *node, sim_config_t *config)
{
    if (!cJSON_IsObject(node))
    {
        return false;
    }
    for (const cJSON *item = node->child; item != NULL; item = item->next)
    {
        int64_t value;
        if (!sim_config_integer(item, -1000000, 4294967295LL, &value) || gb27930_set(&config->values, item->string, value) != 0)
        {
            return false;
        }
    }
    return true;
}
static bool sim_config_messages(const cJSON *node, sim_config_t *config)
{
    if (!cJSON_IsObject(node))
    {
        return false;
    }
    for (const cJSON *item = node->child; item != NULL; item = item->next)
    {
        gb27930_message_t message = gb27930_find_message(item->string);
        if (message == GB27930_MESSAGE_COUNT || !cJSON_IsObject(item))
        {
            return false;
        }
        for (const cJSON *field = item->child; field != NULL; field = field->next)
        {
            int64_t value;
            if (strcmp(field->string, "enabled") == 0 && cJSON_IsBool(field))
            {
                config->enabled[message] = cJSON_IsTrue(field);
            }
            else if (strcmp(field->string, "period_ms") == 0 && sim_config_integer(field, 1, 3600000, &value))
            {
                config->period_ms[message] = (uint32_t) value;
            }
            else if (strcmp(field->string, "timeout_ms") == 0 && sim_config_integer(field, 1, 3600000, &value))
            {
                config->timeout_ms[message] = (uint32_t) value;
            }
            else if (strcmp(field->string, "first_timeout_ms") == 0 && sim_config_integer(field, 1, 3600000, &value))
            {
                config->first_timeout_ms[message] = (uint32_t) value;
            }
            else
            {
                return false;
            }
        }
    }
    return true;
}
/** Known stage names are stable scene interfaces. */
static bool sim_config_stage(const char *stage)
{
    const char *names[] = {"*", "handshake", "identify", "config", "ready", "charging", "stopping", "statistics", "recovery", "done"};
    for (size_t idx = 0u; idx < sizeof(names) / sizeof(names[0]); idx++)
    {
        if (strcmp(stage, names[idx]) == 0)
        {
            return true;
        }
    }
    return false;
}
static bool sim_config_fault(const cJSON *node, sim_fault_rule_t *rule)
{
    if (!cJSON_IsObject(node))
    {
        return false;
    }
    memset(rule, 0, sizeof(*rule));
    memcpy(rule->stage, "*", 2u);
    rule->count = 1u;
    rule->delay_ms = 100u;
    bool has_action = false, has_message = false;
    for (const cJSON *item = node->child; item != NULL; item = item->next)
    {
        const char *key = item->string;
        int64_t value;
        if (strcmp(key, "stage") == 0)
        {
            if (!sim_config_string(item, rule->stage, sizeof(rule->stage)) || !sim_config_stage(rule->stage))
            {
                return false;
            }
        }
        else if (strcmp(key, "message") == 0)
        {
            if (!sim_config_string(item, rule->message, sizeof(rule->message)) ||
                (gb27930_find_message(rule->message) == GB27930_MESSAGE_COUNT && strcmp(rule->message, "TP_CM") != 0 && strcmp(rule->message, "TP_DT") != 0))
            {
                return false;
            }
            has_message = true;
        }
        else if (strcmp(key, "direction") == 0 && cJSON_IsString(item))
        {
            if (strcmp(item->valuestring, "rx") != 0 && strcmp(item->valuestring, "tx") != 0)
            {
                return false;
            }
            rule->receive = strcmp(item->valuestring, "rx") == 0;
        }
        else if (strcmp(key, "action") == 0 && cJSON_IsString(item))
        {
            const char *actions[] = {"drop", "delay", "duplicate", "corrupt", "length", "id"};
            bool found = false;
            for (unsigned idx = 0u; idx < 6u; idx++)
            {
                if (strcmp(item->valuestring, actions[idx]) == 0)
                {
                    rule->action = (sim_fault_action_t) idx;
                    found = true;
                }
            }
            if (!found)
            {
                return false;
            }
            has_action = true;
        }
        else if (strcmp(key, "after_ms") == 0 && sim_config_integer(item, 0, 3600000, &value))
        {
            rule->after_ms = (uint32_t) value;
        }
        else if (strcmp(key, "duration_ms") == 0 && sim_config_integer(item, 0, 3600000, &value))
        {
            rule->duration_ms = (uint32_t) value;
        }
        else if (strcmp(key, "count") == 0 && sim_config_integer(item, 0, 1000000, &value))
        {
            rule->count = (uint32_t) value;
        }
        else if (strcmp(key, "delay_ms") == 0 && sim_config_integer(item, 1, 3600000, &value))
        {
            rule->delay_ms = (uint32_t) value;
        }
        else if (strcmp(key, "byte") == 0 && sim_config_integer(item, 0, 1784, &value))
        {
            rule->byte = (uint16_t) value;
        }
        else if (strcmp(key, "value") == 0 && sim_config_integer(item, 0, 0x1FFFFFFF, &value))
        {
            rule->value = (uint32_t) value;
        }
        else
        {
            return false;
        }
    }
    if (rule->action == SIM_FAULT_LENGTH && rule->value > 8u)
    {
        return false;
    }
    if (rule->action == SIM_FAULT_CORRUPT && rule->value > 255u)
    {
        return false;
    }
    return has_action && has_message;
}
static bool sim_config_events(const cJSON *node, sim_config_t *config)
{
    if (!cJSON_IsArray(node) || cJSON_GetArraySize(node) > (int) SIM_CONFIG_EVENT_CAPACITY)
    {
        return false;
    }
    const char *actions[] = {"stop", "pause", "resume", "unplug", "replug", "set_parameter"};
    for (const cJSON *item = node->child; item != NULL; item = item->next)
    {
        if (!cJSON_IsObject(item))
        {
            return false;
        }
        sim_event_t *event = &config->events[config->event_count++];
        memcpy(event->stage, "*", 2u);
        for (const cJSON *field = item->child; field != NULL; field = field->next)
        {
            int64_t value;
            if (strcmp(field->string, "stage") == 0)
            {
                if (!sim_config_string(field, event->stage, sizeof(event->stage)) || !sim_config_stage(event->stage))
                {
                    return false;
                }
            }
            else if (strcmp(field->string, "action") == 0)
            {
                if (!sim_config_string(field, event->action, sizeof(event->action)))
                {
                    return false;
                }
            }
            else if (strcmp(field->string, "after_ms") == 0 && sim_config_integer(field, 0, 3600000, &value))
            {
                event->after_ms = (uint32_t) value;
            }
            else if (strcmp(field->string, "parameter") == 0)
            {
                if (!sim_config_string(field, event->parameter, sizeof(event->parameter)))
                {
                    return false;
                }
            }
            else if (strcmp(field->string, "value") == 0 && sim_config_integer(field, -1000000, 4294967295LL, &value))
            {
                event->value = value;
            }
            else
            {
                return false;
            }
        }
        bool known = false;
        for (size_t idx = 0u; idx < sizeof(actions) / sizeof(actions[0]); idx++)
        {
            if (strcmp(event->action, actions[idx]) == 0)
            {
                known = true;
            }
        }
        if (!known)
        {
            return false;
        }
        if (strcmp(event->action, "set_parameter") == 0)
        {
            gb27930_values_t values = config->values;
            if (gb27930_set(&values, event->parameter, event->value) != 0)
            {
                return false;
            }
        }
    }
    return true;
}
/** Table-driven scalar settings keep startup validation independent of runtime. */
static bool sim_config_scalar(const cJSON *item, sim_config_t *config)
{
    const char *key = item->string;
    int64_t value;
#define SIM_CONFIG_BOOL(name, member)        \
    if (strcmp(key, name) == 0)              \
    {                                        \
        if (!cJSON_IsBool(item))             \
        {                                    \
            return false;                    \
        }                                    \
        config->member = cJSON_IsTrue(item); \
        return true;                         \
    }
    SIM_CONFIG_BOOL("software_test", software_test)
    SIM_CONFIG_BOOL("file_log", file_log)
    SIM_CONFIG_BOOL("self_test_pass", self_test_pass)
    SIM_CONFIG_BOOL("parameters_match", parameters_match)
    SIM_CONFIG_BOOL("precharge_pass", precharge_pass)
    SIM_CONFIG_BOOL("ready", ready)
    SIM_CONFIG_BOOL("reconnect_allowed", reconnect_allowed)
#undef SIM_CONFIG_BOOL
#define SIM_CONFIG_NUMBER(name, member, min, max)        \
    if (strcmp(key, name) == 0)                          \
    {                                                    \
        if (!sim_config_integer(item, min, max, &value)) \
        {                                                \
            return false;                                \
        }                                                \
        config->member = (uint32_t) value;               \
        return true;                                     \
    }
    SIM_CONFIG_NUMBER("speed", speed, 1, 100)
    SIM_CONFIG_NUMBER("ready_delay_ms", ready_delay_ms, 0, 3600000)
    SIM_CONFIG_NUMBER("ready_timeout_ms", ready_timeout_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("stop_after_ms", stop_after_ms, 0, 3600000)
    SIM_CONFIG_NUMBER("max_session_ms", max_session_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("recovery_wait_ms", recovery_wait_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("retries", retries, 0, 100)
    SIM_CONFIG_NUMBER("dt_interval_ms", dt_interval_ms, 1, 1000)
    SIM_CONFIG_NUMBER("tp_t1_ms", tp_t1_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("tp_t2_ms", tp_t2_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("tp_t3_ms", tp_t3_ms, 1, 3600000)
    SIM_CONFIG_NUMBER("tp_t4_ms", tp_t4_ms, 1, 3600000)
#undef SIM_CONFIG_NUMBER
    if (strcmp(key, "tp_rx_window") == 0 && sim_config_integer(item, 1, 255, &value))
    {
        config->tp_rx_window = (uint8_t) value;
        return true;
    }
    if (strcmp(key, "can_interface") == 0)
    {
        return sim_config_string(item, config->can_interface, sizeof(config->can_interface)) && config->can_interface[0] != '\0';
    }
    if (strcmp(key, "log_path") == 0)
    {
        return sim_config_string(item, config->log_path, sizeof(config->log_path));
    }
    if (strcmp(key, "source") == 0 && sim_config_integer(item, 0, 253, &value))
    {
        config->source = (uint8_t) value;
        return true;
    }
    if (strcmp(key, "peer") == 0 && sim_config_integer(item, 0, 253, &value))
    {
        config->peer = (uint8_t) value;
        return true;
    }
    return false;
}
static bool sim_config_arrays(const cJSON *item, sim_config_t *config)
{
    if (!cJSON_IsArray(item))
    {
        return false;
    }
    int count = cJSON_GetArraySize(item);
    size_t idx = 0u;
    if (strcmp(item->string, "cell_group") == 0 && count > 0 && count <= 256)
    {
        config->cell_group_count = (size_t) count;
        for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
        {
            int64_t value;
            if (!sim_config_integer(entry, 0, 15, &value))
            {
                return false;
            }
            config->values.cell_groups[idx++] = (uint8_t) value;
        }
        return true;
    }
    if (strcmp(item->string, "cell_voltage_mv") == 0 && count > 0 && count <= 256)
    {
        config->values.cell_count = (size_t) count;
        for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
        {
            int64_t value;
            if (!sim_config_integer(entry, 0, 24000, &value) || value % 10 != 0)
            {
                return false;
            }
            config->values.cells_mv[idx++] = (uint16_t) value;
        }
        return true;
    }
    if (strcmp(item->string, "temperature_degc") == 0 && count > 0 && count <= 128)
    {
        config->values.temperature_count = (size_t) count;
        for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
        {
            int64_t value;
            if (!sim_config_integer(entry, -50, 200, &value))
            {
                return false;
            }
            config->values.temperatures_degc[idx++] = (int16_t) value;
        }
        return true;
    }
    if (strcmp(item->string, "reserved_bytes") == 0 && count == 16)
    {
        for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
        {
            int64_t value;
            if (!sim_config_integer(entry, 0, 255, &value))
            {
                return false;
            }
            config->values.reserved[idx++] = (uint8_t) value;
        }
        return true;
    }
    if (strcmp(item->string, "software_bytes") == 0 && count == 8)
    {
        for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
        {
            int64_t value;
            if (!sim_config_integer(entry, 0, 255, &value))
            {
                return false;
            }
            config->values.software[idx++] = (uint8_t) value;
        }
        return true;
    }
    return false;
}
int sim_config_parse(const char *text, sim_config_t *out_config, char *out_error, size_t error_capacity)
{
    if (text == NULL || out_config == NULL || out_error == NULL || error_capacity == 0u)
    {
        return -EINVAL;
    }
    out_error[0] = '\0';
    if (strlen(text) > 65536u || strstr(text, "\\u0000") != NULL)
    {
        return sim_config_error(out_error, error_capacity, "size/NUL");
    }
    sim_config_t config;
    sim_config_defaults(&config);
    cJSON *root = cJSON_ParseWithOpts(text, NULL, true);
    if (!cJSON_IsObject(root) || !sim_config_unique(root))
    {
        cJSON_Delete(root);
        return sim_config_error(out_error, error_capacity, "JSON/duplicates");
    }
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(root, "role");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    int64_t year;
    if (!cJSON_IsString(role) || (strcmp(role->valuestring, "charger") != 0 && strcmp(role->valuestring, "bms") != 0) ||
        !sim_config_integer(version, 2015, 2023, &year) || (year != 2015 && year != 2023))
    {
        cJSON_Delete(root);
        return sim_config_error(out_error, error_capacity, "role/version");
    }
    config.charger = strcmp(role->valuestring, "charger") == 0;
    config.version = (unsigned) year;
    config.reconnect_allowed = config.version == 2015u;
    config.source = config.charger ? 0x56u : 0xF4u;
    config.peer = config.charger ? 0xF4u : 0x56u;
    int rc = 0;
    for (const cJSON *item = root->child; item != NULL; item = item->next)
    {
        const char *key = item->string;
        bool valid = false;
        if (strcmp(key, "role") == 0 || strcmp(key, "version") == 0)
        {
            continue;
        }
        if (strcmp(key, "parameters") == 0)
        {
            valid = sim_config_parameters(item, &config);
        }
        else if (strcmp(key, "messages") == 0)
        {
            valid = sim_config_messages(item, &config);
        }
        else if (strcmp(key, "events") == 0)
        {
            valid = sim_config_events(item, &config);
        }
        else if (strcmp(key, "faults") == 0 && cJSON_IsArray(item) && cJSON_GetArraySize(item) <= (int) SIM_CONFIG_RULE_CAPACITY)
        {
            valid = true;
            for (const cJSON *entry = item->child; entry != NULL; entry = entry->next)
            {
                if (!sim_config_fault(entry, &config.faults[config.fault_count++]))
                {
                    valid = false;
                    break;
                }
            }
        }
        else if (strcmp(key, "cell_voltage_mv") == 0 || strcmp(key, "temperature_degc") == 0 || strcmp(key, "reserved_bytes") == 0 || strcmp(key, "software_bytes") == 0 || strcmp(key, "cell_group") == 0)
        {
            valid = sim_config_arrays(item, &config);
        }
        else if (strcmp(key, "vin") == 0 && cJSON_IsString(item) && strlen(item->valuestring) == 17u)
        {
            memcpy(config.values.vin, item->valuestring, 17u);
            valid = true;
        }
        else if (strcmp(key, "manufacturer") == 0 && cJSON_IsString(item) && strlen(item->valuestring) == 4u)
        {
            memcpy(config.values.manufacturer, item->valuestring, 4u);
            valid = true;
        }
        else if (strcmp(key, "region") == 0 && cJSON_IsString(item) && strlen(item->valuestring) == 3u)
        {
            memcpy(config.values.region, item->valuestring, 3u);
            valid = true;
        }
        else
        {
            valid = sim_config_scalar(item, &config);
        }
        if (!valid)
        {
            rc = sim_config_error(out_error, error_capacity, key);
            break;
        }
    }
    if (rc == 0 && (config.source == config.peer || (config.speed > 1u && !config.software_test) || (config.file_log && config.log_path[0] == '\0') ||
                       gb27930_get(&config.values, "cml_min_voltage_mv") > gb27930_get(&config.values, "cml_max_voltage_mv") ||
                       gb27930_get(&config.values, "cml_min_current_ma") > gb27930_get(&config.values, "cml_max_current_ma")))
    {
        rc = sim_config_error(out_error, error_capacity, "cross-field constraints");
    }
    cJSON_Delete(root);
    if (rc == 0 && config.cell_group_count != 0u && config.cell_group_count != config.values.cell_count)
    {
        rc = sim_config_error(out_error, error_capacity, "cell_group length");
    }
    if (rc == 0)
    {
        *out_config = config;
    }
    return rc;
}
int sim_config_load(const char *path, sim_config_t *out_config, char *out_error, size_t error_capacity)
{
    config_source_t source = {0};
    if (config_source_load(&source, path) != 0)
    {
        return sim_config_error(out_error, error_capacity, "config source");
    }
    int rc = sim_config_parse(source.text, out_config, out_error, error_capacity);
    config_source_free(&source);
    return rc;
}
