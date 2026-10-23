/** @file ui_offscreen_backend.c @brief Test-only display replacement; production still queries fbdev. */
#include "support/ui_display.h"
#include "lvgl/lvgl.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
static void *buffer;
static unsigned starts;
static bool was_running;
void ui_background_priority(void)
{
    struct sched_param parameter = {.sched_priority = 0};
    (void) pthread_setschedparam(pthread_self(), SCHED_OTHER, &parameter);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    (void) area;
    (void) data;
    lv_display_flush_ready(display);
}
static lv_obj_t *find_start(lv_obj_t *object)
{
    if (lv_obj_check_type(object, &lv_button_class))
    {
        lv_obj_t *child = lv_obj_get_child(object, 0);
        if (child != NULL && lv_obj_check_type(child, &lv_label_class) && strcmp(lv_label_get_text(child), "Start charging") == 0)
        {
            return object;
        }
    }
    uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0u; index < count; index++)
    {
        lv_obj_t *found = find_start(lv_obj_get_child(object, (int32_t) index));
        if (found != NULL)
        {
            return found;
        }
    }
    return NULL;
}
static void automate(lv_timer_t *timer)
{
    (void) timer;
    lv_obj_t *button = find_start(lv_screen_active());
    if (button == NULL || starts >= 2u)
    {
        return;
    }
    bool disabled = lv_obj_has_state(button, LV_STATE_DISABLED);
    if (disabled)
    {
        was_running = true;
    }
    else if (starts == 0u || was_running)
    {
        (void) lv_obj_send_event(button, LV_EVENT_CLICKED, NULL);
        starts++;
        was_running = false;
    }
}
int ui_display_open(const ui_display_options_t *options, void **display)
{
    (void) options;
    starts = 0u;
    was_running = false;
    lv_display_t *screen = lv_display_create(800, 480);
    if (screen == NULL)
    {
        return -ENOMEM;
    }
    buffer = malloc(800u * 480u * 4u);
    if (buffer == NULL)
    {
        lv_display_delete(screen);
        return -ENOMEM;
    }
    lv_display_set_color_format(screen, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_flush_cb(screen, flush);
    lv_display_set_buffers(screen, buffer, NULL, 800u * 480u * 4u, LV_DISPLAY_RENDER_MODE_FULL);
    (void) lv_timer_create(automate, 100u, NULL);
    *display = screen;
    return 0;
}
int ui_display_error(void)
{
    return 0;
}
void ui_display_close(void *display)
{
    (void) display;
    lv_deinit();
    free(buffer);
    buffer = NULL;
}
