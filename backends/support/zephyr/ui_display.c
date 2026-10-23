/** @file ui_display.c @brief Zephyr display contract skeleton, without LVGL or device support. */
#include "support/ui_display.h"
#include <errno.h>
void ui_background_priority(void)
{
}
int ui_display_open(const ui_display_options_t *options, void **display)
{
    (void) options;
    *display = 0;
    return -ENOSYS;
}
int ui_display_error(void)
{
    return -ENOSYS;
}
void ui_display_close(void *display)
{
    (void) display;
}
