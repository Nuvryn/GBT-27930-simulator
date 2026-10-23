/** @file ui_display.c @brief Official fbdev/evdev drivers; no hard-coded display resolution. */
#include "support/ui_display.h"
#include "lvgl/lvgl.h"
#include "libvncserver_port.h"
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
static bool s_input_failed, s_web;
static void input_deleted(lv_event_t *event)
{
    (void) event;
    s_input_failed = true;
}
int ui_display_error(void)
{
    return s_web ? ui_web_error() : (s_input_failed ? -ENODEV : 0);
}
void ui_background_priority(void)
{
    struct sched_param parameters = {.sched_priority = 0};
    (void) pthread_setschedparam(pthread_self(), SCHED_OTHER, &parameters);
}
int ui_display_open(const ui_display_options_t *options, void **display)
{
    *display = NULL;
    s_input_failed = false;
    s_web = options->backend != NULL && strcmp(options->backend, "web") == 0;
    if (s_web)
    {
        return ui_web_open(options, display);
    }
    lv_display_t *screen = lv_linux_fbdev_create();
    if (screen == NULL)
    {
        return -ENOMEM;
    }
    if (lv_linux_fbdev_set_file(screen, options->framebuffer) != LV_RESULT_OK)
    {
        lv_display_delete(screen);
        return -ENODEV;
    }
    lv_display_set_rotation(screen, (lv_display_rotation_t) (options->rotation / 90u));
    int input_fd = open(options->input_device, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (input_fd < 0)
    {
        int rc = -errno;
        lv_display_delete(screen);
        return rc;
    }
    int version;
    if (ioctl(input_fd, EVIOCGVERSION, &version) != 0)
    {
        int rc = -errno;
        (void) close(input_fd);
        lv_display_delete(screen);
        return rc;
    }
    struct input_absinfo x_range, y_range;
    bool has_x = ioctl(input_fd, EVIOCGABS(ABS_X), &x_range) == 0 || ioctl(input_fd, EVIOCGABS(ABS_MT_POSITION_X), &x_range) == 0;
    bool has_y = ioctl(input_fd, EVIOCGABS(ABS_Y), &y_range) == 0 || ioctl(input_fd, EVIOCGABS(ABS_MT_POSITION_Y), &y_range) == 0;
    lv_indev_t *touch = lv_evdev_create_fd(LV_INDEV_TYPE_POINTER, input_fd);
    if (touch == NULL)
    {
        lv_display_delete(screen);
        return -ENODEV;
    }
    if (has_x && has_y)
    {
        lv_evdev_set_calibration(touch, x_range.minimum, y_range.minimum, x_range.maximum, y_range.maximum);
    }
    lv_indev_set_display(touch, screen);
    lv_indev_add_event_cb(touch, input_deleted, LV_EVENT_DELETE, NULL);
    *display = screen;
    return 0;
}
void ui_display_close(void *display)
{
    (void) display;
    if (s_web)
    {
        ui_web_close();
        s_web = false;
        return;
    }
    /* lv_deinit releases input devices first, then displays and backend-owned buffers. */
    lv_deinit();
}
