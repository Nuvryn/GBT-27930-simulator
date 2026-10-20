/**
 * @file args.c
 * Port backend - Linux host: parse the command line (--config/--speed/--help/-h).
 *
 * Preserves the exact CLI behavior of the legacy board_host.c backend (the old
 * startup-argument accessor, lines 109-167): option names, defaults, usage text and
 * return codes are intentionally identical. Only the output sink and the symbol
 * name change:
 *   - formatted print      -> console_printf (support/console.h);
 *   - old startup accessor -> args_parse (support/args.h).
 *
 * Behavior:
 *   - defaults        : config_src = "config/default.json", speed = 0 (use the configured value);
 *   - --help / -h     : print the usage text and return 1 (caller exits with exit code 0);
 *   - --config <file> : next argument becomes config_src;
 *   - --speed <N>     : next argument parsed as unsigned via strtoul;
 *   - any other token : print "Unknown option: %s" + usage text, return -1 (caller exits 1);
 *   - normal parse    : return 0.
 * Sorting is order-sensitive: options are consumed left to right, the last --config/--speed wins,
 * and --help short-circuits the loop from its first occurrence.
 */

#include "support/args.h"
#include "support/console.h"

#include <stddef.h> /* NULL */
#include <stdlib.h> /* strtoul */
#include <string.h>

#define HOST_DEFAULT_CONFIG "config/default.json"

static void print_usage(const char *prog)
{
    console_printf("Usage: %s [OPTIONS]\n", prog);
    console_printf("\n");
    console_printf("Platform Scaffold Demo\n");
    console_printf("\n");
    console_printf("Options:\n");
    console_printf("  --config <file>   Path to configuration file\n");
    console_printf("                     (default: %s)\n", HOST_DEFAULT_CONFIG);
    console_printf("  --speed <N>       Speed multiplier (default: 1)\n");
    console_printf("  --help, -h        Show this help message and exit\n");
    console_printf("\n");
    console_printf("Examples:\n");
    console_printf("  %s\n", prog);
    console_printf("  %s --config config/default.json\n", prog);
    console_printf("  %s --config config/default.json --speed 2\n", prog);
}

int args_parse(args_t *out, int argc, char **argv)
{
    if (!out)
    {
        return -1;
    }

    /* Defaults: default config file; speed=0 = use the configured value */
    out->config_src = HOST_DEFAULT_CONFIG;
    out->speed      = 0;

    if (argc > 0 && argv != NULL)
    {
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            {
                print_usage(argv[0]);
                return 1; /* help handled, caller exits with exit code 0 */
            }
            else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            {
                out->config_src = argv[++i];
            }
            else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc)
            {
                out->speed = (unsigned)strtoul(argv[++i], NULL, 10);
            }
            else
            {
                console_printf("Unknown option: %s\n", argv[i]);
                print_usage(argv[0]);
                return -1; /* argument error, caller exits with exit code 1 */
            }
        }
    }
    return 0;
}