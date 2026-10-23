/** @file sim_ui.c @brief Dedicated LVGL owner and deferred configuration worker. */
#include "ui/sim_ui.h"
#include "ui/sim_ui_view.h"
#include "support/ui_display.h"
#include "osal/osal.h"
#include <errno.h>
#include <stdatomic.h>
#include <string.h>
struct sim_ui_s
{
    sim_control_t *control;
    sim_config_t config;
    char path[256];
    osal_sem_t ready, ended, work_ended;
    atomic_bool stop;
    atomic_int error;
    bool worker_started;
};
static void ui_thread(void *user)
{
    sim_ui_t *ui = user;
    ui_background_priority();
    lv_init();
    void *display = NULL;
    ui_display_options_t options = {.backend = ui->config.ui_backend, .listen = ui->config.ui_web_listen, .web_port = ui->config.ui_web_port, .vnc_port = ui->config.ui_web_vnc_port, .width = ui->config.ui_web_width, .height = ui->config.ui_web_height, .fps = ui->config.ui_web_fps, .framebuffer = ui->config.ui_framebuffer, .input_device = ui->config.ui_input_device, .rotation = ui->config.ui_rotation};
    int rc = ui_display_open(&options, &display);
    sim_ui_view_t *view = rc == 0 ? sim_ui_view_create(ui->control, ui->path) : NULL;
    if (rc == 0 && view == NULL)
    {
        rc = -ENOMEM;
    }
    atomic_store(&ui->error, rc);
    (void) osal_sem_post(ui->ready);
    if (rc == 0)
    {
        uint32_t last_tick = osal_get_tick_ms(), last_refresh = last_tick;
        while (!atomic_load(&ui->stop))
        {
            uint32_t now = osal_get_tick_ms();
            lv_tick_inc(now - last_tick);
            last_tick = now;
            if (now - last_refresh >= 100u)
            {
                sim_ui_view_refresh(view);
                last_refresh = now;
            }
            (void) lv_timer_handler();
            rc = ui_display_error();
            if (rc != 0)
            {
                atomic_store(&ui->error, rc);
                break;
            }
            osal_delay_ms(5u);
        }
    }
    if (view != NULL)
    {
        sim_ui_view_destroy(view);
    }
    ui_display_close(display);
    (void) osal_sem_post(ui->ended);
}
static void worker_thread(void *user)
{
    sim_ui_t *ui = user;
    ui_background_priority();
    while (!atomic_load(&ui->stop))
    {
        sim_control_work(ui->control);
        osal_delay_ms(5u);
    }
    /* Drain the bounded evidence and pending save queues before logger shutdown. */
    for (unsigned idx = 0u; idx < 5u; idx++)
    {
        sim_control_work(ui->control);
    }
    (void) osal_sem_post(ui->work_ended);
}
int sim_ui_open(sim_control_t *control, const sim_config_t *config, const char *path, sim_ui_t **output)
{
    *output = NULL;
    if (!config->ui_enabled)
    {
        return 0;
    }
    sim_ui_t *ui = osal_malloc(sizeof(*ui));
    if (ui == NULL)
    {
        return -ENOMEM;
    }
    memset(ui, 0, sizeof(*ui));
    ui->control = control;
    ui->config = *config;
    atomic_init(&ui->stop, false);
    atomic_init(&ui->error, 0);
    if (path != NULL && strlen(path) < sizeof(ui->path))
    {
        memcpy(ui->path, path, strlen(path) + 1u);
    }
    int rc = osal_sem_create(&ui->ready, 0u, 1u);
    if (rc == 0)
    {
        rc = osal_sem_create(&ui->ended, 0u, 1u);
    }
    if (rc == 0)
    {
        rc = osal_sem_create(&ui->work_ended, 0u, 1u);
    }
    if (rc != 0)
    {
        if (ui->ready != NULL)
        {
            (void) osal_sem_destroy(ui->ready);
        }
        if (ui->ended != NULL)
        {
            (void) osal_sem_destroy(ui->ended);
        }
        osal_free(ui);
        return rc;
    }
    osal_task_attr_t attr = {.name = "sim_ui", .entry = ui_thread, .arg = ui, .stack_size = 512u * 1024u, .prio = 0u};
    rc = osal_task_create(&attr);
    if (rc != 0)
    {
        (void) osal_sem_destroy(ui->ready);
        (void) osal_sem_destroy(ui->ended);
        (void) osal_sem_destroy(ui->work_ended);
        osal_free(ui);
        return rc;
    }
    (void) osal_sem_wait(ui->ready, OSAL_WAIT_FOREVER);
    rc = atomic_load(&ui->error);
    if (rc == 0)
    {
        attr.entry = worker_thread;
        attr.name = "sim_config";
        rc = osal_task_create(&attr);
        ui->worker_started = rc == 0;
    }
    if (rc != 0)
    {
        sim_ui_close(ui);
        return rc;
    }
    *output = ui;
    return 0;
}
int sim_ui_error(sim_ui_t *ui)
{
    return ui == NULL ? 0 : atomic_load(&ui->error);
}
void sim_ui_close(sim_ui_t *ui)
{
    if (ui == NULL)
    {
        return;
    }
    atomic_store(&ui->stop, true);
    (void) osal_sem_wait(ui->ended, OSAL_WAIT_FOREVER);
    if (ui->worker_started)
    {
        (void) osal_sem_wait(ui->work_ended, OSAL_WAIT_FOREVER);
    }
    (void) osal_sem_destroy(ui->ready);
    (void) osal_sem_destroy(ui->ended);
    (void) osal_sem_destroy(ui->work_ended);
    osal_free(ui);
}

bool sim_ui_available(void)
{
    return true;
}
