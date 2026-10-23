/** @file sim_ui.h @brief Optional front end lifecycle; no LVGL types escape. */
#ifndef SIM_UI_H
#define SIM_UI_H
#include "ui/sim_control.h"
typedef struct sim_ui_s sim_ui_t;
bool sim_ui_available(void);
int sim_ui_open(sim_control_t *control, const sim_config_t *config, const char *path, sim_ui_t **ui);
int sim_ui_error(sim_ui_t *ui);
void sim_ui_close(sim_ui_t *ui);
#endif
