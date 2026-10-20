/**
 * @file console.h
 * Project console contract (supports the framework-port architecture).
 *
 * This replaces the old board-level console + stop-hook role (the legacy
 * host backend's raw byte output / formatted print / console flush /
 * stop-handler setter).
 *
 * Backends:
 *   - backends/support/linux/console.c   real: stdout (fwrite/vsnprintf/fflush) + SIGINT/SIGTERM
 *   - backends/support/zephyr/console.c  skeleton: drops bytes and counts them; stop hook stored,
 *                                     never fired (same shape as the old MCU-board skeleton).
 *
 * The contract is implemented on the project support layer; it depends only on
 * libc headers, never on a specific OS.
 */

#ifndef SUPPORT_CONSOLE_H
#define SUPPORT_CONSOLE_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write @p len bytes to the console output stream.
 * @param data Byte buffer to write. A NULL pointer with a non-zero @p len is a
 *             guarded no-op (never dereferenced).
 */
void console_out(const char *data, size_t len);

/**
 * @brief Formatted console output (printf-style).
 * @return The printf-style count (number of characters that would have been
 *         written), or 0 when @p fmt is NULL. Truncation and negative return
 *         semantics follow vsnprintf.
 */
int  console_printf(const char *fmt, ...);

/** @brief Flush the console output stream (fflush(stdout) on host). */
void console_flush(void);

/**
 * @brief Register a board-level "stop" hook (Ctrl-C / termination).
 * @param fn Callback invoked on SIGINT/SIGTERM in the host backend; NULL
 *           restores the default signal handling. The zephyr skeleton only
 *           stores the callback and never fires it.
 */
void console_set_stop_handler(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* SUPPORT_CONSOLE_H */