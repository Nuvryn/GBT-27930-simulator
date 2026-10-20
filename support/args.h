/**
 * @file args.h
 * Application startup argument contract - the support layer decides "where the application gets its
 * startup parameters" (command line on the Linux host, compile-time built-in defaults on Zephyr).
 *
 * Replaces the legacy app-args contract (the old startup-argument accessor). CLI syntax/usage text/
 * default config path belong to "host platform behavior", implemented by each platform backend:
 *   - backends/support/linux/args.c  : parse the command line (--config/--speed/--help/-h);
 *   - backends/support/zephyr/args.c : no command line, returns the built-in defaults.
 *
 * Return value semantics (args_parse):
 *   0  continue running (args filled in);
 *   1  --help handled (usage printed), the caller should exit with exit code 0;
 *   -1 argument error (error already printed), the caller should exit with exit code 1.
 */

#ifndef SUPPORT_ARGS_H
#define SUPPORT_ARGS_H

/* -------------- startup arguments -------------- */

typedef struct {
    /**
     * config source (passed to config_source_load):
     *   - Linux: config file path (default "config/default.json", given by the linux backend);
     *   - Zephyr: NULL = use the compile-time built-in config (config_source zephyr backend).
     */
    const char *config_src;

    /** speed rate override: >0 overrides the config file speed; 0 = use the configured value. */
    unsigned    speed;
} args_t;

/* -------------- startup argument parsing -------------- */

/**
 * Get the application startup parameters (implemented by the port backend).
 * @param out   output (allocated by the caller; filled in when 0 is returned successfully)
 * @param argc  linux: number of command line arguments; zephyr: 0
 * @param argv  linux: command line argument array; zephyr: NULL
 * @return 0 = continue, 1 = help already printed (caller exits 0), -1 = argument error (caller exits 1)
 */
int args_parse(args_t *out, int argc, char **argv);

#endif /* SUPPORT_ARGS_H */