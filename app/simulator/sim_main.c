/** @file sim_main.c @brief Simulator lifecycle; one process and protocol thread per role. */
#include "config/sim_config.h"
#include "ui/sim_ui.h"
#include "simulator/sim_session.h"
#include "simulator/sim_build_info.h"
#include "logging/sim_log.h"
#include "support/args.h"
#include "support/can_bus.h"
#include "support/can_capture.h"
#include "support/console.h"
#include "support/log_sink.h"
#include "support/log_retention.h"
#include "osal/osal.h"
#include <signal.h>
#include <errno.h>
#include <stdio.h>

/* Only a signal notification; no thread synchronization uses volatile. */
static volatile sig_atomic_t s_sim_stop_requested;
static void sim_main_stop(void)
{
    s_sim_stop_requested = 1;
}
static int sim_main_send(void *user, const sim_frame_t *frame)
{
    (void) user;
    int rc = can_bus_send(frame->id, frame->data, frame->len);
    sim_log_frame(rc == 0 ? "TX" : "TX_ERROR", frame);
    return rc;
}
static int sim_main_log_close(void)
{
    int rc = sim_log_close();
    log_retention_close();
    return rc;
}
static void sim_main_version(void)
{
    const sim_build_info_t *info = sim_build_info_get();
    console_printf("GB/T 27930 Simulator v%s\nBuild: %s\nTarget: %s\n", info->version, info->time_utc, info->target);
}
static int sim_main_config_record(const char *data, size_t len, void *cb_user)
{
    unsigned *part = cb_user;
    (*part)++;
    return sim_log_detail("CONFIG part=%u data=%.*s", *part, (int) len, data);
}
static int sim_main_startup(const sim_config_t *config, const args_t *args)
{
    const sim_build_info_t *info = sim_build_info_get();
    int rc = sim_log_detail("software_version=%s built_utc=%s target=%s", info->version, info->time_utc, info->target);
    if (rc == 0)
    {
        rc = sim_log_detail("config_source=%s speed_override=%u", args->config_src != NULL ? args->config_src : "builtin", args->speed);
    }
    if (rc == 0)
    {
        rc = sim_log_detail("compiler=%s", info->compiler);
    }
    if (rc == 0)
    {
        rc = sim_log_detail("CONFIG_BEGIN effective=true defaults_included=true");
    }
    unsigned part = 0u;
    if (rc == 0)
    {
        rc = sim_config_snapshot(config, sim_main_config_record, &part);
    }
    if (rc == 0)
    {
        rc = sim_log_detail("CONFIG_END parts=%u", part);
    }
    return rc;
}
static void sim_main_idle(bool charger, bool ui)
{
    sim_log_event("IDLE", "%s", charger ? (ui ? "Use Start charging on screen" : "Press Enter to start charging") : "Waiting for peer CHM");
    console_flush();
}
/** All resources are initialized once and released in reverse order. */
static int sim_main_run(const args_t *args)
{
    sim_main_version();
    console_printf("Config: %s\n", args->config_src != NULL ? args->config_src : "builtin");
    sim_config_t config;
    char error[256];
    if (sim_config_load(args->config_src, &config, error, sizeof(error)) != 0)
    {
        console_printf("%s\n", error);
        return 1;
    }
    if (config.ui_enabled && !sim_ui_available())
    {
        console_printf("UI requested but SIM_UI is disabled in this build\n");
        return 1;
    }
    if (args->speed != 0u)
    {
        config.speed = args->speed;
    }
    if (config.speed > 100u || config.speed == 0u || (config.speed != 1u && !config.software_test))
    {
        console_printf("speed requires software_test=true and 1..100\n");
        return 1;
    }
    if (sim_log_open(config.file_log, config.log_path, config.charger ? "charger" : "bms") != 0)
    {
        console_printf("log initialization failed\n");
        return 1;
    }
    sim_log_event("START", "role=%s version=%u interface=%s source=%02X peer=%02X speed=%u software_test=%u start_mode=%s",
        config.charger ? "charger" : "bms", config.version, config.can_interface, (unsigned) config.source, (unsigned) config.peer, config.speed, config.software_test ? 1u : 0u, config.manual_start ? "manual" : "auto");
    const char *role = config.charger ? "charger" : "bms";
    const char *automatic_log = config.file_log && config.log_path[0] == '\0' ? log_sink_path() : NULL;
    int rc = sim_main_startup(&config, args);
    if (rc != 0)
    {
        sim_log_event("ERROR", "startup information failed rc=%d", rc);
        (void) sim_main_log_close();
        return 1;
    }
    rc = log_retention_track(role, automatic_log, NULL);
    if (rc != 0)
    {
        sim_log_event("ERROR", "log retention failed rc=%d", rc);
        (void) sim_main_log_close();
        return 1;
    }
    rc = can_bus_open(config.can_interface, false);
    if (rc != 0)
    {
        sim_log_event("ERROR", "can_bus_open failed rc=%d", rc);
        (void) sim_main_log_close();
        return 1;
    }
    console_set_stop_handler(sim_main_stop);
    if (config.candump)
    {
        char capture_path[512];
        const char *path = config.candump_path;
        if (path[0] == '\0' && log_sink_path()[0] != '\0')
        {
            (void) snprintf(capture_path, sizeof(capture_path), "%s.candump.log", log_sink_path());
            path = capture_path;
        }
        rc = can_capture_start(config.can_interface, path, config.charger ? "charger" : "bms");
        if (rc == 0 && config.candump_path[0] == '\0' && (!config.file_log || automatic_log != NULL))
        {
            rc = log_retention_track(role, automatic_log, can_capture_path());
            if (rc != 0)
            {
                sim_log_event("ERROR", "capture retention failed rc=%d", rc);
                (void) can_capture_stop();
            }
        }
        if (rc != 0)
        {
            sim_log_event("ERROR", "candump startup failed rc=%d path=%s", rc, can_capture_path());
            console_set_stop_handler(NULL);
            can_bus_close();
            (void) sim_main_log_close();
            return 1;
        }
        sim_log_event("CAPTURE", "started interface=%s path=%s", config.can_interface, can_capture_path());
    }
    sim_session_t *session = sim_session_create(&config, sim_main_send, NULL);
    if (session == NULL)
    {
        if (config.candump)
        {
            (void) can_capture_stop();
        }
        console_set_stop_handler(NULL);
        can_bus_close();
        (void) sim_main_log_close();
        return 1;
    }
    sim_control_t *control = config.ui_enabled ? sim_control_create(&config, args->config_src) : NULL;
    sim_ui_t *ui = NULL;
    rc = config.ui_enabled && control == NULL ? -ENOMEM : sim_ui_open(control, &config, args->config_src, &ui);
    if (rc != 0)
    {
        sim_log_event("ERROR", "UI initialization failed rc=%d", rc);
        sim_control_destroy(control);
        sim_session_destroy(session);
        if (config.candump)
        {
            (void) can_capture_stop();
        }
        console_set_stop_handler(NULL);
        can_bus_close();
        (void) sim_main_log_close();
        return 1;
    }
    sim_log_observe(control != NULL ? sim_control_log : NULL, control);
    uint32_t start_ms = osal_get_tick_ms();
    int result = config.manual_start ? 0 : sim_session_start(session, 0u);
    bool exiting = false, failed = false;
    if (result == 0)
    {
        sim_log_event("READY", "Startup complete");
        if (config.manual_start)
        {
            sim_main_idle(config.charger, config.ui_enabled);
        }
    }
    while (result == 0)
    {
        uint32_t now_ms = (uint32_t) (osal_get_tick_ms() - start_ms) * config.speed;
        sim_log_time(now_ms);
        if (s_sim_stop_requested != 0)
        {
            exiting = !sim_session_active(session) || console_termination_requested();
            sim_session_stop(session, now_ms);
            s_sim_stop_requested = 0;
        }
        if (exiting && !sim_session_active(session))
        {
            result = failed ? -1 : 1;
            break;
        }
        if (config.manual_start && config.charger && !config.ui_enabled && !exiting)
        {
            int input = console_input_poll();
            if (input < 0 || input == 2)
            {
                sim_log_event(input < 0 ? "ERROR" : "INPUT", "stdin %s code=%d", input < 0 ? "failed" : "closed", input);
                if (input < 0)
                {
                    result = input;
                    break;
                }
                exiting = true;
                sim_session_stop(session, now_ms);
                if (!sim_session_active(session))
                {
                    result = failed ? -1 : 1;
                    break;
                }
            }
            else if (input == 1 && !sim_session_active(session))
            {
                result = sim_session_start(session, now_ms);
                if (result != 0)
                {
                    break;
                }
            }
        }
        if (!exiting)
        {
            result = sim_control_step(control, session, now_ms);
        }
        if (result != 0)
        {
            break;
        }
        for (unsigned drained = 0u; drained < 128u; drained++)
        {
            sim_frame_t frame;
            rc = can_bus_recv(&frame.id, frame.data, &frame.len);
            if (rc == 0)
            {
                break;
            }
            if (rc < 0)
            {
                result = rc;
                break;
            }
            sim_log_frame("RX", &frame);
            rc = sim_session_receive(session, &frame, now_ms);
            if (rc != 0)
            {
                result = rc;
                break;
            }
        }
        if (result == 0)
        {
            rc = sim_session_tick(session, now_ms);
            if (rc != 0)
            {
                failed = failed || rc < 0;
                if (!config.manual_start || exiting)
                {
                    result = failed ? (rc < 0 ? rc : -1) : 1;
                }
                else
                {
                    sim_main_idle(config.charger, config.ui_enabled);
                }
            }
        }
        rc = sim_control_publish(control, session);
        if (rc != 0 || sim_ui_error(ui) != 0 || sim_control_error(control) != 0)
        {
            result = -1;
            sim_log_event("ERROR", "UI or configuration worker failed rc=%d", rc);
        }
        if (sim_session_io_error(session) != 0)
        {
            result = sim_session_io_error(session);
            sim_log_event("ERROR", "CAN send failed rc=%d", result);
        }
        if (log_sink_error() != 0 || can_bus_drop_count() != 0u)
        {
            result = -1;
            sim_log_event("ERROR", "log sink or CAN ring lost data");
        }
        if (config.candump && can_capture_error() != 0)
        {
            result = -1;
            sim_log_event("ERROR", "candump runtime failed rc=%d", can_capture_error());
        }
        if (result == 0)
        {
            osal_delay_ms(1u);
        }
    }
    if (config.candump)
    {
        rc = can_capture_stop();
        sim_log_event("CAPTURE", "stopped rc=%d path=%s", rc, can_capture_path());
        if (rc != 0)
        {
            result = -1;
        }
    }
    sim_ui_close(ui);
    if (sim_control_error(control) != 0)
    {
        result = -1;
    }
    sim_log_event("RESULT", "status=%s code=%d", result == 1 ? "PASS" : "FAIL", result);
    sim_log_observe(NULL, NULL);
    sim_control_destroy(control);
    console_set_stop_handler(NULL);
    sim_session_destroy(session);
    can_bus_close();
    if (sim_main_log_close() != 0)
    {
        return 1;
    }
    return result == 1 ? 0 : 1;
}
#ifndef PLATFORM_ZEPHYR
int main(int argc, char **argv)
{
    args_t args;
    int rc = args_parse(&args, argc, argv);
    if (rc == 1)
    {
        return 0;
    }
    if (rc != 0)
    {
        return 1;
    }
    if (args.show_version)
    {
        sim_main_version();
        return 0;
    }
    return sim_main_run(&args);
}
#else
int main(void)
{
    args_t args;
    int rc = args_parse(&args, 0, NULL);
    if (rc != 0)
    {
        return 1;
    }
    return sim_main_run(&args);
}
#endif
