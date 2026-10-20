/**
 * @file config_source.c
 * Zephyr backend - configuration source: self-contained built-in default.
 *
 * The scaffold deliberately ships NO code-generation step: this backend returns
 * a compile-time built-in default string so the Zephyr skeleton compiles without
 * a Zephyr SDK or an injected generated header. Replace BUILTIN_DEFAULT with the
 * project's default config, or back it with settings/flash.
 *
 * Ownership contract (see support/config_source.h): config_source_load always
 * returns a malloc copy (out->owns == true); never a static literal pointer.
 */

#include <stdlib.h>
#include <string.h>

#include "support/config_source.h"

/* Template placeholder - swap in your project's default config text. */
static const char BUILTIN_DEFAULT[] =
    "{\n"
    "    \"demo\": true\n"
    "}\n";

int config_source_load(config_source_t *out, const char *src)
{
    (void) src; /* built-in backend ignores the source selector */

    if (!out)
        return -1;

    /* Must strdup: the contract forbids handing back a static literal. */
    out->text = strdup(BUILTIN_DEFAULT);
    if (!out->text)
        return -1;

    out->owns = true;
    return 0;
}

void config_source_free(config_source_t *cfg)
{
    if (!cfg)
        return;

    if (cfg->owns && cfg->text)
    {
        free((void *) cfg->text);
        cfg->text = NULL;
        cfg->owns = false;
    }
}