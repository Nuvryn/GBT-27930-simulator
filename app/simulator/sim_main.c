/** @file sim_main.c @brief Simulator lifecycle; one process and protocol thread per role. */
#include "config/sim_config.h"
#include "simulator/sim_engine.h"
#include "logging/sim_log.h"
#include "support/args.h"
#include "support/can_bus.h"
#include "support/can_capture.h"
#include "support/console.h"
#include "support/log_sink.h"
#include "osal/osal.h"
#include <signal.h>
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
/** All resources are initialized once and released in reverse order. */
static int sim_main_run(const args_t *args)
{
    sim_config_t config;
    char error[256];
    if (sim_config_load(args->config_src, &config, error, sizeof(error)) != 0)
    {
        console_printf("%s\n", error);
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
    sim_log_event("START", "role=%s version=%u interface=%s source=%02X peer=%02X speed=%u software_test=%u",
        config.charger ? "charger" : "bms", config.version, config.can_interface, (unsigned) config.source, (unsigned) config.peer, config.speed, config.software_test ? 1u : 0u);
    int rc = can_bus_open(config.can_interface, false);
    if (rc != 0)
    {
        sim_log_event("ERROR", "can_bus_open failed rc=%d", rc);
        (void) sim_log_close();
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
        if (rc != 0)
        {
            sim_log_event("ERROR", "candump startup failed rc=%d path=%s", rc, can_capture_path());
            console_set_stop_handler(NULL);
            can_bus_close();
            (void) sim_log_close();
            return 1;
        }
        sim_log_event("CAPTURE", "started interface=%s path=%s", config.can_interface, can_capture_path());
    }
    sim_engine_t *engine = sim_engine_create(&config, sim_main_send, NULL);
    if (engine == NULL)
    {
        if (config.candump)
        {
            (void) can_capture_stop();
        }
        console_set_stop_handler(NULL);
        can_bus_close();
        (void) sim_log_close();
        return 1;
    }
    uint32_t start_ms = osal_get_tick_ms();
    int result = 0;
    while (result == 0)
    {
        uint32_t now_ms = (uint32_t) (osal_get_tick_ms() - start_ms) * config.speed;
        sim_log_time(now_ms);
        if (s_sim_stop_requested != 0)
        {
            sim_engine_stop(engine, now_ms);
            s_sim_stop_requested = 0;
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
            rc = sim_engine_receive(engine, &frame, now_ms);
            if (rc != 0)
            {
                result = rc;
                break;
            }
        }
        if (result == 0)
        {
            result = sim_engine_tick(engine, now_ms);
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
    sim_log_event("RESULT", "status=%s code=%d", result == 1 ? "PASS" : "FAIL", result);
    console_set_stop_handler(NULL);
    sim_engine_destroy(engine);
    can_bus_close();
    if (sim_log_close() != 0)
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
