/*
 * SKELETON — see .omo/plans/framework-port.md
 * Zephyr backend of the support layer: compilable stub, not a real implementation.
 */
/**
 * @file args.c
 * Port backend - Zephyr skeleton: no command line, returns the built-in defaults.
 *
 * In this project's design a bare Zephyr main(void) carries no argv/argc, so startup arguments
 * always come from the compile-time built-in config: config_src = NULL (use the compiled-in
 * config), speed = 0 (use the configured value), return 0.
 *
 * A non-NULL argv / argc > 0 is not expected; it is NOT silently swallowed - every such call is
 * counted as an "unimplemented" marker (see s_unimplemented_argv_calls), keeping the skeleton
 * observable under review.
 *
 * MUST NOT include any <zephyr/...> header: this file is compile-checked on the host via the
 * mock-shim include paths, with no Zephyr SDK dependency.
 */

#include "support/args.h"

#include <stddef.h> /* NULL */

/* Number of args_parse calls made with a non-empty argv on a platform with no CLI.
 * File-local marker for "unimplemented" behavior: incremented, but NOT externally observable. */
static unsigned s_unimplemented_argv_calls = 0;

int args_parse(args_t *out, int argc, char **argv)
{
    ((void) argv); /* no argv on a bare Zephyr main - ignored only after counting the marker */

    if (!out)
    {
        return -1;
    }

    if (argc > 0)
    {
        s_unimplemented_argv_calls++;
    }

    /* Built-in defaults: use the compile-time built-in config, speed stays at the configured value */
    out->config_src = NULL;
    out->speed = 0;
    out->show_version = false;

    return 0;
}
