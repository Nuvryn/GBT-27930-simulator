/** @file config_store.c @brief File persistence is not implemented on the Zephyr skeleton. */
#include "support/config_store.h"
#include <errno.h>
int config_store_write(const char *path, const char *text, size_t length)
{
    (void) path;
    (void) text;
    (void) length;
    return -ENOSYS;
}
