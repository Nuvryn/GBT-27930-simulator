/** @file sim_ui_view.c @brief Five English touch pages; flow follows structured engine state. */
#include "ui/sim_ui_view.h"
#include "simulator/sim_build_info.h"
#include "osal/osal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
struct sim_ui_view_s
{
    sim_control_t *control;
    sim_control_view_t data;
    lv_obj_t *root, *tabs, *summary, *wait, *path, *measurements, *banner, *feedback, *control_state, *save_state;
    lv_obj_t *nodes[SIM_STAGE_IDLE], *node_text[SIM_STAGE_IDLE];
    lv_obj_t *start, *stop, *apply, *save, *enable, *disable, *keyboard, *parameters;
    lv_obj_t *fields[GBT27930_FIELD_CAPACITY];
    lv_obj_t *message, *stage, *direction, *action, *count, *duration, *delay, *byte, *value;
    lv_obj_t *logs, *filter, *pause, *log_state;
    bool dirty, paused;
    unsigned apply_request, stop_sequence;
    uint32_t log_cursor, log_start;
    char log_text[32768], config_path[256];
};
static lv_obj_t *label(lv_obj_t *parent, const char *text)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_WRAP);
    return obj;
}
static void feedback(sim_ui_view_t *view, const char *operation, int rc)
{
    char text[160];
    (void) snprintf(text, sizeof(text), "%s: %s (code %d)", operation, rc == 0 ? "Accepted" : (rc == -EBUSY ? "Busy - try again" : "Rejected"), rc);
    lv_label_set_text(view->feedback, text);
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_event_cb_t callback, sim_ui_view_t *view)
{
    lv_obj_t *obj = lv_button_create(parent);
    lv_obj_set_height(obj, 44);
    lv_obj_set_width(obj, LV_SIZE_CONTENT);
    lv_obj_t *caption = lv_label_create(obj);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, view);
    return obj;
}
static void disabled(lv_obj_t *obj, bool disable)
{
    if (disable)
    {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    }
}
static sim_control_request_t request_for(sim_ui_view_t *view, sim_control_kind_t kind)
{
    sim_control_request_t request = {.kind = kind, .session_id = view->data.status.session_id};
    return request;
}
static void start_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    if (view->dirty)
    {
        lv_label_set_text(view->feedback, "Apply parameter edits before starting");
        return;
    }
    sim_control_request_t request = request_for(view, SIM_CONTROL_START);
    feedback(view, "Start queued", sim_control_submit(view->control, &request));
}
static void stop_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_stop(view->control, view->data.status.session_id);
    lv_label_set_text(view->feedback, "Stop requested; waiting for protocol completion");
}
static void edited(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    view->dirty = true;
}
static void focused(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    lv_keyboard_set_mode(view->keyboard, lv_event_get_target_obj(event) == view->filter ? LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(view->keyboard, lv_event_get_target_obj(event));
    lv_obj_set_hidden(view->keyboard, false);
    lv_obj_scroll_to_view(lv_event_get_target_obj(event), LV_ANIM_OFF);
}
static void keyboard_done(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    lv_keyboard_set_textarea(view->keyboard, NULL);
    lv_obj_set_hidden(view->keyboard, true);
}
static void apply_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_request_t request = request_for(view, SIM_CONTROL_APPLY);
    request.values = view->data.config.values;
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        if (view->fields[idx] == NULL)
        {
            continue;
        }
        int64_t value;
        int rc = sim_control_parse_value(gbt27930_field(idx), lv_textarea_get_text(view->fields[idx]), &value);
        if (rc != 0)
        {
            char text[160];
            (void) snprintf(text, sizeof(text), "Invalid value: %s (code %d)", gbt27930_field(idx)->key, rc);
            lv_label_set_text(view->feedback, text);
            return;
        }
        request.values.field[idx] = value;
    }
    int rc = sim_control_submit(view->control, &request);
    if (rc == 0)
    {
        view->apply_request = request.id;
    }
    feedback(view, "Apply queued", rc);
}
static void save_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    if (view->dirty || view->apply_request != 0u)
    {
        lv_label_set_text(view->feedback, "Apply parameter edits before saving");
        return;
    }
    feedback(view, "Save queued", sim_control_save(view->control));
}
static lv_obj_t *input(lv_obj_t *parent, const char *caption, const char *initial, sim_ui_view_t *view, bool parameter)
{
    label(parent, caption);
    lv_obj_t *obj = lv_textarea_create(parent);
    lv_textarea_set_one_line(obj, true);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_textarea_set_max_length(obj, 24u);
    lv_textarea_set_accepted_chars(obj, "-0123456789.");
    lv_textarea_set_text(obj, initial);
    lv_obj_add_event_cb(obj, focused, LV_EVENT_FOCUSED, view);
    if (parameter)
    {
        lv_obj_add_event_cb(obj, edited, LV_EVENT_VALUE_CHANGED, view);
    }
    return obj;
}
static lv_obj_t *dropdown(lv_obj_t *parent, const char *caption, const char *options)
{
    label(parent, caption);
    lv_obj_t *obj = lv_dropdown_create(parent);
    lv_dropdown_set_options(obj, options);
    lv_obj_set_width(obj, LV_PCT(100));
    return obj;
}
static int unsigned_input(lv_obj_t *obj, uint32_t *value)
{
    const char *text = lv_textarea_get_text(obj);
    if (*text < '0' || *text > '9')
    {
        return -EINVAL;
    }
    uint64_t result = 0u;
    while (*text >= '0' && *text <= '9')
    {
        result = result * 10u + (uint64_t) (*text++ - '0');
        if (result > UINT32_MAX)
        {
            return -ERANGE;
        }
    }
    if (*text != '\0')
    {
        return -EINVAL;
    }
    *value = (uint32_t) result;
    return 0;
}
static void fault_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_request_t request = request_for(view, SIM_CONTROL_FAULT);
    sim_fault_rule_t *rule = &request.fault;
    lv_dropdown_get_selected_str(view->message, rule->message, sizeof(rule->message));
    lv_dropdown_get_selected_str(view->stage, rule->stage, sizeof(rule->stage));
    rule->receive = lv_dropdown_get_selected(view->direction) == 1u;
    rule->action = (sim_fault_action_t) lv_dropdown_get_selected(view->action);
    uint32_t byte;
    if (unsigned_input(view->count, &rule->count) != 0 || unsigned_input(view->duration, &rule->duration_ms) != 0 ||
        unsigned_input(view->delay, &rule->delay_ms) != 0 || unsigned_input(view->byte, &byte) != 0 || byte > 1784u || unsigned_input(view->value, &rule->value) != 0)
    {
        feedback(view, "Fault values", -EINVAL);
        return;
    }
    rule->byte = (uint16_t) byte;
    int rc = sim_config_validate_fault(rule);
    if (rc == 0)
    {
        rc = sim_control_submit(view->control, &request);
    }
    feedback(view, "Enable fault queued", rc);
}
static void cancel_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_request_t request = request_for(view, SIM_CONTROL_CANCEL);
    feedback(view, "Cancel fault queued", sim_control_submit(view->control, &request));
}
static void pause_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    view->paused = !view->paused;
    lv_label_set_text(lv_obj_get_child(view->pause, 0), view->paused ? "Resume display" : "Pause display");
}
static void clear_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    view->log_start = view->data.log_sequence;
    view->log_cursor = view->log_start;
    view->log_text[0] = '\0';
    lv_label_set_text(view->logs, "");
}
static void filter_changed(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    view->log_cursor = view->data.log_sequence > SIM_UI_LOG_LINES ? view->data.log_sequence - SIM_UI_LOG_LINES : 0u;
    if (view->log_cursor < view->log_start)
    {
        view->log_cursor = view->log_start;
    }
    view->log_text[0] = '\0';
    lv_label_set_text(view->logs, "");
}
static lv_obj_t *page(lv_obj_t *tabs, const char *name)
{
    lv_obj_t *obj = lv_tabview_add_tab(tabs, name);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(obj, 12, 0);
    lv_obj_set_style_pad_row(obj, 10, 0);
    return obj;
}
sim_ui_view_t *sim_ui_view_create(sim_control_t *control, const char *config_path)
{
    sim_ui_view_t *view = osal_malloc(sizeof(*view));
    if (view == NULL)
    {
        return NULL;
    }
    memset(view, 0, sizeof(*view));
    view->control = control;
    if (sim_control_read(control, &view->data) != 0)
    {
        osal_free(view);
        return NULL;
    }
    if (config_path != NULL)
    {
        (void) snprintf(view->config_path, sizeof(view->config_path), "%s", config_path);
    }
    view->root = lv_obj_create(NULL);
    lv_obj_set_flex_flow(view->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(view->root, lv_color_hex(0x101C2D), 0);
    lv_obj_set_style_text_color(view->root, lv_color_hex(0xE6EDF7), 0);
    lv_obj_set_style_pad_all(view->root, 8, 0);
    lv_obj_t *heading = label(view->root, "GB/T 27930 Simulator");
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_18, 0);
    view->banner = label(view->root, "Manual fault: OFF");
    view->feedback = label(view->root, "Ready");
    view->tabs = lv_tabview_create(view->root);
    lv_tabview_set_tab_bar_size(view->tabs, 44);
    lv_obj_set_width(view->tabs, LV_PCT(100));
    lv_obj_set_flex_grow(view->tabs, 1);
    lv_obj_t *home = page(view->tabs, "Home");
    view->summary = label(home, "Idle");
    lv_obj_t *flow = lv_obj_create(home);
    lv_obj_set_width(flow, LV_PCT(100));
    lv_obj_set_height(flow, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(flow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(flow, 8, 0);
    for (unsigned idx = 0u; idx < SIM_STAGE_IDLE; idx++)
    {
        view->nodes[idx] = lv_obj_create(flow);
        lv_obj_set_width(view->nodes[idx], LV_PCT(30));
        lv_obj_set_style_min_width(view->nodes[idx], 140, 0);
        lv_obj_set_height(view->nodes[idx], 64);
        lv_obj_set_style_pad_all(view->nodes[idx], 7, 0);
        lv_obj_set_flex_grow(view->nodes[idx], 1);
        view->node_text[idx] = label(view->nodes[idx], sim_stage_name((sim_stage_t) idx));
    }
    view->path = label(home, "Actual path: idle");
    view->wait = label(home, "Waiting for start");
    view->measurements = label(home, "Peer measurements: unavailable");
    view->parameters = page(view->tabs, "Parameters");
    view->apply = button(view->parameters, "Apply for next session", apply_clicked, view);
    view->save = button(view->parameters, "Save configuration", save_clicked, view);
    view->save_state = label(view->parameters, "Applied values are separate from unsaved edits");
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        const gbt27930_field_t *field = gbt27930_field(idx);
        if (gbt27930_descriptor(field->message)->charger != view->data.config.charger)
        {
            continue;
        }
        char initial[40], caption[240], minimum[40], maximum[40], step[40];
        sim_control_format_value(field, view->data.config.values.field[idx], initial, sizeof(initial));
        sim_control_format_value(field, field->minimum, minimum, sizeof(minimum));
        sim_control_format_value(field, field->maximum, maximum, sizeof(maximum));
        sim_control_format_value(field, field->scale < 0 ? -(int64_t) field->scale : field->scale, step, sizeof(step));
        const char *unit = (strlen(field->key) >= 3u && strcmp(field->key + strlen(field->key) - 3u, "_mv") == 0) ? "V" : ((strlen(field->key) >= 3u && strcmp(field->key + strlen(field->key) - 3u, "_ma") == 0) ? "A" : "physical units in key");
        (void) snprintf(caption, sizeof(caption), "%s / %s (%s) [%s..%s], step %s", gbt27930_descriptor(field->message)->name, field->key, unit, minimum, maximum, step);
        view->fields[idx] = input(view->parameters, caption, initial, view, true);
    }
    lv_obj_t *controls = page(view->tabs, "Control");
    view->control_state = label(controls, "Waiting");
    view->start = button(controls, "Start charging", start_clicked, view);
    view->stop = button(controls, "Stop charging", stop_clicked, view);
    label(controls, view->data.config.charger ? "Start sends CHM. Completion returns to idle." : "BMS starts on a valid peer CHM. No local start is required.");
    lv_obj_t *faults = page(view->tabs, "Faults");
    char options[256] = "";
    for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT; idx++)
    {
        if (idx != 0u)
        {
            (void) strcat(options, "\n");
        }
        (void) strcat(options, gbt27930_descriptor((gbt27930_message_t) idx)->name);
    }
    (void) strcat(options, "\nTP_CM\nTP_DT");
    view->enable = button(faults, "Enable manual fault", fault_clicked, view);
    view->disable = button(faults, "Disable manual fault", cancel_clicked, view);
    view->message = dropdown(faults, "Message", options);
    view->stage = dropdown(faults, "Stage", "*\nhandshake\nidentify\nconfig\nready\ncharging\nstopping\nstatistics\nrecovery\ndone");
    view->direction = dropdown(faults, "Direction", "tx\nrx");
    view->action = dropdown(faults, "Action", "drop\ndelay\nduplicate\ncorrupt byte\nlength\nCAN id");
    view->count = input(faults, "Count (0 = unlimited)", "1", view, false);
    view->duration = input(faults, "Duration after enable (ms, 0 = unlimited)", "0", view, false);
    view->delay = input(faults, "Delay (ms)", "100", view, false);
    view->byte = input(faults, "Byte offset (TP_DT sequence = 0)", "0", view, false);
    view->value = input(faults, "Replacement value (decimal)", "0", view, false);
    lv_obj_t *logs = page(view->tabs, "Logs / Info");
    const sim_build_info_t *info = sim_build_info_get();
    char text[768];
    (void) snprintf(text, sizeof(text), "Version: %s | Build: %s | Target: %s\nConfig: %s\nFile log: %s | candump: %s", info->version, info->time_utc, info->target, view->config_path[0] != '\0' ? view->config_path : "No writable source", view->data.config.file_log ? "ON" : "OFF", view->data.config.candump ? "ON" : "OFF");
    label(logs, text);
    view->pause = button(logs, "Pause display", pause_clicked, view);
    button(logs, "Clear display", clear_clicked, view);
    label(logs, "Filter by tag or text");
    view->filter = lv_textarea_create(logs);
    lv_textarea_set_one_line(view->filter, true);
    lv_obj_set_width(view->filter, LV_PCT(100));
    lv_textarea_set_max_length(view->filter, 64u);
    lv_obj_add_event_cb(view->filter, filter_changed, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(view->filter, focused, LV_EVENT_FOCUSED, view);
    view->log_state = label(logs, "");
    view->logs = label(logs, "");
    view->keyboard = lv_keyboard_create(view->root);
    lv_keyboard_set_mode(view->keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_height(view->keyboard, LV_PCT(40));
    lv_obj_add_event_cb(view->keyboard, keyboard_done, LV_EVENT_READY, view);
    lv_obj_add_event_cb(view->keyboard, keyboard_done, LV_EVENT_CANCEL, view);
    lv_obj_set_hidden(view->keyboard, true);
    lv_screen_load(view->root);
    sim_ui_view_refresh(view);
    return view;
}
static void append_text(char *destination, size_t capacity, const char *text)
{
    size_t used = strlen(destination);
    if (used < capacity)
    {
        (void) snprintf(destination + used, capacity - used, "%s", text);
    }
}
void sim_ui_view_refresh(sim_ui_view_t *view)
{
    if (sim_control_read(view->control, &view->data) != 0)
    {
        return;
    }
    sim_status_t *status = &view->data.status;
    sim_control_result_t result;
    while (sim_control_result(view->control, &result) == 1)
    {
        if (result.id == view->apply_request)
        {
            if (result.result == 0)
            {
                view->dirty = false;
            }
            view->apply_request = 0u;
        }
        char message[128];
        (void) snprintf(message, sizeof(message), "Request %u completed: %s (code %d)", result.id, result.result == 0 ? "OK" : "Rejected", result.result);
        lv_label_set_text(view->feedback, message);
    }
    if (view->stop_sequence != view->data.stop_sequence)
    {
        view->stop_sequence = view->data.stop_sequence;
        feedback(view, "Stop request", view->data.stop_result);
    }
    char text[2048];
    (void) snprintf(text, sizeof(text), "%s | %u | %s\n%s%s | Session %u | %.1f s | %s %.1f s | Revision %u | Last result %d", view->data.config.charger ? "Charger" : "BMS", view->data.config.version, view->data.config.can_interface, sim_stage_name(status->stage), status->paused ? " (paused)" : "", status->session_id, (double) status->elapsed_ms / 1000.0, status->active ? "Stage" : "Last stage", (double) status->stage_elapsed_ms / 1000.0, status->revision, status->result);
    lv_label_set_text(view->summary, text);
    lv_label_set_text(view->control_state, text);
    (void) snprintf(text, sizeof(text), "Manual fault: %s | Hits: %u | Request %u result %d", status->fault_enabled ? (status->active ? "ACTIVE" : "ARMED for next session") : "OFF", status->fault_hits, view->data.request_id, view->data.request_result);
    lv_label_set_text(view->banner, text);
    lv_obj_set_style_text_color(view->banner, lv_color_hex(status->fault_enabled ? 0xFFBC52 : 0x8BDAC2), 0);
    bool visited[SIM_STAGE_IDLE] = {false};
    unsigned retained = status->history_count < SIM_STAGE_HISTORY ? status->history_count : SIM_STAGE_HISTORY;
    for (unsigned idx = 0u; idx < retained; idx++)
    {
        const sim_transition_t *step = &status->history[(status->history_count - retained + idx) % SIM_STAGE_HISTORY];
        if ((unsigned) step->stage < SIM_STAGE_IDLE)
        {
            visited[step->stage] = true;
        }
    }
    for (unsigned idx = 0u; idx < SIM_STAGE_IDLE; idx++)
    {
        bool current = status->active && (unsigned) status->stage == idx;
        bool error = status->failed && (unsigned) status->error_stage == idx;
        const char *state = error ? "Error" : (current ? "Current" : (visited[idx] ? "Visited" : "Ahead"));
        (void) snprintf(text, sizeof(text), "%s\n%s", sim_stage_name((sim_stage_t) idx), state);
        lv_label_set_text(view->node_text[idx], text);
        lv_obj_set_style_bg_color(view->nodes[idx], lv_color_hex(error ? 0xF5B8B8 : (current ? 0xA9D6FF : (visited[idx] ? 0xBEE3D3 : 0xE4E8EF))), 0);
    }
    (void) snprintf(text, sizeof(text), "Actual path: ");
    unsigned path_count = retained < 12u ? retained : 12u;
    for (unsigned idx = 0u; idx < path_count; idx++)
    {
        const sim_transition_t *step = &status->history[(status->history_count - path_count + idx) % SIM_STAGE_HISTORY];
        if (idx != 0u)
        {
            append_text(text, sizeof(text), " > ");
        }
        append_text(text, sizeof(text), sim_stage_name(step->stage));
    }
    if (!status->active)
    {
        append_text(text, sizeof(text), " > idle");
    }
    lv_label_set_text(view->path, text);
    (void) snprintf(text, sizeof(text), "Waiting / monitoring: ");
    for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT; idx++)
    {
        if ((status->waiting_messages & (1u << idx)) != 0u)
        {
            append_text(text, sizeof(text), gbt27930_descriptor((gbt27930_message_t) idx)->name);
            append_text(text, sizeof(text), " ");
        }
    }
    const char *conditions[] = {"self-test ", "precharge ", "local ready ", "peer ready ", "local ready sent ", "BCP update completion ", "simulated auxiliary power off ", "recovery delay "};
    for (unsigned idx = 0u; idx < 8u; idx++)
    {
        if ((status->conditions & (1u << idx)) != 0u)
        {
            append_text(text, sizeof(text), conditions[idx]);
        }
    }
    if (!status->active)
    {
        append_text(text, sizeof(text), view->data.config.charger ? "Enter a new session with Start" : "peer CHM");
    }
    if (status->error[0] != '\0')
    {
        append_text(text, sizeof(text), "\nLast error: ");
        append_text(text, sizeof(text), status->error);
        if (status->error_message < GBT27930_MESSAGE_COUNT)
        {
            append_text(text, sizeof(text), " / ");
            append_text(text, sizeof(text), gbt27930_descriptor(status->error_message)->name);
        }
    }
    if (status->history_count > SIM_STAGE_HISTORY)
    {
        append_text(text, sizeof(text), "\nOlder transitions evicted (history gap)");
    }
    lv_label_set_text(view->wait, text);
    const gbt27930_values_t *local = status->active ? &status->local : &view->data.config.values;
    (void) snprintf(text, sizeof(text), "Local simulated: %s voltage %.1f V | current %.1f A\n", view->data.config.charger ? "capability" : "demand", (double) gbt27930_get(local, view->data.config.charger ? "cml_max_voltage_mv" : "bcl_voltage_mv") / 1000.0, (double) gbt27930_get(local, view->data.config.charger ? "cml_max_current_ma" : "bcl_current_ma") / 1000.0);
    gbt27930_message_t measurement = view->data.config.charger ? GBT27930_BCL : GBT27930_CCS;
    if (status->active && (status->peer_valid_messages & (1u << measurement)) != 0u)
    {
        char peer[160];
        (void) snprintf(peer, sizeof(peer), "Peer reported: %.1f V | %.1f A", (double) gbt27930_get(&status->peer, view->data.config.charger ? "bcl_voltage_mv" : "ccs_voltage_mv") / 1000.0, (double) gbt27930_get(&status->peer, view->data.config.charger ? "bcl_current_ma" : "ccs_current_ma") / 1000.0);
        append_text(text, sizeof(text), peer);
    }
    else
    {
        append_text(text, sizeof(text), "Peer reported: unavailable / expired");
    }
    if (view->data.config.charger && status->active && (status->peer_valid_messages & (1u << GBT27930_BCS)) != 0u)
    {
        char soc[64];
        (void) snprintf(soc, sizeof(soc), " | Peer SOC %" PRId64 "%%", gbt27930_get(&status->peer, "bcs_soc_pct"));
        append_text(text, sizeof(text), soc);
    }
    lv_label_set_text(view->measurements, text);
    disabled(view->start, status->active || !view->data.config.charger || view->apply_request != 0u);
    disabled(view->stop, !status->active);
    disabled(view->apply, status->active || view->apply_request != 0u);
    disabled(view->save, view->config_path[0] == '\0' || view->data.saving || view->dirty || view->apply_request != 0u);
    disabled(view->enable, false);
    disabled(view->disable, false);
    for (size_t idx = 0u; idx < GBT27930_FIELD_CAPACITY; idx++)
    {
        if (view->fields[idx] != NULL)
        {
            disabled(view->fields[idx], status->active || view->apply_request != 0u);
        }
    }
    (void) snprintf(text, sizeof(text), "Edits: %s | Applied revision %u | Saved revision %u | Save: %s (code %d)", view->dirty ? "not applied" : "applied", status->revision, view->data.saved_revision, view->data.saving ? "working" : (status->revision == view->data.saved_revision ? "saved" : "unsaved"), view->data.save_result);
    lv_label_set_text(view->save_state, text);
    (void) snprintf(text, sizeof(text), "UI log evictions / skips: %u | %s", view->data.log_dropped, view->paused ? "paused" : "live");
    lv_label_set_text(view->log_state, text);
    if (!view->paused)
    {
        char line[SIM_UI_LOG_LINE_SIZE];
        const char *filter = lv_textarea_get_text(view->filter);
        bool changed = false;
        for (unsigned idx = 0u; idx < 32u && sim_control_log_read(view->control, &view->log_cursor, line, sizeof(line)) == 1; idx++)
        {
            if (filter[0] != '\0' && strstr(line, filter) == NULL)
            {
                continue;
            }
            size_t used = strlen(view->log_text), add = strlen(line) + 1u;
            while (used + add >= sizeof(view->log_text))
            {
                char *newline = strchr(view->log_text, '\n');
                if (newline == NULL)
                {
                    view->log_text[0] = '\0';
                    break;
                }
                memmove(view->log_text, newline + 1, strlen(newline + 1) + 1u);
                used = strlen(view->log_text);
            }
            append_text(view->log_text, sizeof(view->log_text), line);
            append_text(view->log_text, sizeof(view->log_text), "\n");
            changed = true;
        }
        if (changed)
        {
            lv_label_set_text(view->logs, view->log_text);
        }
    }
}
void sim_ui_view_destroy(sim_ui_view_t *view)
{
    if (view != NULL)
    {
        lv_obj_delete(view->root);
        osal_free(view);
    }
}
lv_obj_t *sim_ui_view_widget(sim_ui_view_t *view, const char *name)
{
    if (strcmp(name, "tabs") == 0)
    {
        return view->tabs;
    }
    if (strcmp(name, "fault_message") == 0)
    {
        return view->message;
    }
    if (strcmp(name, "start") == 0)
    {
        return view->start;
    }
    if (strcmp(name, "stop") == 0)
    {
        return view->stop;
    }
    if (strcmp(name, "apply") == 0)
    {
        return view->apply;
    }
    if (strcmp(name, "save") == 0)
    {
        return view->save;
    }
    if (strcmp(name, "enable") == 0)
    {
        return view->enable;
    }
    if (strcmp(name, "disable") == 0)
    {
        return view->disable;
    }
    if (strcmp(name, "summary") == 0)
    {
        return view->summary;
    }
    if (strcmp(name, "waiting") == 0)
    {
        return view->wait;
    }
    if (strcmp(name, "logs") == 0)
    {
        return view->logs;
    }
    if (strcmp(name, "parameters") == 0)
    {
        return view->parameters;
    }
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        if (strcmp(name, gbt27930_field(idx)->key) == 0)
        {
            return view->fields[idx];
        }
    }
    return NULL;
}
