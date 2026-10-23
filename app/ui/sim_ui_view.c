/** @file sim_ui_view.c @brief Five Chinese touch pages; flow follows structured engine state. */
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
    lv_obj_t *detail_panel, *detail_title, *detail_choice, *detail_text, *detail_close;
    sim_stage_t detail_stage;
    uint32_t detail_record;
    unsigned detail_session;
    bool dirty, paused;
    unsigned apply_request, stop_sequence;
    uint32_t log_cursor, log_start;
    char log_text[32768], config_path[256];
};
static const char *ui_stage_name(sim_stage_t stage)
{
    static const char *const names[] = {"握手", "辨识", "参数配置", "准备就绪", "充电", "中止", "统计", "异常恢复", "结束", "待机"};
    return (unsigned) stage <= SIM_STAGE_IDLE ? names[stage] : "未知阶段";
}
static const char *ui_field_name(const char *key)
{
    static const struct
    {
        const char *key;
        const char *name;
    } fields[] = {
        {"bhm_max_voltage_mv", "最高允许充电电压"},
        {"crm_result", "辨识结果"},
        {"charger_id", "充电机编号"},
        {"battery_type", "电池类型"},
        {"battery_capacity_mah", "额定容量"},
        {"battery_rated_voltage_mv", "额定总电压"},
        {"battery_serial", "电池组序号"},
        {"battery_year", "生产年份"},
        {"battery_month", "生产月份"},
        {"battery_day", "生产日期"},
        {"battery_charge_count", "累计充电次数"},
        {"battery_owner", "产权标识"},
        {"bcp_cell_max_voltage_mv", "单体最高允许电压"},
        {"bcp_max_current_ma", "最大允许充电电流"},
        {"battery_energy_wh", "标称总能量"},
        {"bcp_max_voltage_mv", "最高允许总电压"},
        {"bcp_max_temperature_degc", "最高允许温度"},
        {"bcp_soc_permille", "当前荷电状态"},
        {"bcp_voltage_mv", "当前总电压"},
        {"cml_max_voltage_mv", "最高输出电压"},
        {"cml_min_voltage_mv", "最低输出电压"},
        {"cml_max_current_ma", "最大输出电流"},
        {"cml_min_current_ma", "最小输出电流"},
        {"bro_ready", "BMS 就绪状态"},
        {"cro_ready", "充电机就绪状态"},
        {"bcl_voltage_mv", "需求电压"},
        {"bcl_current_ma", "需求电流"},
        {"bcl_mode", "充电模式"},
        {"bcs_voltage_mv", "测量总电压"},
        {"bcs_current_ma", "测量电流"},
        {"bcs_cell_max_voltage_mv", "最高单体电压"},
        {"bcs_cell_group", "最高单体电压组号"},
        {"bcs_soc_pct", "当前荷电状态"},
        {"remaining_minutes", "预计剩余分钟"},
        {"ccs_voltage_mv", "输出电压"},
        {"ccs_current_ma", "输出电流"},
        {"ccs_elapsed_minutes", "累计充电分钟"},
        {"ccs_allow", "充电允许状态"},
        {"bsm_cell_index", "最高单体电压编号"},
        {"bsm_max_temperature_degc", "最高温度"},
        {"bsm_max_temperature_index", "最高温度探头编号"},
        {"bsm_min_temperature_degc", "最低温度"},
        {"bsm_min_temperature_index", "最低温度探头编号"},
        {"bsm_cell_voltage_state", "单体电压状态"},
        {"bsm_soc_state", "荷电状态"},
        {"bsm_current_state", "电流状态"},
        {"bsm_temperature_state", "温度状态"},
        {"bsm_insulation_state", "绝缘状态"},
        {"bsm_connector_state", "连接器状态"},
        {"bsm_allow", "充电允许状态"},
        {"bst_reason", "BMS 中止原因"},
        {"bst_fault", "BMS 中止故障"},
        {"bst_error", "BMS 中止错误"},
        {"cst_reason", "充电机中止原因"},
        {"cst_fault", "充电机中止故障"},
        {"cst_error", "充电机中止错误"},
        {"bsd_soc_pct", "结束荷电状态"},
        {"bsd_cell_min_voltage_mv", "最低单体电压"},
        {"bsd_cell_max_voltage_mv", "最高单体电压"},
        {"bsd_min_temperature_degc", "最低温度"},
        {"bsd_max_temperature_degc", "最高温度"},
        {"csd_elapsed_minutes", "累计充电分钟"},
        {"csd_energy_wh", "输出能量"},
        {"csd_charger_id", "充电机编号"},
        {"bem_crm_initial", "CRM 初始辨识接收状态"},
        {"bem_crm_confirm", "CRM 确认辨识接收状态"},
        {"bem_cml", "CML 接收状态"},
        {"bem_cro", "CRO 接收状态"},
        {"bem_ccs", "CCS 接收状态"},
        {"bem_cst", "CST 接收状态"},
        {"bem_csd", "CSD 接收状态"},
        {"cem_brm", "BRM 接收状态"},
        {"cem_bcp", "BCP 接收状态"},
        {"cem_bro", "BRO 接收状态"},
        {"cem_bcs", "BCS 接收状态"},
        {"cem_bcl", "BCL 接收状态"},
        {"cem_bst", "BST 接收状态"},
        {"cem_bsd", "BSD 接收状态"},
        {"cem_bsm", "BSM 接收状态"},
        {"cts_second", "时间同步秒"},
        {"cts_minute", "时间同步分"},
        {"cts_hour", "时间同步时"},
        {"cts_day", "时间同步日"},
        {"cts_month", "时间同步月"},
        {"cts_year", "时间同步年"},
    };
    for (size_t idx = 0u; idx < sizeof(fields) / sizeof(fields[0]); idx++)
    {
        if (strcmp(key, fields[idx].key) == 0)
        {
            return fields[idx].name;
        }
    }
    return key;
}
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
    (void) snprintf(text, sizeof(text), "%s：%s（代码 %d）", operation, rc == 0 ? "已接收" : (rc == -EBUSY ? "繁忙，请重试" : "已拒绝"), rc);
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
static void append_text(char *destination, size_t capacity, const char *text);
static const char *ui_reason(const char *reason)
{
    static const struct
    {
        const char *key;
        const char *text;
    } reasons[] = {
        {"message_timeout", "报文接收超时"}, {"ready_timeout", "准备就绪超时"},
        {"session_timeout", "会话总时长超限"}, {"runtime_error", "运行错误，详见会话结果代码及日志"},
        {"tp_error", "TP 传输失败"}, {"peer_error", "收到对端错误报文"},
        {"battery_parameters_mismatch", "电池参数不匹配"}, {"charger_parameters_mismatch", "充电机参数不匹配"},
        {"self_test_failed", "模拟自检条件未通过"}, {"precharge_failed", "模拟预充条件未通过"},
        {"session_start", "会话开始"}, {"restart", "恢复后重试"}, {"operator_stop", "手动请求中止"},
        {"peer_stop", "对端请求中止"}, {"handshake_complete", "握手完成"}, {"crm_initial", "收到初始辨识报文"},
        {"identified", "辨识完成"}, {"battery_parameters_accepted", "电池参数已接受"},
        {"charger_parameters_accepted", "充电机参数已接受"}, {"bro_received", "收到 BMS 就绪报文"},
        {"bcl_bcs_received", "需求和状态报文已收到"}, {"soc_target", "达到配置的荷电目标"},
        {"both_ready", "双方已就绪"}, {"stop_acknowledged", "中止交互完成"},
        {"statistics_complete", "统计完成"}, {"retries_exhausted", "恢复结束或重试条件不满足"},
        {"session_failed", "会话失败结束"}, {"session_complete", "会话正常结束"}};
    for (size_t idx = 0u; idx < sizeof(reasons) / sizeof(reasons[0]); idx++)
    {
        if (strcmp(reason, reasons[idx].key) == 0)
        {
            return reasons[idx].text;
        }
    }
    return reason[0] != '\0' ? reason : "未记录";
}
static const char *ui_message(gbt27930_message_t message)
{
    const gbt27930_descriptor_t *descriptor = gbt27930_descriptor(message);
    return descriptor != NULL ? descriptor->name : "未记录";
}
static void detail_refresh(sim_ui_view_t *view)
{
    if (view->detail_panel == NULL || lv_obj_is_hidden(view->detail_panel))
    {
        return;
    }
    const sim_status_t *status = &view->data.status;
    if (status->session_id != view->detail_session)
    {
        lv_dropdown_close(view->detail_choice);
        lv_obj_set_hidden(view->detail_panel, true);
        lv_obj_set_clickable(lv_layer_top(), false);
        return;
    }
    char text[4096], options[4096] = "";
    (void) snprintf(text, sizeof(text), "会话 %u / %s阶段详情", status->session_id, ui_stage_name(view->detail_stage));
    lv_label_set_text(view->detail_title, text);
    uint32_t first = status->history_count > SIM_STAGE_HISTORY ? status->history_count - SIM_STAGE_HISTORY : 0u;
    unsigned selected = 0u, count = 0u;
    const sim_transition_t *record = NULL, *latest = NULL;
    uint32_t latest_index = 0u;
    for (uint32_t idx = first; idx < status->history_count; idx++)
    {
        const sim_transition_t *entry = &status->history[idx % SIM_STAGE_HISTORY];
        if (entry->stage != view->detail_stage)
        {
            continue;
        }
        (void) snprintf(text, sizeof(text), "%s记录 #%u | %u ms | %s", count != 0u ? "\n" : "", idx + 1u, entry->entered_ms,
            entry->failed ? "异常" : (entry->closed ? "已结束" : "进行中"));
        append_text(options, sizeof(options), text);
        if (idx == view->detail_record)
        {
            selected = count;
            record = entry;
        }
        latest = entry;
        latest_index = idx;
        count++;
    }
    if (view->detail_record == UINT32_MAX && latest != NULL)
    {
        view->detail_record = latest_index;
        record = latest;
        selected = count - 1u;
    }
    if (strcmp(lv_dropdown_get_options(view->detail_choice), options) != 0)
    {
        lv_dropdown_set_options(view->detail_choice, options);
    }
    lv_dropdown_set_selected(view->detail_choice, selected);
    lv_obj_set_hidden(view->detail_choice, count == 0u);
    if (record == NULL)
    {
        lv_label_set_text(view->detail_text, view->detail_record == UINT32_MAX ? (status->history_count <= SIM_STAGE_HISTORY ? "本轮尚未进入此阶段。" : "保留历史中无此阶段记录；较早记录已淘汰，无法判断此前是否进入。") : "该条阶段记录已被淘汰，无法还原详情。可选择其他保留记录。旧记录缺失不代表此阶段从未进入。");
        return;
    }
    uint32_t end = record->closed ? record->exited_ms : status->elapsed_ms;
    (void) snprintf(text, sizeof(text), "进入：%u ms（相对本轮开始）\n进入原因：%s (%s)\n停留：%u ms\n", record->entered_ms,
        ui_reason(record->reason), record->reason, end - record->entered_ms);
    char part[768];
    if (record->closed)
    {
        (void) snprintf(part, sizeof(part), "退出：%u ms\n退出原因：%s (%s)\n", record->exited_ms, ui_reason(record->exit_reason), record->exit_reason);
    }
    else
    {
        (void) snprintf(part, sizeof(part), "退出：尚未退出\n");
    }
    append_text(text, sizeof(text), part);
    if (record->has_last_rx)
    {
        (void) snprintf(part, sizeof(part), "本阶段最后有效接收：%s / %u ms\n", ui_message(record->last_rx_message), record->last_rx_ms);
        append_text(text, sizeof(text), part);
    }
    else
    {
        append_text(text, sizeof(text), "本阶段最后有效接收：未记录\n");
    }
    if (record->failed)
    {
        (void) snprintf(part, sizeof(part), "失败：%s (%s)\n失败时间：%u ms | 相关报文：%s\n", ui_reason(record->error), record->error,
            record->failed_ms, ui_message(record->error_message));
        append_text(text, sizeof(text), part);
        if (record->error_message < GBT27930_MESSAGE_COUNT)
        {
            if (record->has_error_rx)
            {
                (void) snprintf(part, sizeof(part), "相关报文最后有效接收：%u ms（失败时的接收依据）\n", record->error_rx_ms);
                append_text(text, sizeof(text), part);
            }
            else
            {
                append_text(text, sizeof(text), "相关报文最后有效接收：本次尝试未收到\n");
            }
        }
        if (record->timeout_kind != SIM_TIMEOUT_NONE)
        {
            const char *kinds[] = {"未记录", "首次等待", "持续接收", "阶段等待", "会话总时长"};
            (void) snprintf(part, sizeof(part), "超时判据：%s\n计时起点：%u ms | 阈值：%u ms | 实际等待：%u ms\n",
                kinds[record->timeout_kind], record->timeout_origin_ms, record->timeout_limit_ms, record->timeout_wait_ms);
            append_text(text, sizeof(text), part);
        }
        else
        {
            append_text(text, sizeof(text), "超时判据：未记录（非超时或证据不足）\n");
        }
    }
    else
    {
        append_text(text, sizeof(text), "失败：本条记录未记录失败\n");
    }
    (void) snprintf(part, sizeof(part), "本阶段本端注入：手动 %u 次，配置 %u 次\n", record->manual_hits, record->config_hits);
    append_text(text, sizeof(text), part);
    if (record->manual_hits != 0u || record->config_hits != 0u)
    {
        const sim_fault_evidence_t *hit = &record->last_fault;
        const char *actions[] = {"丢弃", "延迟", "重复", "修改字节", "修改长度", "修改 CAN 标识符"};
        (void) snprintf(part, sizeof(part), "最近命中：%u ms | %s规则 %u | %s | %s | %s\n", hit->at_ms,
            hit->manual ? "手动" : "配置", hit->rule, hit->message, hit->receive ? "接收" : "发送", actions[hit->action]);
        append_text(text, sizeof(text), part);
        if (hit->action == SIM_FAULT_DELAY)
        {
            (void) snprintf(part, sizeof(part), "延迟参数：%u ms\n", hit->delay_ms);
            append_text(text, sizeof(text), part);
        }
        else if (hit->action == SIM_FAULT_CORRUPT)
        {
            if (hit->byte < hit->length)
            {
                (void) snprintf(part, sizeof(part), "字节偏移：%u | 原值：%u | 替换值：%u | 原长度：%u\n", (unsigned) hit->byte, (unsigned) hit->before, hit->value, (unsigned) hit->length);
            }
            else
            {
                (void) snprintf(part, sizeof(part), "字节偏移：%u | 原长度：%u | 偏移越界，原值未记录\n", (unsigned) hit->byte, (unsigned) hit->length);
            }
            append_text(text, sizeof(text), part);
        }
        else if (hit->action == SIM_FAULT_LENGTH || hit->action == SIM_FAULT_ID)
        {
            (void) snprintf(part, sizeof(part), "替换值：%u | 原长度：%u\n", hit->value, (unsigned) hit->length);
            append_text(text, sizeof(text), part);
        }
        append_text(text, sizeof(text), "仅展示最近一次命中详情，其余见日志；命中不等同于失败原因。\n");
    }
    append_text(text, sizeof(text), "对端注入及未发送原因：本端无法直接观测。\n");
    if (status->history_count > SIM_STAGE_HISTORY)
    {
        append_text(text, sizeof(text), "较早的阶段记录已淘汰。\n");
    }
    lv_label_set_text(view->detail_text, text);
}
static void detail_closed(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    lv_dropdown_close(view->detail_choice);
    lv_obj_set_hidden(view->detail_panel, true);
    lv_obj_set_clickable(lv_layer_top(), false);
}
static void detail_selected(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    unsigned selected = lv_dropdown_get_selected(view->detail_choice);
    const sim_status_t *status = &view->data.status;
    uint32_t first = status->history_count > SIM_STAGE_HISTORY ? status->history_count - SIM_STAGE_HISTORY : 0u;
    for (uint32_t idx = first; idx < status->history_count; idx++)
    {
        if (status->history[idx % SIM_STAGE_HISTORY].stage == view->detail_stage)
        {
            if (selected == 0u)
            {
                view->detail_record = idx;
                break;
            }
            selected--;
        }
    }
    detail_refresh(view);
}
static void stage_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    lv_obj_t *node = lv_event_get_current_target(event);
    for (unsigned idx = 0u; idx < SIM_STAGE_IDLE; idx++)
    {
        if (node == view->nodes[idx])
        {
            view->detail_stage = (sim_stage_t) idx;
            break;
        }
    }
    const sim_status_t *status = &view->data.status;
    view->detail_session = status->session_id;
    view->detail_record = UINT32_MAX;
    uint32_t first = status->history_count > SIM_STAGE_HISTORY ? status->history_count - SIM_STAGE_HISTORY : 0u;
    bool failed = false;
    for (uint32_t idx = first; idx < status->history_count; idx++)
    {
        const sim_transition_t *entry = &status->history[idx % SIM_STAGE_HISTORY];
        if (entry->stage == view->detail_stage && (entry->failed || !failed))
        {
            view->detail_record = idx;
            failed = entry->failed;
        }
    }
    lv_obj_set_clickable(lv_layer_top(), true);
    lv_obj_set_hidden(view->detail_panel, false);
    detail_refresh(view);
    lv_obj_scroll_to_y(lv_obj_get_parent(view->detail_text), 0, LV_ANIM_OFF);
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
        lv_label_set_text(view->feedback, "请先应用修改后的参数再开始充电");
        return;
    }
    sim_control_request_t request = request_for(view, SIM_CONTROL_START);
    feedback(view, "开始请求入队", sim_control_submit(view->control, &request));
}
static void stop_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_stop(view->control, view->data.status.session_id);
    lv_label_set_text(view->feedback, "已请求停止，等待协议中止流程完成");
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
            (void) snprintf(text, sizeof(text), "参数值无效：%s（代码 %d）", gbt27930_field(idx)->key, rc);
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
    feedback(view, "应用请求入队", rc);
}
static void save_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    if (view->dirty || view->apply_request != 0u)
    {
        lv_label_set_text(view->feedback, "请先应用修改后的参数再保存");
        return;
    }
    feedback(view, "保存请求入队", sim_control_save(view->control));
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
    static const char *const stages[] = {"*", "handshake", "identify", "config", "ready", "charging", "stopping", "statistics", "recovery", "done"};
    unsigned selected = lv_dropdown_get_selected(view->stage);
    if (selected >= sizeof(stages) / sizeof(stages[0]))
    {
        feedback(view, "注入阶段", -EINVAL);
        return;
    }
    (void) snprintf(rule->stage, sizeof(rule->stage), "%s", stages[selected]);
    rule->receive = lv_dropdown_get_selected(view->direction) == 1u;
    rule->action = (sim_fault_action_t) lv_dropdown_get_selected(view->action);
    uint32_t byte;
    if (unsigned_input(view->count, &rule->count) != 0 || unsigned_input(view->duration, &rule->duration_ms) != 0 ||
        unsigned_input(view->delay, &rule->delay_ms) != 0 || unsigned_input(view->byte, &byte) != 0 || byte > 1784u || unsigned_input(view->value, &rule->value) != 0)
    {
        feedback(view, "注入参数", -EINVAL);
        return;
    }
    rule->byte = (uint16_t) byte;
    int rc = sim_config_validate_fault(rule);
    if (rc == 0)
    {
        rc = sim_control_submit(view->control, &request);
    }
    feedback(view, "启用注入请求入队", rc);
}
static void cancel_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    sim_control_request_t request = request_for(view, SIM_CONTROL_CANCEL);
    feedback(view, "取消注入请求入队", sim_control_submit(view->control, &request));
}
static void pause_clicked(lv_event_t *event)
{
    sim_ui_view_t *view = lv_event_get_user_data(event);
    view->paused = !view->paused;
    lv_label_set_text(lv_obj_get_child(view->pause, 0), view->paused ? "继续显示" : "暂停显示");
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
    lv_obj_t *heading = label(view->root, "GB/T 27930 协议模拟器");
    lv_obj_set_style_text_font(heading, &sim_ui_font_16, 0);
    view->banner = label(view->root, "手动注入：关闭");
    view->feedback = label(view->root, "就绪");
    view->tabs = lv_tabview_create(view->root);
    lv_tabview_set_tab_bar_size(view->tabs, 44);
    lv_obj_set_width(view->tabs, LV_PCT(100));
    lv_obj_set_flex_grow(view->tabs, 1);
    lv_obj_t *home = page(view->tabs, "主页");
    view->summary = label(home, "待机");
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
        lv_obj_set_clickable(view->nodes[idx], true);
        lv_obj_add_event_cb(view->nodes[idx], stage_clicked, LV_EVENT_CLICKED, view);
        view->node_text[idx] = label(view->nodes[idx], ui_stage_name((sim_stage_t) idx));
    }
    label(home, "点击阶段查看详情，异常阶段优先显示失败记录。");
    view->path = label(home, "实际路径：待机");
    view->wait = label(home, "等待开始充电");
    view->measurements = label(home, "对端数据：暂无有效值");
    view->parameters = page(view->tabs, "参数设置");
    view->apply = button(view->parameters, "应用到下一轮", apply_clicked, view);
    view->save = button(view->parameters, "保存配置", save_clicked, view);
    view->save_state = label(view->parameters, "编辑后请先应用，保存仅写入已应用的参数");
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        const gbt27930_field_t *field = gbt27930_field(idx);
        if (gbt27930_descriptor(field->message)->charger != view->data.config.charger)
        {
            continue;
        }
        char initial[40], caption[384], minimum[40], maximum[40], step[40];
        sim_control_format_value(field, view->data.config.values.field[idx], initial, sizeof(initial));
        sim_control_format_value(field, field->minimum, minimum, sizeof(minimum));
        sim_control_format_value(field, field->maximum, maximum, sizeof(maximum));
        sim_control_format_value(field, field->scale < 0 ? -(int64_t) field->scale : field->scale, step, sizeof(step));
        const char *unit = (strlen(field->key) >= 3u && strcmp(field->key + strlen(field->key) - 3u, "_mv") == 0) ? "V" : ((strlen(field->key) >= 3u && strcmp(field->key + strlen(field->key) - 3u, "_ma") == 0) ? "A" : "单位见配置键");
        (void) snprintf(caption, sizeof(caption), "%s / %s（%s）范围 [%s..%s]，步长 %s\n%s", gbt27930_descriptor(field->message)->name, ui_field_name(field->key), unit, minimum, maximum, step, field->key);
        view->fields[idx] = input(view->parameters, caption, initial, view, true);
    }
    lv_obj_t *controls = page(view->tabs, "充电控制");
    view->control_state = label(controls, "等待");
    view->start = button(controls, "开始充电", start_clicked, view);
    view->stop = button(controls, "停止充电", stop_clicked, view);
    label(controls, view->data.config.charger ? "点击开始发送 CHM，充电结束后返回待机。" : "BMS 等待对端合法 CHM 自动开始，无需手动启动。");
    lv_obj_t *faults = page(view->tabs, "错误注入");
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
    view->enable = button(faults, "启用手动注入", fault_clicked, view);
    view->disable = button(faults, "关闭手动注入", cancel_clicked, view);
    view->message = dropdown(faults, "报文", options);
    view->stage = dropdown(faults, "阶段", "全部阶段\n握手\n辨识\n参数配置\n准备就绪\n充电\n中止\n统计\n异常恢复\n结束");
    view->direction = dropdown(faults, "方向", "发送（tx）\n接收（rx）");
    view->action = dropdown(faults, "动作", "丢弃\n延迟\n重复\n修改字节\n修改长度\n修改 CAN 标识符");
    view->count = input(faults, "命中次数（0 表示不限）", "1", view, false);
    view->duration = input(faults, "启用后持续时间（ms，0 表示不限）", "0", view, false);
    view->delay = input(faults, "延迟（ms）", "100", view, false);
    view->byte = input(faults, "字节偏移（TP_DT 序号位于 0）", "0", view, false);
    view->value = input(faults, "替换值（十进制）", "0", view, false);
    lv_obj_t *logs = page(view->tabs, "日志信息");
    const sim_build_info_t *info = sim_build_info_get();
    char text[768];
    (void) snprintf(text, sizeof(text), "版本：%s | 编译时间：%s | 目标：%s\n配置：%s\n文件日志：%s | candump：%s", info->version, info->time_utc, info->target, view->config_path[0] != '\0' ? view->config_path : "无可写配置来源", view->data.config.file_log ? "开启" : "关闭", view->data.config.candump ? "开启" : "关闭");
    label(logs, text);
    view->pause = button(logs, "暂停显示", pause_clicked, view);
    button(logs, "清空显示", clear_clicked, view);
    label(logs, "按标签或文本筛选（英文键盘）");
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
    view->detail_panel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(view->detail_panel, LV_PCT(96), LV_PCT(94));
    lv_obj_center(view->detail_panel);
    lv_obj_set_flex_flow(view->detail_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(view->detail_panel, false);
    lv_obj_set_style_pad_all(view->detail_panel, 8, 0);
    lv_obj_set_style_pad_row(view->detail_panel, 4, 0);
    view->detail_close = button(view->detail_panel, "关闭详情", detail_closed, view);
    view->detail_title = label(view->detail_panel, "阶段详情");
    view->detail_choice = dropdown(view->detail_panel, "本轮进入记录（可选择每次重试）", "");
    lv_obj_add_event_cb(view->detail_choice, detail_selected, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_t *detail_body = lv_obj_create(view->detail_panel);
    lv_obj_set_width(detail_body, LV_PCT(100));
    lv_obj_set_flex_grow(detail_body, 1);
    lv_obj_set_style_min_height(detail_body, 40, 0);
    lv_obj_set_style_pad_all(detail_body, 4, 0);
    view->detail_text = label(detail_body, "");
    lv_obj_set_hidden(view->detail_panel, true);
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
        (void) snprintf(message, sizeof(message), "请求 %u 已完成：%s（代码 %d）", result.id, result.result == 0 ? "成功" : "已拒绝", result.result);
        lv_label_set_text(view->feedback, message);
    }
    if (view->stop_sequence != view->data.stop_sequence)
    {
        view->stop_sequence = view->data.stop_sequence;
        feedback(view, "停止请求", view->data.stop_result);
    }
    char text[2048];
    (void) snprintf(text, sizeof(text), "%s | %u | %s\n%s%s | 会话 %u | %.1f s | %s %.1f s | 参数版本 %u | 上轮结果 %d", view->data.config.charger ? "充电桩" : "BMS", view->data.config.version, view->data.config.can_interface, ui_stage_name(status->stage), status->paused ? "（暂停中）" : "", status->session_id, (double) status->elapsed_ms / 1000.0, status->active ? "阶段" : "上一阶段", (double) status->stage_elapsed_ms / 1000.0, status->revision, status->result);
    lv_label_set_text(view->summary, text);
    lv_label_set_text(view->control_state, text);
    (void) snprintf(text, sizeof(text), "手动注入：%s | 已命中 %u 次 | 请求 %u 结果 %d", status->fault_enabled ? (status->active ? "生效中" : "已预置到下一轮") : "关闭", status->fault_hits, view->data.request_id, view->data.request_result);
    lv_label_set_text(view->banner, text);
    lv_obj_set_style_text_color(view->banner, lv_color_hex(status->fault_enabled ? 0xFFBC52 : 0x8BDAC2), 0);
    detail_refresh(view);
    bool visited[SIM_STAGE_IDLE] = {false}, failed_stage[SIM_STAGE_IDLE] = {false};
    unsigned retained = status->history_count < SIM_STAGE_HISTORY ? status->history_count : SIM_STAGE_HISTORY;
    for (unsigned idx = 0u; idx < retained; idx++)
    {
        const sim_transition_t *step = &status->history[(status->history_count - retained + idx) % SIM_STAGE_HISTORY];
        if ((unsigned) step->stage < SIM_STAGE_IDLE)
        {
            visited[step->stage] = true;
            failed_stage[step->stage] = failed_stage[step->stage] || step->failed;
        }
    }
    for (unsigned idx = 0u; idx < SIM_STAGE_IDLE; idx++)
    {
        bool current = status->active && (unsigned) status->stage == idx;
        bool error = failed_stage[idx];
        const char *state = error ? "异常" : (current ? "当前" : (visited[idx] ? "已进入" : "未进入"));
        (void) snprintf(text, sizeof(text), "%s\n%s", ui_stage_name((sim_stage_t) idx), state);
        lv_label_set_text(view->node_text[idx], text);
        lv_obj_set_style_bg_color(view->nodes[idx], lv_color_hex(error ? 0xF5B8B8 : (current ? 0xA9D6FF : (visited[idx] ? 0xBEE3D3 : 0xE4E8EF))), 0);
    }
    (void) snprintf(text, sizeof(text), "实际路径：");
    unsigned path_count = retained < 12u ? retained : 12u;
    for (unsigned idx = 0u; idx < path_count; idx++)
    {
        const sim_transition_t *step = &status->history[(status->history_count - path_count + idx) % SIM_STAGE_HISTORY];
        if (idx != 0u)
        {
            append_text(text, sizeof(text), " > ");
        }
        append_text(text, sizeof(text), ui_stage_name(step->stage));
    }
    if (!status->active)
    {
        append_text(text, sizeof(text), " > 待机");
    }
    lv_label_set_text(view->path, text);
    (void) snprintf(text, sizeof(text), "等待或监控：");
    for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT; idx++)
    {
        if ((status->waiting_messages & (1u << idx)) != 0u)
        {
            append_text(text, sizeof(text), gbt27930_descriptor((gbt27930_message_t) idx)->name);
            append_text(text, sizeof(text), " ");
        }
    }
    const char *conditions[] = {"自检 ", "预充 ", "本端就绪 ", "对端就绪 ", "本端就绪报文发送 ", "BCP 更新完成 ", "模拟辅助电源关闭 ", "恢复等待 "};
    for (unsigned idx = 0u; idx < 8u; idx++)
    {
        if ((status->conditions & (1u << idx)) != 0u)
        {
            append_text(text, sizeof(text), conditions[idx]);
        }
    }
    if (!status->active)
    {
        append_text(text, sizeof(text), view->data.config.charger ? "点击开始进入下一轮" : "对端 CHM");
    }
    if (status->error[0] != '\0')
    {
        append_text(text, sizeof(text), "\n最近错误：");
        append_text(text, sizeof(text), status->error);
        if (status->error_message < GBT27930_MESSAGE_COUNT)
        {
            append_text(text, sizeof(text), " / ");
            append_text(text, sizeof(text), gbt27930_descriptor(status->error_message)->name);
        }
    }
    if (status->history_count > SIM_STAGE_HISTORY)
    {
        append_text(text, sizeof(text), "\n较早的状态记录已淘汰，历史不完整");
    }
    lv_label_set_text(view->wait, text);
    const gbt27930_values_t *local = status->active ? &status->local : &view->data.config.values;
    (void) snprintf(text, sizeof(text), "本端模拟：%s电压 %.1f V | 电流 %.1f A\n", view->data.config.charger ? "能力" : "需求", (double) gbt27930_get(local, view->data.config.charger ? "cml_max_voltage_mv" : "bcl_voltage_mv") / 1000.0, (double) gbt27930_get(local, view->data.config.charger ? "cml_max_current_ma" : "bcl_current_ma") / 1000.0);
    gbt27930_message_t measurement = view->data.config.charger ? GBT27930_BCL : GBT27930_CCS;
    if (status->active && (status->peer_valid_messages & (1u << measurement)) != 0u)
    {
        char peer[160];
        (void) snprintf(peer, sizeof(peer), "对端上报：%.1f V | %.1f A", (double) gbt27930_get(&status->peer, view->data.config.charger ? "bcl_voltage_mv" : "ccs_voltage_mv") / 1000.0, (double) gbt27930_get(&status->peer, view->data.config.charger ? "bcl_current_ma" : "ccs_current_ma") / 1000.0);
        append_text(text, sizeof(text), peer);
    }
    else
    {
        append_text(text, sizeof(text), "对端上报：未收到或已过期");
    }
    if (view->data.config.charger && status->active && (status->peer_valid_messages & (1u << GBT27930_BCS)) != 0u)
    {
        char soc[64];
        (void) snprintf(soc, sizeof(soc), " | 对端 SOC %" PRId64 "%%", gbt27930_get(&status->peer, "bcs_soc_pct"));
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
    (void) snprintf(text, sizeof(text), "编辑：%s | 已应用版本 %u | 已保存版本 %u | 保存：%s（代码 %d）", view->dirty ? "未应用" : "已应用", status->revision, view->data.saved_revision, view->data.saving ? "处理中" : (status->revision == view->data.saved_revision ? "已保存" : "未保存"), view->data.save_result);
    lv_label_set_text(view->save_state, text);
    (void) snprintf(text, sizeof(text), "界面日志淘汰或跳过：%u | %s", view->data.log_dropped, view->paused ? "已暂停" : "实时显示");
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
        if (!lv_obj_is_hidden(view->detail_panel))
        {
            lv_obj_set_clickable(lv_layer_top(), false);
        }
        lv_obj_delete(view->detail_panel);
        lv_obj_delete(view->root);
        osal_free(view);
    }
}
lv_obj_t *sim_ui_view_widget(sim_ui_view_t *view, const char *name)
{
    if (strcmp(name, "stage_detail") == 0)
    {
        return view->detail_text;
    }
    if (strcmp(name, "stage_records") == 0)
    {
        return view->detail_choice;
    }
    if (strcmp(name, "stage_close") == 0)
    {
        return view->detail_close;
    }
    if (strcmp(name, "stage_panel") == 0)
    {
        return view->detail_panel;
    }
    for (unsigned idx = 0u; idx < SIM_STAGE_IDLE; idx++)
    {
        char node[40];
        (void) snprintf(node, sizeof(node), "stage_%s", sim_stage_name((sim_stage_t) idx));
        if (strcmp(name, node) == 0)
        {
            return view->nodes[idx];
        }
    }
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
