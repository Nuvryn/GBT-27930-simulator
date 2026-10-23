/** @file ui_display.h @brief Platform LVGL display/input construction; UI thread only. */
#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H
typedef struct
{
    const char *backend;
    const char *listen;
    unsigned web_port, vnc_port, width, height, fps;
    const char *framebuffer;
    const char *input_device;
    unsigned rotation;
} ui_display_options_t;
/* Result is an opaque LVGL display pointer. fbdev reads system dimensions; web uses configured virtual dimensions. */
void ui_background_priority(void);
int ui_display_open(const ui_display_options_t *options, void **display);
int ui_display_error(void);
void ui_display_close(void *display);
#endif
