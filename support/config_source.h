/**
 * @file config_source.h
 * Project supplementary layer - configuration source contract header.
 *
 * Ownership contract (the single rule source for the whole repo):
 *   config_source_load() always returns a malloc copy: out->text points to a heap-allocated
 *   string buffer, out->owns is always true. The caller is responsible for releasing it
 *   uniformly with config_source_free() (regardless of the backend).
 *
 *   Backends are forbidden from directly returning a static literal pointer (e.g. "{\"role\":...\"}"), because
 *   the caller cannot free it with uniform logic - some sources require free and some do not, which inevitably leaks.
 *
 * src semantics:
 *   - file backend (backends/support/linux/config_source.c): src is the config file path
 *     (e.g. "config/default.json"); src==NULL returns -1 (the file backend cannot decide the built-in default)
 *   - compile-time built-in backend (backends/support/zephyr/config_source.c): src is usually NULL,
 *     meaning "compile-time built-in default"; even non-NULL returns the same version of the
 *     built-in JSON - the built-in default is fixed at compile time, paths are not parsed
 *   - all failures return -1 with out content undefined (the caller must not free an out that failed to open)
 *
 * Typical usage:
 *   config_source_t cfg;
 *   if (config_source_load(&cfg, filepath) != 0) { ... handle the error ... }
 *   // use cfg.text (cJSON_Parse / custom)
 *   config_source_free(&cfg);
 *   // afterwards cfg.text==NULL, cfg.owns==false
 */

#ifndef SUPPORT_CONFIG_SOURCE_H
#define SUPPORT_CONFIG_SOURCE_H

#include <stdbool.h>

/* -------------- config source handle -------------- */

typedef struct {
    const char *text;   /**< config JSON text (malloc copy, released by free) */
    bool        owns;   /**< always true: the caller is responsible for free(text) */
} config_source_t;

/* -------------- config source API -------------- */

/**
 * Load a config source.
 * @param out  output handle (allocated by the caller, owns=true after a successful load)
 * @param src  config source identifier (file path / NULL=built-in default)
 * @return 0 on success, -1 on failure (out content undefined)
 */
int  config_source_load(config_source_t *out, const char *src);

/**
 * Release a config source.
 * If cfg->owns is true (always true), release cfg->text and set it to NULL.
 * Idempotent: may be called repeatedly (the second call skips free when text==NULL).
 */
void config_source_free(config_source_t *cfg);

#endif /* SUPPORT_CONFIG_SOURCE_H */