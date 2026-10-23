/** @file log_retention.c @brief Filesystem retention is unsupported on the Zephyr skeleton. */
#include "support/log_retention.h"
#include <errno.h>
#include <stddef.h>

int log_retention_track(const char *role, const char *application_path, const char *capture_path)
{
    (void) role;
    if ((application_path != NULL && application_path[0] != '\0') || (capture_path != NULL && capture_path[0] != '\0'))
    {
        return -ENOSYS;
    }
    return 0;
}
void log_retention_close(void)
{
}
