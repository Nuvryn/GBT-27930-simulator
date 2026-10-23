/** @file config_store.h @brief Atomic replacement of a validated configuration file. */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H
#include <stddef.h>
int config_store_write(const char *path, const char *text, size_t length);
#endif
