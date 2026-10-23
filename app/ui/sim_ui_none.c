/** @file sim_ui_none.c @brief A headless build has no graphical dependencies. */
#include "ui/sim_ui.h"
#include <errno.h>
int sim_ui_open(sim_control_t *control, const sim_config_t *config, const char *path, sim_ui_t **ui)
{
    (void) control;
    (void) path;
    *ui = NULL;
    return config->ui_enabled ? -ENOTSUP : 0;
}
int sim_ui_error(sim_ui_t *ui)
{
    (void) ui;
    return 0;
}
void sim_ui_close(sim_ui_t *ui)
{
    (void) ui;
}

bool sim_ui_available(void)
{
    return false;
}
