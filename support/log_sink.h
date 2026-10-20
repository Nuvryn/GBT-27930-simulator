/** @file log_sink.h
 * @brief Logging worker and sink; platform owns thread and file lifetimes.
 */
#ifndef SUPPORT_LOG_SINK_H
#define SUPPORT_LOG_SINK_H
#include <stdbool.h>
#include <stddef.h>
typedef void (*log_sink_drain_cb_t)(void *cb_user);
/** Empty/NULL path selects logs/<local timestamp>_<pid>.log. Explicit paths override. */
int log_sink_open(bool file_enabled, const char *path);
/** Start one worker; callback drains upstream records. NULL callback rejected. */
int log_sink_start(log_sink_drain_cb_t drain_cb, void *cb_user);
/** Notify worker without waiting for I/O. */
void log_sink_notify(void);
/** Stop producer first. Wake, drain remaining records, and join worker. */
int log_sink_stop(void);
/** EasyLogger formatting/ring lock; worker releases it before I/O. */
void log_sink_lock(void);
void log_sink_unlock(void);
/** Latch first negative error, including -ENOSPC when the log ring is full. */
void log_sink_fail(int error);
/** Actual file name, empty when disabled; immutable until close. */
const char *log_sink_path(void);
/** Worker-only write; records persistent I/O errors. No producer-side file access. */
void log_sink_write(const char *data, size_t len);
/** First I/O failure, 0 if healthy. */
int log_sink_error(void);
/** Flush/close; return negative errno on failure. Safe with no open file. */
int log_sink_close(void);
#endif
