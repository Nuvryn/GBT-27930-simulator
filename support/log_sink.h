/** @file log_sink.h
 * @brief Single-thread logging sink; platform owns any opened file.
 */
#ifndef SUPPORT_LOG_SINK_H
#define SUPPORT_LOG_SINK_H
#include <stdbool.h>
#include <stddef.h>
/** NULL path for console only. File startup failures return negative errno. */
int log_sink_open(bool file_enabled, const char *path);
/** Synchronous write; records persistent I/O errors. NULL rejected for nonzero length. */
void log_sink_write(const char *data, size_t len);
/** First I/O failure, 0 if healthy. */
int log_sink_error(void);
/** Flush/close; return negative errno on failure. Safe with no open file. */
int log_sink_close(void);
#endif
