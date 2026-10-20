/** @file log_sink.c
 * @brief Zephyr skeleton console sink; filesystem output remains unsupported.
 */
#include "support/log_sink.h"
#include "support/console.h"
#include <errno.h>
int log_sink_open(bool file_enabled, const char *path)
{
    (void) path;
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
