/** @file test_ui_web_host.c @brief Actual display backend without CAN hardware for network/cleanup testing. */
#include "ui/sim_ui_view.h"
#include "support/ui_display.h"
#include "osal/osal.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
static volatile sig_atomic_t s_host_stop;
static void host_signal(int signum)
{
    (void) signum;
    s_host_stop = 1;
}
static int host_send(void *user, const sim_frame_t *frame)
{
    (void) user;
    (void) frame;
    return 0;
}
int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6)
    {
        return 2;
    }
    (void) setvbuf(stdout, NULL, _IOLBF, 0);
    (void) signal(SIGTERM, host_signal);
    (void) signal(SIGINT, host_signal);
    sim_config_t config;
    sim_config_defaults(&config);
    config.manual_start = true;
    config.ui_enabled = true;
    config.recovery_wait_ms = 10u;
    config.aux_power_off_delay_ms = 1u;
    config.max_session_ms = 5000u;
    for (unsigned idx = 0u; idx < GBT27930_MESSAGE_COUNT; idx++)
    {
        config.first_timeout_ms[idx] = 500u;
        config.timeout_ms[idx] = 500u;
    }
    lv_init();
    ui_display_options_t options = {.backend = "web", .listen = argc == 6 ? argv[5] : "127.0.0.1", .web_port = (unsigned) strtoul(argv[1], NULL, 10), .vnc_port = (unsigned) strtoul(argv[2], NULL, 10), .width = (unsigned) strtoul(argv[3], NULL, 10), .height = (unsigned) strtoul(argv[4], NULL, 10), .fps = 10u};
    void *display = NULL;
    int result = ui_display_open(&options, &display);
    if (result != 0)
    {
        fprintf(stderr, "OPEN_FAILED %d\n", result);
        ui_display_close(display);
        return 1;
    }
    sim_control_t *control = sim_control_create(&config, NULL);
    sim_session_t *session = sim_session_create(&config, host_send, NULL);
    if (control == NULL || session == NULL)
    {
        ui_display_close(display);
        sim_control_destroy(control);
        sim_session_destroy(session);
        return 1;
    }
    sim_ui_view_t *view = sim_ui_view_create(control, NULL);
    if (view == NULL)
    {
        ui_display_close(display);
        sim_control_destroy(control);
        sim_session_destroy(session);
        return 1;
    }
    lv_tabview_set_active(sim_ui_view_widget(view, "tabs"), 2u, LV_ANIM_OFF);
    lv_obj_update_layout(lv_screen_active());
    const char *targets[] = {"start", "stop"};
    for (unsigned idx = 0u; idx < 2u; idx++)
    {
        lv_area_t area;
        lv_obj_get_coords(sim_ui_view_widget(view, targets[idx]), &area);
        printf("TARGET %s %d %d\n", targets[idx], (int) (area.x1 + area.x2) / 2, (int) (area.y1 + area.y2) / 2);
    }
    puts("READY");
    uint32_t start = osal_get_tick_ms(), last = start, refresh = start;
    unsigned previous_session = UINT32_MAX;
    bool previous_active = false;
    lv_indev_state_t previous_pointer = LV_INDEV_STATE_PRESSED;
    while (!s_host_stop && osal_get_tick_ms() - start < 20000u)
    {
        uint32_t now = osal_get_tick_ms();
        lv_tick_inc(now - last);
        last = now;
        (void) sim_control_step(control, session, now);
        (void) sim_session_tick(session, now);
        (void) sim_control_publish(control, session);
        if (now - refresh >= 100u)
        {
            sim_ui_view_refresh(view);
            refresh = now;
        }
        (void) lv_timer_handler();
        sim_status_t status;
        sim_session_status(session, &status);
        lv_indev_state_t pointer = lv_indev_get_state(lv_indev_get_next(NULL));
        if (status.session_id != previous_session || status.active != previous_active || pointer != previous_pointer)
        {
            printf("STATUS session=%u active=%u pressed=%u\n", status.session_id, status.active ? 1u : 0u, pointer == LV_INDEV_STATE_PRESSED ? 1u : 0u);
            previous_session = status.session_id;
            previous_active = status.active;
            previous_pointer = pointer;
        }
        if (ui_display_error() != 0)
        {
            result = 1;
            break;
        }
        osal_delay_ms(1u);
    }
    sim_ui_view_destroy(view);
    ui_display_close(display);
    sim_control_destroy(control);
    sim_session_destroy(session);
    return result;
}
