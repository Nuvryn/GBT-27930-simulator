/** @file log_sink.c
 * @brief Linux console/file logging. File lifetime belongs to this backend.
 */
#include "support/log_sink.h"
#include <errno.h>
#include <stdio.h>
static FILE *s_log_sink_file;
static int s_log_sink_error;
int log_sink_open(bool file_enabled, const char *path)
{
    if (s_log_sink_file != NULL)
    {
        return -EBUSY;
    }
    s_log_sink_error = 0;
    if (file_enabled)
    {
        if (path == NULL || path[0] == '\0')
        {
            return -EINVAL;
        }
        s_log_sink_file = fopen(path, "wb");
        if (s_log_sink_file == NULL)
        {
            return -errno;
        }
    }
    return 0;
}
void log_sink_write(const char *data, size_t len)
{
    if (data == NULL && len != 0u)
    {
        s_log_sink_error = -EINVAL;
        return;
    }
    if (fwrite(data, 1u, len, stdout) != len || fflush(stdout) != 0)
    {
        s_log_sink_error = -EIO;
    }
    if (s_log_sink_file != NULL && (fwrite(data, 1u, len, s_log_sink_file) != len || fflush(s_log_sink_file) != 0))
    {
        s_log_sink_error = -EIO;
    }
}
int log_sink_error(void)
{
    return s_log_sink_error;
}
int log_sink_close(void)
{
    if (s_log_sink_file != NULL)
    {
        if (fclose(s_log_sink_file) != 0)
        {
            s_log_sink_error = -EIO;
        }
        s_log_sink_file = NULL;
    }
    return s_log_sink_error;
}
