/** @file can_capture.c @brief External process capture is unsupported on Zephyr. */
#include "support/can_capture.h"
#include <errno.h>
int can_capture_start(const char *interface_name, const char *path)
{
    (void) interface_name;
    (void) path;
    return -ENOSYS;
}
int can_capture_error(void)
{
    return 0;
}
const char *can_capture_path(void)
{
    return "";
}
int can_capture_stop(void)
{
    return 0;
}
