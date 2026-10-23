/** @file sim_ui_view.h @brief UI-thread-owned pages, also usable with an offscreen display. */
#ifndef SIM_UI_VIEW_H
#define SIM_UI_VIEW_H
#include "ui/sim_control.h"
#include "lvgl/lvgl.h"
typedef struct sim_ui_view_s sim_ui_view_t;
sim_ui_view_t *sim_ui_view_create(sim_control_t *control, const char *config_path);
void sim_ui_view_refresh(sim_ui_view_t *view);
void sim_ui_view_destroy(sim_ui_view_t *view);
/** Borrowed widget for integration tests/accessibility; UI thread only. */
lv_obj_t *sim_ui_view_widget(sim_ui_view_t *view, const char *name);
#endif
