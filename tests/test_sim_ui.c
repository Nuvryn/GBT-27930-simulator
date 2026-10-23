/** @file test_sim_ui.c @brief Offscreen LVGL with actual widgets, touch input and paired protocol engines. */
#include "ui/sim_ui_view.h"
#include "logging/sim_log.h"
#include "src/misc/lv_text_private.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(expr)                                            \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); \
            exit(1);                                           \
        }                                                      \
    } while (0)
static sim_frame_t frames[32768];
static size_t head, tail;
static uint32_t *pixels;
static unsigned width, height;
static lv_indev_data_t touch;
static int send_frame(void *user, const sim_frame_t *frame)
{
    (void) user;
    CHECK(tail < 32768u);
    frames[tail++] = *frame;
    return 0;
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    CHECK(area->x1 >= 0 && area->y1 >= 0 && area->x2 < (int32_t) width && area->y2 < (int32_t) height);
    size_t line = (size_t) (area->x2 - area->x1 + 1);
    for (int32_t y = area->y1; y <= area->y2; y++)
    {
        memcpy(pixels + (size_t) y * width + (size_t) area->x1, data, line * 4u);
        data += line * 4u;
    }
    lv_display_flush_ready(display);
}
static void read_touch(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void) indev;
    *data = touch;
}
static void click(sim_ui_view_t *view, const char *widget)
{
    lv_obj_t *obj = sim_ui_view_widget(view, widget);
    CHECK(obj != NULL);
    CHECK(!lv_obj_has_state(obj, LV_STATE_DISABLED));
    CHECK(lv_obj_send_event(obj, LV_EVENT_CLICKED, NULL) == LV_RESULT_OK);
}
static void touch_click(sim_ui_view_t *view, const char *widget)
{
    lv_obj_t *obj = sim_ui_view_widget(view, widget);
    CHECK(obj != NULL);
    lv_obj_scroll_to_view_recursive(obj, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    touch.point.x = (area.x1 + area.x2) / 2;
    touch.point.y = (area.y1 + area.y2) / 2;
    CHECK(touch.point.x >= 0 && touch.point.x < (int32_t) width && touch.point.y >= 0 && touch.point.y < (int32_t) height);
    lv_obj_t *hit = lv_indev_search_obj(lv_layer_top(), &touch.point);
    if (hit == NULL)
    {
        hit = lv_indev_search_obj(lv_screen_active(), &touch.point);
    }
    CHECK(hit == obj);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_touch);
    touch.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(indev);
    touch.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(indev);
    lv_indev_delete(indev);
}
static void screenshot(const char *directory, const char *name)
{
    char path[512];
    (void) snprintf(path, sizeof(path), "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    CHECK(fprintf(file, "P6\n%u %u\n255\n", width, height) > 0);
    for (size_t i = 0u; i < (size_t) width * height; i++)
    {
        uint32_t color = pixels[i];
        uint8_t rgb[3] = {(uint8_t) (color >> 16u), (uint8_t) (color >> 8u), (uint8_t) color};
        CHECK(fwrite(rgb, 1u, 3u, file) == 3u);
    }
    CHECK(fclose(file) == 0);
}
static void run_pair(unsigned version, bool gui_charger, const char *directory)
{
    sim_config_t charger, bms;
    sim_config_defaults(&charger);
    charger.manual_start = true;
    charger.version = version;
    charger.stop_after_ms = 1000u;
    charger.max_session_ms = 12000u;
    charger.retries = 0u;
    charger.reconnect_allowed = false;
    bms = charger;
    bms.charger = false;
    bms.source = 0xF4u;
    bms.peer = 0x56u;
    bms.stop_after_ms = 0u;
    if (version == 2023u)
    {
        bms.event_count = 2u;
        bms.events[0] = (sim_event_t){.stage = "charging", .action = "pause", .after_ms = 200u};
        bms.events[1] = (sim_event_t){.stage = "charging", .action = "resume", .after_ms = 700u};
    }
    sim_session_t *left = sim_session_create(&charger, send_frame, NULL), *right = sim_session_create(&bms, send_frame, NULL);
    CHECK(left != NULL && right != NULL);
    sim_session_t *subject = gui_charger ? left : right;
    sim_control_t *control = sim_control_create(gui_charger ? &charger : &bms, NULL);
    CHECK(control != NULL);
    sim_log_observe(sim_control_log, control);
    sim_ui_view_t *view = sim_ui_view_create(control, NULL);
    CHECK(view != NULL);
    uint32_t now = 100000u;
    for (unsigned round = 0u; round < 2u; round++)
    {
        head = tail = 0u;
        if (round != 0u)
        {
            lv_obj_t *field = sim_ui_view_widget(view, gui_charger ? "cml_max_voltage_mv" : "bcl_voltage_mv");
            CHECK(field != NULL);
            lv_textarea_set_text(field, gui_charger ? "740.000" : "410.000");
            click(view, "apply");
            CHECK(sim_control_step(control, subject, now) == 0);
            CHECK(sim_control_publish(control, subject) == 0);
            sim_ui_view_refresh(view);
            sim_control_view_t data;
            CHECK(sim_control_read(control, &data) == 0 && data.status.revision == 2u);
        }
        if (gui_charger)
        {
            if (round == 0u)
            {
                lv_tabview_set_active(sim_ui_view_widget(view, "tabs"), 2u, LV_ANIM_OFF);
                lv_obj_update_layout(lv_screen_active());
                lv_refr_now(NULL);
                lv_area_t bounds;
                lv_obj_get_coords(sim_ui_view_widget(view, "start"), &bounds);
                touch.point.x = (bounds.x1 + bounds.x2) / 2;
                touch.point.y = (bounds.y1 + bounds.y2) / 2;
                CHECK(touch.point.y >= 0 && touch.point.y < (int32_t) height);
                lv_indev_t *indev = lv_indev_create();
                lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
                lv_indev_set_read_cb(indev, read_touch);
                touch.state = LV_INDEV_STATE_PRESSED;
                lv_indev_read(indev);
                touch.state = LV_INDEV_STATE_RELEASED;
                lv_indev_read(indev);
                lv_indev_delete(indev);
            }
            else
            {
                click(view, "start");
            }
        }
        else
        {
            CHECK(lv_obj_has_state(sim_ui_view_widget(view, "start"), LV_STATE_DISABLED));
            CHECK(sim_session_start(left, now) == 0);
        }
        int lrc = 0, rrc = 0;
        bool charging_seen = false, injected = false, stopped = false, paused_seen = false, resumed_seen = false;
        for (uint32_t elapsed = 0u; elapsed < 12000u && (lrc == 0 || rrc == 0); elapsed++, now++)
        {
            CHECK(sim_control_step(control, subject, now) == 0);
            if (lrc == 0)
            {
                lrc = sim_session_tick(left, now);
            }
            if (rrc == 0)
            {
                rrc = sim_session_tick(right, now);
            }
            while (head < tail)
            {
                sim_frame_t frame = frames[head++];
                CHECK(sim_session_receive((uint8_t) frame.id == 0x56u ? right : left, &frame, now) == 0);
            }
            CHECK(sim_control_publish(control, subject) == 0);
            if (elapsed % 100u == 0u)
            {
                sim_ui_view_refresh(view);
                sim_control_work(control);
                lv_tick_inc(100u);
                (void) lv_timer_handler();
                sim_control_view_t data;
                CHECK(sim_control_read(control, &data) == 0);
                if (data.status.stage == SIM_STAGE_CHARGING)
                {
                    charging_seen = true;
                    if (data.status.paused)
                    {
                        paused_seen = true;
                    }
                    else if (paused_seen)
                    {
                        resumed_seen = true;
                    }
                    if (!injected)
                    {
                        /* UI enables a one-shot drop on a nonessential optional message, then cancels it. */
                        lv_dropdown_set_selected(sim_ui_view_widget(view, "fault_message"), GBT27930_BMV);
                        click(view, "enable");
                        CHECK(sim_control_step(control, subject, now) == 0);
                        CHECK(sim_control_publish(control, subject) == 0);
                        sim_ui_view_refresh(view);
                        click(view, "disable");
                        CHECK(sim_control_step(control, subject, now) == 0);
                        CHECK(sim_control_publish(control, subject) == 0);
                        sim_ui_view_refresh(view);
                        injected = true;
                    }
                    if (round == 0u && !stopped)
                    {
                        lv_tabview_set_active(sim_ui_view_widget(view, "tabs"), 0u, LV_ANIM_OFF);
                        lv_refr_now(NULL);
                        char name[64];
                        (void) snprintf(name, sizeof(name), "ui_%u_%s_%u", version, gui_charger ? "charger" : "bms", width);
                        screenshot(directory, name);
                        click(view, "stop");
                        stopped = true;
                    }
                }
            }
        }
        CHECK(lrc == 1 && rrc == 1 && charging_seen && injected);
        if (round == 0u)
        {
            CHECK(stopped);
        }
        if (version == 2023u && round == 1u)
        {
            CHECK(paused_seen && resumed_seen);
        }
        CHECK(sim_control_publish(control, subject) == 0);
        sim_ui_view_refresh(view);
        sim_control_view_t data;
        CHECK(sim_control_read(control, &data) == 0 && !data.status.active && data.status.stage == SIM_STAGE_IDLE && data.status.result == 1);
        CHECK(data.status.history_count >= 7u && !data.status.fault_enabled);
        CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "summary")), "待机") != NULL);
        for (unsigned record = 0u; record < data.status.history_count; record++)
        {
            const sim_transition_t *entry = &data.status.history[record % SIM_STAGE_HISTORY];
            CHECK(entry->closed && entry->exited_ms >= entry->entered_ms && !entry->failed);
        }
        click(view, "stage_handshake");
        CHECK(!lv_obj_is_hidden(sim_ui_view_widget(view, "stage_panel")));
        CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "握手完成") != NULL || !gui_charger);
        CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "退出：") != NULL);
        click(view, "stage_close");
        CHECK(lv_obj_is_hidden(sim_ui_view_widget(view, "stage_panel")));
        now += 10000u;
    }
    sim_log_observe(NULL, NULL);
    sim_ui_view_destroy(view);
    sim_control_destroy(control);
    sim_session_destroy(left);
    sim_session_destroy(right);
}
static void failed_flow(const char *directory)
{
    sim_config_t config;
    sim_config_defaults(&config);
    config.manual_start = true;
    config.recovery_wait_ms = 50u;
    config.aux_power_off_delay_ms = 10u;
    config.first_timeout_ms[GBT27930_BHM] = 100u;
    config.max_session_ms = 1000u;
    config.fault_count = 1u;
    config.faults[0] = (sim_fault_rule_t){.message = "CHM", .stage = "*", .action = SIM_FAULT_DROP, .count = 1u};
    sim_session_t *session = sim_session_create(&config, send_frame, NULL);
    sim_control_t *control = sim_control_create(&config, NULL);
    CHECK(session != NULL && control != NULL);
    sim_ui_view_t *view = sim_ui_view_create(control, NULL);
    CHECK(view != NULL);
    head = tail = 0u;
    sim_fault_rule_t dropped = {.message = "BHM", .stage = "*", .receive = true, .action = SIM_FAULT_DROP, .count = 1u, .delay_ms = 100u};
    CHECK(sim_session_manual_fault(session, &dropped) == 0);
    click(view, "start");
    CHECK(sim_control_step(control, session, 100000u) == 0);
    int rc = 0;
    for (uint32_t now = 100000u; now < 101000u && rc == 0; now++)
    {
        if (now == 100050u || now == 100075u)
        {
            sim_frame_t bhm = {.id = gbt27930_can_id(GBT27930_BHM, config.peer, config.source), .len = now == 100050u ? 2u : 1u, .data = {0x4Cu, 0x1Du}};
            CHECK(sim_session_receive(session, &bhm, now) == 0);
        }
        rc = sim_session_tick(session, now);
        CHECK(sim_control_publish(control, session) == 0);
    }
    CHECK(rc < 0);
    sim_ui_view_refresh(view);
    sim_control_view_t data;
    CHECK(sim_control_read(control, &data) == 0 && data.status.failed && !data.status.active);
    CHECK(data.status.error_stage == SIM_STAGE_HANDSHAKE && data.status.error_message == GBT27930_BHM);
    CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "waiting")), "message_timeout") != NULL);
    const sim_transition_t *failed = &data.status.history[0];
    CHECK(failed->failed && failed->closed && failed->entered_ms == 0u && failed->exited_ms == 100u);
    CHECK(failed->timeout_kind == SIM_TIMEOUT_FIRST && failed->timeout_limit_ms == 100u && failed->timeout_wait_ms == 100u);
    CHECK(!failed->has_error_rx && !failed->has_last_rx && failed->manual_hits == 1u && failed->config_hits == 1u);
    CHECK(failed->last_fault.receive && failed->last_fault.at_ms == 50u && strcmp(failed->last_fault.message, "BHM") == 0);
    touch_click(view, "stage_handshake");
    CHECK(!lv_obj_is_hidden(sim_ui_view_widget(view, "stage_panel")));
    lv_point_t edge = {0, 0};
    CHECK(lv_indev_search_obj(lv_layer_top(), &edge) == lv_layer_top());
    const char *detail = lv_label_get_text(sim_ui_view_widget(view, "stage_detail"));
    CHECK(strstr(detail, "报文接收超时") != NULL && strstr(detail, "阈值：100 ms") != NULL);
    CHECK(strstr(detail, "本次尝试未收到") != NULL && strstr(detail, "手动 1 次，配置 1 次") != NULL);
    CHECK(strstr(detail, "BHM") != NULL && strstr(detail, "命中不等同于失败原因") != NULL);
    lv_obj_update_layout(lv_layer_top());
    lv_area_t bounds;
    lv_obj_get_coords(sim_ui_view_widget(view, "stage_panel"), &bounds);
    CHECK(bounds.x1 >= 0 && bounds.y1 >= 0 && bounds.x2 < (int32_t) width && bounds.y2 < (int32_t) height);
    lv_refr_now(NULL);
    char name[64];
    (void) snprintf(name, sizeof(name), "ui_stage_failure_%u", width);
    screenshot(directory, name);
    /* Pending idle time must not change a frozen record's duration. */
    CHECK(sim_session_tick(session, 101999u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_ui_view_refresh(view);
    CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "停留：100 ms") != NULL);
    lv_dropdown_open(sim_ui_view_widget(view, "stage_records"));
    touch_click(view, "stage_close");
    CHECK(!lv_dropdown_is_open(sim_ui_view_widget(view, "stage_records")));
    CHECK(!lv_obj_is_clickable(lv_layer_top()));
    click(view, "stage_charging");
    CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "尚未进入") != NULL);
    click(view, "stage_close");
    click(view, "stage_handshake");
    lv_dropdown_open(sim_ui_view_widget(view, "stage_records"));
    /* Arm a CHM drop before the next start: it must affect the first frame. */
    click(view, "enable");
    CHECK(sim_control_step(control, session, 102000u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_ui_view_refresh(view);
    CHECK(sim_control_read(control, &data) == 0 && data.status.fault_enabled && !data.status.active);
    unsigned before = (unsigned) tail;
    click(view, "start");
    CHECK(sim_control_step(control, session, 102001u) == 0);
    CHECK(sim_session_tick(session, 102001u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_ui_view_refresh(view);
    CHECK(tail == before);
    CHECK(lv_obj_is_hidden(sim_ui_view_widget(view, "stage_panel")));
    CHECK(!lv_dropdown_is_open(sim_ui_view_widget(view, "stage_records")));
    CHECK(sim_control_read(control, &data) == 0 && data.status.fault_hits == 1u && !data.status.failed && data.status.session_id == 2u);
    sim_control_work(control);
    sim_control_work(control);
    sim_ui_view_destroy(view);
    sim_control_destroy(control);
    sim_session_destroy(session);
}
static void repeated_details(void)
{
    sim_config_t config;
    sim_config_defaults(&config);
    config.retries = 40u;
    config.reconnect_allowed = true;
    config.first_timeout_ms[GBT27930_BHM] = 10u;
    config.recovery_wait_ms = 1u;
    config.max_session_ms = 2000u;
    sim_session_t *session = sim_session_create(&config, send_frame, NULL);
    sim_control_t *control = sim_control_create(&config, NULL);
    CHECK(session != NULL && control != NULL && sim_session_start(session, 100000u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_ui_view_t *view = sim_ui_view_create(control, NULL);
    CHECK(view != NULL);
    CHECK(sim_session_tick(session, 100010u) == 0);
    CHECK(sim_control_publish(control, session) == 0);
    sim_ui_view_refresh(view);
    click(view, "stage_handshake");
    CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "进入：0 ms") != NULL);
    for (uint32_t now = 100011u; now <= 100400u; now++)
    {
        CHECK(sim_session_tick(session, now) == 0);
        CHECK(sim_control_publish(control, session) == 0);
        sim_ui_view_refresh(view);
    }
    sim_control_view_t data;
    CHECK(sim_control_read(control, &data) == 0 && data.status.history_count > SIM_STAGE_HISTORY);
    CHECK(strstr(lv_label_get_text(sim_ui_view_widget(view, "stage_detail")), "已被淘汰") != NULL);
    lv_obj_t *choices = sim_ui_view_widget(view, "stage_records");
    CHECK(lv_dropdown_get_option_count(choices) > 2u);
    lv_dropdown_set_selected(choices, 1u);
    CHECK(lv_obj_send_event(choices, LV_EVENT_VALUE_CHANGED, NULL) == LV_RESULT_OK);
    const char *detail = lv_label_get_text(sim_ui_view_widget(view, "stage_detail"));
    CHECK(strstr(detail, "阈值：10 ms") != NULL && strstr(detail, "已被淘汰") == NULL);
    uint32_t first = data.status.history_count - SIM_STAGE_HISTORY;
    for (uint32_t idx = first; idx < data.status.history_count; idx++)
    {
        const sim_transition_t *entry = &data.status.history[idx % SIM_STAGE_HISTORY];
        if (entry->stage == SIM_STAGE_HANDSHAKE && entry->closed)
        {
            CHECK(entry->failed && entry->timeout_origin_ms == entry->entered_ms);
            CHECK(entry->timeout_wait_ms == 10u && !entry->has_error_rx);
        }
    }
    sim_ui_view_destroy(view);
    sim_control_destroy(control);
    sim_session_destroy(session);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3);
    lv_init();
    /* Every translated glyph must resolve to a real bitmap, never a placeholder. */
    FILE *characters = fopen(argv[2], "rb");
    CHECK(characters != NULL);
    char text[4096];
    size_t length = fread(text, 1u, sizeof(text) - 1u, characters);
    CHECK(!ferror(characters) && feof(characters));
    CHECK(fclose(characters) == 0);
    text[length] = '\0';
    for (uint32_t offset = 0u; text[offset] != '\0';)
    {
        uint32_t codepoint = lv_text_encoded_next(text, &offset);
        if (codepoint <= 32u)
        {
            continue;
        }
        lv_font_glyph_dsc_t glyph;
        CHECK(lv_font_get_glyph_dsc(LV_FONT_DEFAULT, &glyph, codepoint, 0u));
        CHECK(!glyph.is_placeholder);
    }
    width = 800u;
    height = 480u;
    pixels = calloc((size_t) width * height, 4u);
    CHECK(pixels != NULL);
    uint8_t *buffer = malloc((size_t) width * height * 4u);
    CHECK(buffer != NULL);
    lv_display_t *display = lv_display_create((int32_t) width, (int32_t) height);
    CHECK(display != NULL);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_buffers(display, buffer, NULL, width * height * 4u, LV_DISPLAY_RENDER_MODE_FULL);
    run_pair(2015u, true, argv[1]);
    run_pair(2023u, true, argv[1]);
    run_pair(2015u, false, argv[1]);
    run_pair(2023u, false, argv[1]);
    failed_flow(argv[1]);
    /* The same UI uses the display's reported dimensions, without a config resolution override. */
    width = 480u;
    height = 272u;
    lv_display_set_resolution(display, (int32_t) width, (int32_t) height);
    run_pair(2015u, true, argv[1]);
    failed_flow(argv[1]);
    repeated_details();
    lv_deinit();
    free(buffer);
    free(pixels);
    puts("PASS: real LVGL widgets/touch, two versions and roles, repeat sessions and responsive dimensions");
    return 0;
}
