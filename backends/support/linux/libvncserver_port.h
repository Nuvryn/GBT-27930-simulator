/** @file libvncserver_port.h @brief Private Linux web display implementation. */
#ifndef LIBVNCSERVER_PORT_H
#define LIBVNCSERVER_PORT_H
#include "support/ui_display.h"
int ui_web_open(const ui_display_options_t *options, void **display);
int ui_web_error(void);
void ui_web_close(void);
#endif
