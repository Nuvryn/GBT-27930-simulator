/**
 * @file console.c
 * Linux console backend - stdout console + SIGINT/SIGTERM stop hook.
 *
 * Behavior is preserved verbatim from the old board_host.c implementation
 * (src/platform/board/host/board_host.c lines 34-105):
 *   - console_out         == old raw byte output         (fwrite, NULL/len==0 guarded)
 *   - console_printf      == old formatted print         (vsnprintf -> 1024B static buf -> out)
 *   - console_flush       == old console flush           (fflush(stdout))
 *   - console_set_stop_handler    == old stop-handler setter     (SIGINT/SIGTERM forwarding)
 */

#include "support/console.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h> /* SIG_DFL */
#include <string.h>

/* -------------- Console (unified output interface -> stdout) -------------- */

void console_out(const char *data, size_t len)
{
    if (data == NULL || len == 0)
    {
        return;
    }
    (void)fwrite(data, 1, len, stdout);
}

int console_printf(const char *fmt, ...)
{
    static char buf[1024];
    va_list ap;
    int n;

    if (fmt == NULL)
    {
        return 0;
    }
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        console_out(buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf)));
    }
    return n;
}

void console_flush(void)
{
    (void)fflush(stdout);
}

/* -------------- Stop hook (SIGINT/SIGTERM -> callback) -------------- */

static void (*s_stop_fn)(void) = NULL;

static void console_stop_signal_handler(int sig)
{
    (void)sig;
    if (s_stop_fn)
    {
        s_stop_fn();
    }
}

void console_set_stop_handler(void (*fn)(void))
{
    s_stop_fn = fn;
    if (fn)
    {
        /* Register the callback: Ctrl-C (SIGINT) and kill/termination (SIGTERM)
         * are both forwarded. */
        signal(SIGINT, console_stop_signal_handler);
        signal(SIGTERM, console_stop_signal_handler);
    }
    else
    {
        /* NULL = unregister, restore the process default handling */
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
    }
}