/** @file sim_log.c @brief Monotonic event records; no hardware or filesystem access. */
#include "logging/sim_log.h"
#include "support/log_sink.h"
#include "elog.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdatomic.h>
#include <errno.h>
static uint32_t s_sim_log_time_ms;
static atomic_size_t s_sim_log_pending;
/** Compiled upstream entry point; checked wrapper prevents silent partial records. */
extern void elog_async_output_upstream(uint8_t level, const char *log, size_t size);
void elog_async_output(uint8_t level, const char *log, size_t size)
{
    /* EasyLogger already owns its output lock while calling this hook. */
    size_t pending = atomic_load(&s_sim_log_pending);
    if (size > ELOG_ASYNC_OUTPUT_BUF_SIZE || pending > ELOG_ASYNC_OUTPUT_BUF_SIZE - size)
    {
        log_sink_fail(-ENOSPC);
        return;
    }
    (void) atomic_fetch_add(&s_sim_log_pending, size);
    elog_async_output_upstream(level, log, size);
}
void elog_async_output_notice(void)
{
    log_sink_notify();
}
static void sim_log_drain(void *cb_user)
{
    (void) cb_user;
    char buffer[4096];
    size_t len;
    while ((len = elog_async_get_log(buffer, sizeof(buffer))) != 0u)
    {
        (void) atomic_fetch_sub(&s_sim_log_pending, len);
        log_sink_write(buffer, len);
    }
}
int sim_log_open(bool file, const char *path)
{
    int rc = log_sink_open(file, path);
    if (rc != 0)
    {
        return rc;
    }
    atomic_store(&s_sim_log_pending, 0u);
    s_sim_log_time_ms = 0u;
    if (elog_init() != ELOG_NO_ERR)
    {
        (void) log_sink_close();
        return -1;
    }
    rc = log_sink_start(sim_log_drain, NULL);
    if (rc != 0)
    {
        elog_deinit();
        (void) log_sink_close();
        return rc;
    }
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_TIME | ELOG_FMT_TAG);
    elog_start();
    if (log_sink_error() != 0)
    {
        int error = log_sink_error();
        elog_stop();
        (void) log_sink_stop();
        elog_deinit();
        (void) log_sink_close();
        return error;
    }
    if (file)
    {
        sim_log_event("LOGFILE", "path=%s", log_sink_path());
    }
    return 0;
}
void sim_log_time(uint32_t now_ms)
{
    s_sim_log_time_ms = now_ms;
}
void sim_log_event(const char *tag, const char *format, ...)
{
    char message[1024];
    va_list args;
    va_start(args, format);
    (void) vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    elog_info(tag, "%s", message);
}
void sim_log_frame(const char *direction, const sim_frame_t *frame)
{
    if (frame == NULL)
    {
        return;
    }
    char bytes[25] = {0};
    size_t pos = 0u;
    for (uint8_t idx = 0u; idx < frame->len && idx < 8u; idx++)
    {
        (void) snprintf(bytes + pos, sizeof(bytes) - pos, "%02X ", (unsigned) frame->data[idx]);
        pos += 3u;
    }
    sim_log_event(direction, "id=%08X len=%u data=%s", (unsigned) frame->id, (unsigned) frame->len, bytes);
}
int sim_log_close(void)
{
    elog_stop();
    (void) log_sink_stop();
    elog_deinit();
    return log_sink_close();
}
/** The producer and ring-draining worker share the EasyLogger output lock. */
ElogErrCode elog_port_init(void)
{
    return ELOG_NO_ERR;
}
void elog_port_deinit(void)
{
}
void elog_port_output(const char *log, size_t size)
{
    log_sink_write(log, size);
}
void elog_port_output_lock(void)
{
    log_sink_lock();
}
void elog_port_output_unlock(void)
{
    log_sink_unlock();
}
const char *elog_port_get_time(void)
{
    static char s_time[32];
    (void) snprintf(s_time, sizeof(s_time), "%u ms", (unsigned) s_sim_log_time_ms);
    return s_time;
}
const char *elog_port_get_p_info(void)
{
    return "simulator";
}
const char *elog_port_get_t_info(void)
{
    return "protocol";
}
