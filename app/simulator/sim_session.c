/** @file sim_session.c @brief One fresh protocol engine per session; no input or platform I/O. */
#include "simulator/sim_session.h"
#include "logging/sim_log.h"
#include "osal/osal.h"
#include <errno.h>
#include <string.h>

#define SIM_SESSION_CHM_QUIET_MS 1000u
struct sim_session_s
{
    sim_config_t config;
    sim_status_t status;
    unsigned revision;
    sim_fault_rule_t armed_fault;
    bool fault_armed;
    sim_engine_t *engine;
    sim_engine_send_cb_t send_cb;
    void *cb_user;
    uint32_t start_ms, last_chm_ms;
    unsigned count;
    int receive_error, io_error;
    bool has_chm, require_chm_quiet;
};
static int send_frame(void *cb_user, const sim_frame_t *frame)
{
    sim_session_t *session = cb_user;
    int rc = session->send_cb(session->cb_user, frame);
    if (rc != 0 && session->io_error == 0)
    {
        session->io_error = rc;
    }
    return rc;
}
sim_session_t *sim_session_create(const sim_config_t *config, sim_engine_send_cb_t send_cb, void *cb_user)
{
    if (config == NULL || send_cb == NULL)
    {
        return NULL;
    }
    sim_session_t *session = osal_malloc(sizeof(*session));
    if (session == NULL)
    {
        return NULL;
    }
    *session = (sim_session_t){.config = *config, .send_cb = send_cb, .cb_user = cb_user};
    if (config->manual_start)
    {
        session->config.retries = 0u;
        session->config.reconnect_allowed = false;
    }
    session->revision = 1u;
    session->status.stage = SIM_STAGE_IDLE;
    return session;
}
void sim_session_destroy(sim_session_t *session)
{
    if (session != NULL)
    {
        sim_engine_destroy(session->engine);
        osal_free(session);
    }
}
int sim_session_start(sim_session_t *session, uint32_t now_ms)
{
    if (session == NULL)
    {
        return -EINVAL;
    }
    if (session->engine != NULL)
    {
        return -EBUSY;
    }
    session->engine = sim_engine_create(&session->config, send_frame, session);
    if (session->engine == NULL)
    {
        return -ENOMEM;
    }
    if (session->fault_armed)
    {
        (void) sim_engine_manual_fault(session->engine, &session->armed_fault);
        session->fault_armed = false;
    }
    session->start_ms = now_ms;
    session->receive_error = 0;
    session->io_error = 0;
    session->count++;
    sim_log_event("SESSION", "id=%u event=start role=%s", session->count, session->config.charger ? "charger" : "bms");
    return 0;
}
static bool valid_chm(const sim_session_t *session, const sim_frame_t *frame)
{
    if (frame->id > 0x1FFFFFFFu || (uint8_t) frame->id != session->config.peer ||
        (uint8_t) (frame->id >> 8u) != session->config.source || ((frame->id >> 8u) & 0x3FF00u) != 0x2600u)
    {
        return false;
    }
    gbt27930_values_t decoded;
    return gbt27930_decode(GBT27930_CHM, session->config.version, frame->data, frame->len, &decoded) == 0;
}
int sim_session_receive(sim_session_t *session, const sim_frame_t *frame, uint32_t now_ms)
{
    if (session == NULL || frame == NULL || frame->len > 8u)
    {
        return -EINVAL;
    }
    if (!session->config.charger && valid_chm(session, frame))
    {
        bool fresh = !session->require_chm_quiet || !session->has_chm || now_ms - session->last_chm_ms >= SIM_SESSION_CHM_QUIET_MS;
        session->has_chm = true;
        session->last_chm_ms = now_ms;
        if (session->engine == NULL && session->config.manual_start && fresh)
        {
            int rc = sim_session_start(session, now_ms);
            if (rc != 0)
            {
                return rc;
            }
        }
    }
    if (session->engine != NULL)
    {
        int rc = sim_engine_receive(session->engine, frame, now_ms - session->start_ms);
        if (rc != 0 && session->receive_error == 0)
        {
            session->receive_error = rc;
        }
    }
    return 0;
}
int sim_session_tick(sim_session_t *session, uint32_t now_ms)
{
    if (session == NULL)
    {
        return -EINVAL;
    }
    if (session->engine == NULL)
    {
        return 0;
    }
    int result = session->receive_error != 0 ? session->receive_error : sim_engine_tick(session->engine, now_ms - session->start_ms);
    if (result != 0)
    {
        sim_log_event("SESSION_RESULT", "id=%u status=%s code=%d", session->count, result == 1 ? "PASS" : "FAIL", result);
        sim_engine_finish(session->engine, result, now_ms - session->start_ms);
        sim_engine_status(session->engine, &session->status);
        session->status.active = false;
        session->status.fault_enabled = false;
        session->status.result = result;
        session->require_chm_quiet = result < 0;
        sim_engine_destroy(session->engine);
        session->engine = NULL;
    }
    return result;
}
void sim_session_stop(sim_session_t *session, uint32_t now_ms)
{
    if (session != NULL && session->engine != NULL)
    {
        sim_engine_stop(session->engine, now_ms - session->start_ms);
    }
}
bool sim_session_active(const sim_session_t *session)
{
    return session != NULL && session->engine != NULL;
}
int sim_session_io_error(const sim_session_t *session)
{
    return session != NULL ? session->io_error : -EINVAL;
}

