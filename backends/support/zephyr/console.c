/*
 * SKELETON — see .omo/plans/framework-port.md
 *
 * @file console.c
 * Zephyr console backend - SKELETON.
 *
 * Same shape as the old MCU-board console skeleton:
 * the contract is implemented but the platform hook is NOT wired up yet, so
 * nothing is silently "successful":
 *   - console_out         drops bytes and counts them (unimplemented counter)
 *   - console_printf      still vsnprintf's (real formatting), then drops + counts
 *   - console_flush       counting no-op
 *   - console_set_stop_handler    stores the callback, never fires it
 *
 * This file deliberately includes NO <zephyr/...> header: it compiles with
 * plain libc (<stdio.h>/<stdarg.h>/<stdint.h>) plus the support contract.
 */

#include "support/console.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* File-local diagnostic counters for the unimplemented backend. NOT externally
 * observable - the contract-visible behaviour is that bytes are dropped. */
static uint32_t s_unimpl_out_count   = 0;
static uint32_t s_unimpl_flush_count = 0;

/* Stop callback storage (trigger source TODO) */
static void (*s_stop_fn)(void) = NULL;

/* -------------- Console (unified output interface -> UART stub) -------------- */

void console_out(const char *data, size_t len)
{
    /* TODO: wire up the target console (UART/USB-CDC) via hal_uart.
     *       Skeleton: bytes are dropped but the drop is counted. */
    (void)data;
    (void)len;
    s_unimpl_out_count++;
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
    /* vsnprintf formatting is real (identical to the host backend); only the
     * delivery is a stub - the formatted line is dropped and counted. */
    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        s_unimpl_out_count++;
    }
    return n;
}

void console_flush(void)
{
    /* TODO: flush the UART TX FIFO (wait for the TX-complete flag). A
     *       bufferless backend may leave this empty; count it for now. */
    s_unimpl_flush_count++;
}

/* -------------- stop hook (stored, never fired) -------------- */

void console_set_stop_handler(void (*fn)(void))
{
    /* TODO: connect an interrupt/watchdog/exception callback - on real hardware
     *       the ISR or a main-loop detection point calls s_stop_fn() to trigger
     *       engine cleanup. The skeleton only stores the callback; nobody fires it. */
    s_stop_fn = fn;
}