/** @file log_sink.c
 * @brief Zephyr skeleton console sink; filesystem output remains unsupported.
 */
#include "support/log_sink.h"
#include "support/console.h"
#include <errno.h>
int log_sink_start(log_sink_drain_cb_t drain_cb, void *cb_user)
{
    (void) drain_cb;
    (void) cb_user;
    return -ENOSYS;
}
void log_sink_notify(void)
{
}
int log_sink_stop(void)
{
    return 0;
}
void log_sink_lock(void)
{
}
void log_sink_unlock(void)
{
}
void log_sink_fail(int error)
{
    (void) error;
}
const char *log_sink_path(void)
{
    return "";
}
int log_sink_open(bool file_enabled, const char *path, const char *role)
{
    (void) path;
    (void) role;
    return file_enabled ? -ENOSYS : 0;
}
void log_sink_write(const char *data, size_t len)
{
    console_out(data, len);
}
int log_sink_error(void)
{
    return 0;
}
int log_sink_close(void)
{
    console_flush();
    return 0;
}