int sim_session_values(sim_session_t *session, const gbt27930_values_t *values)
{
    if (session == NULL || values == NULL)
    {
        return -EINVAL;
    }
    if (session->engine != NULL)
    {
        return -EBUSY;
    }
    const gbt27930_values_t *previous = &session->config.values;
    if (values->cell_count != previous->cell_count || values->temperature_count != previous->temperature_count ||
        memcmp(values->cells_mv, previous->cells_mv, sizeof(values->cells_mv)) != 0 ||
        memcmp(values->cell_groups, previous->cell_groups, sizeof(values->cell_groups)) != 0 ||
        memcmp(values->temperatures_degc, previous->temperatures_degc, sizeof(values->temperatures_degc)) != 0 ||
        memcmp(values->available, previous->available, sizeof(values->available)) != 0 ||
        memcmp(values->reserved, previous->reserved, sizeof(values->reserved)) != 0 ||
        memcmp(values->manufacturer, previous->manufacturer, sizeof(values->manufacturer)) != 0 ||
        memcmp(values->vin, previous->vin, sizeof(values->vin)) != 0 || memcmp(values->software, previous->software, sizeof(values->software)) != 0 ||
        memcmp(values->region, previous->region, sizeof(values->region)) != 0)
    {
        return -EINVAL;
    }
    for (size_t idx = 0u; gbt27930_field(idx) != NULL; idx++)
    {
        if (values->field[idx] != previous->field[idx] && gbt27930_descriptor(gbt27930_field(idx)->message)->charger != session->config.charger)
        {
            return -EPERM;
        }
    }
    sim_config_t candidate = session->config;
    candidate.values = *values;
    int rc = sim_config_validate_values(&candidate);
    if (rc != 0)
    {
        return rc;
    }
    session->config.values = *values;
    session->revision++;
    return 0;
}
int sim_session_manual_fault(sim_session_t *session, const sim_fault_rule_t *rule)
{
    if (session == NULL || (rule != NULL && sim_config_validate_fault(rule) != 0))
    {
        return -EINVAL;
    }
    if (session->engine != NULL)
    {
        return sim_engine_manual_fault(session->engine, rule);
    }
    session->fault_armed = rule != NULL;
    if (rule != NULL)
    {
        session->armed_fault = *rule;
    }
    return 0;
}
void sim_session_status(sim_session_t *session, sim_status_t *status)
{
    if (session->engine != NULL)
    {
        sim_engine_status(session->engine, &session->status);
    }
    *status = session->status;
    status->active = session->engine != NULL;
    status->session_id = session->count;
    status->revision = session->revision;
    if (!status->active)
    {
        status->stage = SIM_STAGE_IDLE;
        status->fault_enabled = session->fault_armed;
        if (session->fault_armed)
        {
            status->fault_hits = 0u;
        }
    }
}
