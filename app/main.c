/**
 * @file main.c
 * Minimal scaffold demo - proves the whole stack builds and runs on both
 * targets without any project-domain code.
 *
 * It exercises the three contract axes:
 *   support : args_parse (startup args) + console_* (output) + config_source_*
 *   hal     : hal_can_get_bus (device discovery; may be NULL on a skeleton)
 *   osal    : osal_delay_ms
 *
 * Portability rule (see docs/architecture-spec.md):
 *   No platform headers, no `#ifdef PLATFORM` for behaviour. The ONLY branch on
 *   the platform is the entry-point signature below (host main(argc,argv) vs
 *   Zephyr main(void)); that is the one thing the support layer cannot abstract.
 */

#include <stddef.h>
#include <string.h>

#include "support/args.h"
#include "support/console.h"
#include "support/config_source.h"
#include "hal/hal_dev.h"
#include "osal/osal.h"

#define DEMO_CAN_IF "can0"
#define DEMO_TICKS 5u
#define DEMO_TICK_MS 200u

static int run_demo(const args_t *args)
{
    config_source_t cfg;
    hal_can_bus_t can;
    unsigned int i;

    /* --- support: load config text (host: file; zephyr: built-in default) --- */
    memset(&cfg, 0, sizeof(cfg));
    if (config_source_load(&cfg, args->config_src) != 0)
    {
        console_printf("config_source_load failed (src=%s)\n",
            args->config_src ? args->config_src : "(built-in)");
        return 1;
    }
    console_printf("config loaded: %u bytes\n", (unsigned) strlen(cfg.text));
    config_source_free(&cfg);

    /* --- hal: device discovery. NULL means "not present" on a skeleton. --- */
    can = hal_can_get_bus(DEMO_CAN_IF);
    console_printf("hal_can_get_bus(\"%s\") -> %s\n", DEMO_CAN_IF,
        can ? "ready" : "NULL (device not present)");

    /* --- osal: a timed loop. --- */
    for (i = 0; i < DEMO_TICKS; i++)
    {
        console_printf("tick %u/%u\n", i + 1u, DEMO_TICKS);
        osal_delay_ms(DEMO_TICK_MS);
    }

    console_printf("done.\n");
    console_flush();
    return 0;
}

#if !defined(PLATFORM_ZEPHYR)
int main(int argc, char *argv[])
{
    args_t args;
    int rc = args_parse(&args, argc, argv);

    if (rc == 1)
        return 0; /* --help already printed */
    if (rc < 0)
        return 1; /* argument error already printed */

    return run_demo(&args);
}
#else
int main(void)
{
    args_t args;

    if (args_parse(&args, 0, NULL) != 0)
        return 1;
    return run_demo(&args);
}
#endif
