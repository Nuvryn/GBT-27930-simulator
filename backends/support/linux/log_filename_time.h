/** @file log_filename_time.h @brief Fixed UTC+8 file timestamps without process-wide TZ changes. */
#ifndef BACKENDS_SUPPORT_LINUX_LOG_FILENAME_TIME_H
#define BACKENDS_SUPPORT_LINUX_LOG_FILENAME_TIME_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/** Format Unix seconds as YYYYMMDD_HHMMSS in modern Shanghai time (UTC+8).
 * out_stamp must be non-NULL; buffer_capacity includes the terminating NUL.
 * No zoneinfo database is needed. Historical daylight-saving rules are not applied.
 * Linux targets use signed 32-bit or 64-bit time_t; narrowing is range-checked.
 */
static inline int log_filename_time_format(time_t seconds, char *out_stamp, size_t buffer_capacity)
{
    _Static_assert(sizeof(time_t) == sizeof(int32_t) || sizeof(time_t) == sizeof(int64_t), "Unsupported Linux time_t");
    if (out_stamp == NULL || buffer_capacity == 0u)
    {
        return -EINVAL;
    }
    out_stamp[0] = '\0';
    const int64_t offset_seconds = 8 * 60 * 60;
    const int64_t maximum_seconds = sizeof(time_t) == sizeof(int32_t) ? INT32_MAX : INT64_MAX;
    int64_t epoch_seconds = (int64_t) seconds;
    if (epoch_seconds > maximum_seconds - offset_seconds)
    {
        return -EOVERFLOW;
    }
    time_t shanghai_seconds = (time_t) (epoch_seconds + offset_seconds);
    struct tm shanghai_time;
    if (gmtime_r(&shanghai_seconds, &shanghai_time) == NULL)
    {
        return -EOVERFLOW;
    }
    if (strftime(out_stamp, buffer_capacity, "%Y%m%d_%H%M%S", &shanghai_time) == 0u)
    {
        out_stamp[0] = '\0';
        return -ENOSPC;
    }
    return 0;
}

#endif
