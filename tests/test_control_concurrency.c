/** @file test_control_concurrency.c @brief Real-thread contention and acknowledged bounded commands. */
#include "ui/sim_control.h"
#include "osal/osal.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(expr)                                            \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); \
            abort();                                           \
        }                                                      \
    } while (0)
static atomic_bool done;
static atomic_uint acknowledged;
static int send_frame(void *user, const sim_frame_t *frame)
{
    (void) user;
    (void) frame;
    return 0;
}
static void *ui_thread(void *user)
{
    sim_control_t *control = user;
    unsigned submitted = 0u, received = 0u, last_id = 0u;
    while (received < 2000u)
    {
        sim_control_view_t view;
        if (sim_control_read(control, &view) == 0 && submitted < 2000u)
        {
            sim_control_request_t request = {.kind = SIM_CONTROL_APPLY, .session_id = view.status.session_id, .values = view.config.values};
            int rc = sim_control_submit(control, &request);
            CHECK(rc == 0 || rc == -EBUSY);
            if (rc == 0)
            {
                submitted++;
            }
        }
        sim_control_result_t result;
        int rc;
        while ((rc = sim_control_result(control, &result)) == 1)
        {
            CHECK(result.result == 0 && result.id == last_id + 1u);
            last_id = result.id;
            received++;
            atomic_store(&acknowledged, received);
        }
        CHECK(rc == 0 || rc == -EBUSY);
        uint32_t cursor = 0u;
        char line[1152];
        (void) sim_control_log_read(control, &cursor, line, sizeof(line));
    }
    atomic_store(&done, true);
    return NULL;
}
int main(void)
{
    sim_config_t config;
    sim_config_defaults(&config);
    config.manual_start = true;
    sim_control_t *control = sim_control_create(&config, NULL);
    sim_session_t *session = sim_session_create(&config, send_frame, NULL);
    CHECK(control != NULL && session != NULL);
    atomic_init(&done, false);
    atomic_init(&acknowledged, 0u);
    pthread_t thread;
    CHECK(pthread_create(&thread, NULL, ui_thread, control) == 0);
    uint32_t start = osal_get_tick_ms();
    while (!atomic_load(&done))
    {
        CHECK(osal_get_tick_ms() - start < 15000u);
        CHECK(sim_control_step(control, session, 0u) == 0);
        CHECK(sim_control_publish(control, session) == 0);
        sim_control_log(control, "TEST", "concurrent", 0u);
    }
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(atomic_load(&acknowledged) == 2000u);
    sim_control_destroy(control);
    sim_session_destroy(session);
    puts("PASS: concurrent snapshots, logs and 2000 ordered command results");
    return 0;
}
