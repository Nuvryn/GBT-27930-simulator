/** @file sim_control.c @brief Protocol commands, snapshots, bounded logs and deferred persistence. */
#include "ui/sim_control.h"
#include "support/config_store.h"
#include "logging/sim_log.h"
#include "osal/osal.h"
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define SIM_EVIDENCE_QUEUE 4u
struct sim_control_s
{
    osal_mutex_t mutex;
    atomic_bool stop;
    atomic_uint stop_session;
    atomic_int error;
    atomic_uint dropped;
    sim_control_request_t queue[SIM_CONTROL_QUEUE];
    sim_control_result_t results[SIM_CONTROL_QUEUE];
    unsigned result_head, result_count, outstanding;
    unsigned head, count, next_id, evidence_head, evidence_count, last_session;
    sim_control_view_t view;
    sim_config_t protocol_config;
    bool acknowledgement;
    unsigned acknowledged_id;
    int acknowledged_result, stop_result;
    unsigned stop_sequence;
    sim_config_t evidence[SIM_EVIDENCE_QUEUE], save_config;
    unsigned evidence_session[SIM_EVIDENCE_QUEUE], evidence_revision[SIM_EVIDENCE_QUEUE], save_revision;
    bool save_pending;
    char path[256];
    char log[SIM_UI_LOG_LINES][SIM_UI_LOG_LINE_SIZE];
};
sim_control_t *sim_control_create(const sim_config_t *config, const char *config_path)
{
    if (config == NULL || (config_path != NULL && strlen(config_path) >= 256u))
    {
        return NULL;
    }
    sim_control_t *control = osal_malloc(sizeof(*control));
    if (control == NULL)
    {
        return NULL;
    }
    memset(control, 0, sizeof(*control));
    atomic_init(&control->stop, false);
    atomic_init(&control->stop_session, 0u);
    atomic_init(&control->error, 0);
    atomic_init(&control->dropped, 0u);
    if (osal_mutex_create(&control->mutex) != 0)
    {
        osal_free(control);
        return NULL;
    }
    control->view.config = *config;
    control->protocol_config = *config;
    control->view.status.stage = SIM_STAGE_IDLE;
    control->view.status.revision = 1u;
    control->view.saved_revision = 1u;
    if (config_path != NULL)
    {
        (void) snprintf(control->path, sizeof(control->path), "%s", config_path);
    }
    return control;
}
void sim_control_destroy(sim_control_t *control)
{
    if (control != NULL)
    {
        (void) osal_mutex_destroy(control->mutex);
        osal_free(control);
    }
}
int sim_control_submit(sim_control_t *control, sim_control_request_t *request)
{
    if (control == NULL || request == NULL || (unsigned) request->kind > SIM_CONTROL_CANCEL)
    {
        return -EINVAL;
    }
    int rc = osal_mutex_lock(control->mutex, OSAL_NO_WAIT);
    if (rc != 0)
    {
        return -EBUSY;
    }
    if (control->outstanding == SIM_CONTROL_QUEUE)
    {
        rc = -EBUSY;
    }
    else
    {
        request->id = ++control->next_id;
        control->queue[(control->head + control->count) % SIM_CONTROL_QUEUE] = *request;
        control->count++;
        control->outstanding++;
    }
    (void) osal_mutex_unlock(control->mutex);
    return rc;
}
int sim_control_result(sim_control_t *control, sim_control_result_t *result)
{
    if (control == NULL || result == NULL)
    {
        return -EINVAL;
    }
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        return -EBUSY;
    }
    int rc = 0;
    if (control->result_count != 0u)
    {
        *result = control->results[control->result_head];
        control->result_head = (control->result_head + 1u) % SIM_CONTROL_QUEUE;
        control->result_count--;
        control->outstanding--;
        rc = 1;
    }
    (void) osal_mutex_unlock(control->mutex);
    return rc;
}
void sim_control_stop(sim_control_t *control, unsigned session_id)
{
    if (control != NULL)
    {
        atomic_store(&control->stop_session, session_id);
        atomic_store(&control->stop, true);
    }
}
int sim_control_read(sim_control_t *control, sim_control_view_t *view)
{
    if (control == NULL || view == NULL)
    {
        return -EINVAL;
    }
    int rc = osal_mutex_lock(control->mutex, OSAL_NO_WAIT);
    if (rc != 0)
    {
        return -EBUSY;
    }
    *view = control->view;
    view->log_dropped = atomic_load(&control->dropped);
    (void) osal_mutex_unlock(control->mutex);
    return 0;
}
int sim_control_step(sim_control_t *control, sim_session_t *session, uint32_t now_ms)
{
    if (control == NULL)
    {
        return 0;
    }
    if (atomic_exchange(&control->stop, false))
    {
        sim_status_t current;
        sim_session_status(session, &current);
        unsigned requested = atomic_load(&control->stop_session);
        int rc = current.session_id == requested && current.active ? 0 : -ESTALE;
        if (rc == 0)
        {
            sim_session_stop(session, now_ms);
        }
        control->stop_result = rc;
        control->stop_sequence++;
        sim_log_event("CONTROL_STOP", "session=%u result=%d", requested, rc);
    }
    if (control->acknowledgement)
    {
        return 0;
    }
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        return 0;
    }
    if (control->count == 0u)
    {
        (void) osal_mutex_unlock(control->mutex);
        return 0;
    }
    sim_control_request_t request = control->queue[control->head];
    control->head = (control->head + 1u) % SIM_CONTROL_QUEUE;
    control->count--;
    (void) osal_mutex_unlock(control->mutex);
    sim_status_t status;
    sim_session_status(session, &status);
    int rc = request.session_id != status.session_id ? -ESTALE : 0;
    if (rc == 0)
    {
        switch (request.kind)
        {
            case SIM_CONTROL_START:
                rc = control->protocol_config.charger ? sim_session_start(session, now_ms) : -EPERM;
                break;
            case SIM_CONTROL_APPLY:
                rc = sim_session_values(session, &request.values);
                break;
            case SIM_CONTROL_FAULT:
                rc = sim_session_manual_fault(session, &request.fault);
                break;
            case SIM_CONTROL_CANCEL:
                rc = sim_session_manual_fault(session, NULL);
                break;
            default:
                rc = -EINVAL;
                break;
        }
    }
    control->acknowledgement = true;
    control->acknowledged_id = request.id;
    control->acknowledged_result = rc;
    if (rc == 0 && request.kind == SIM_CONTROL_APPLY)
    {
        control->protocol_config.values = request.values;
    }
    sim_log_event("CONTROL", "request=%u kind=%u result=%d", request.id, (unsigned) request.kind, rc);
    return request.kind == SIM_CONTROL_START && rc == -ENOMEM ? rc : 0;
}
int sim_control_publish(sim_control_t *control, sim_session_t *session)
{
    if (control == NULL)
    {
        return 0;
    }
    sim_status_t status;
    sim_session_status(session, &status);
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        return 0;
    }
    control->view.status = status;
    control->view.stop_result = control->stop_result;
    control->view.stop_sequence = control->stop_sequence;
    control->view.config = control->protocol_config;
    if (control->acknowledgement)
    {
        unsigned slot = (control->result_head + control->result_count) % SIM_CONTROL_QUEUE;
        control->results[slot] = (sim_control_result_t){.id = control->acknowledged_id, .result = control->acknowledged_result};
        control->result_count++;
        control->view.request_id = control->acknowledged_id;
        control->view.request_result = control->acknowledged_result;
        control->acknowledgement = false;
    }
    if (status.session_id != control->last_session)
    {
        if (control->evidence_count == SIM_EVIDENCE_QUEUE)
        {
            atomic_store(&control->error, -ENOSPC);
        }
        else
        {
            unsigned index = (control->evidence_head + control->evidence_count) % SIM_EVIDENCE_QUEUE;
            control->evidence[index] = control->protocol_config;
            control->evidence_session[index] = status.session_id;
            control->evidence_revision[index] = status.revision;
            control->evidence_count++;
        }
        control->last_session = status.session_id;
    }
    (void) osal_mutex_unlock(control->mutex);
    return atomic_load(&control->error);
}
void sim_control_log(void *user, const char *tag, const char *message, uint32_t now_ms)
{
    sim_control_t *control = user;
    if (control == NULL || strcmp(tag, "DETAIL") == 0)
    {
        return;
    }
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        (void) atomic_fetch_add(&control->dropped, 1u);
        return;
    }
    uint32_t sequence = control->view.log_sequence++;
    if (sequence >= SIM_UI_LOG_LINES)
    {
        (void) atomic_fetch_add(&control->dropped, 1u);
    }
    (void) snprintf(control->log[sequence % SIM_UI_LOG_LINES], SIM_UI_LOG_LINE_SIZE, "%s [%u ms] %s", tag, now_ms, message);
    (void) osal_mutex_unlock(control->mutex);
}
int sim_control_log_read(sim_control_t *control, uint32_t *cursor, char *line, size_t capacity)
{
    if (control == NULL || cursor == NULL || line == NULL || capacity == 0u)
    {
        return -EINVAL;
    }
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        return -EBUSY;
    }
    uint32_t end = control->view.log_sequence;
    if (end - *cursor > SIM_UI_LOG_LINES)
    {
        *cursor = end - SIM_UI_LOG_LINES;
    }
    int rc = *cursor != end ? 1 : 0;
    if (rc == 1)
    {
        (void) snprintf(line, capacity, "%s", control->log[*cursor % SIM_UI_LOG_LINES]);
        (*cursor)++;
    }
    (void) osal_mutex_unlock(control->mutex);
    return rc;
}
int sim_control_save(sim_control_t *control)
{
    if (control == NULL)
    {
        return -EINVAL;
    }
    if (osal_mutex_lock(control->mutex, OSAL_NO_WAIT) != 0)
    {
        return -EBUSY;
    }
    int rc = 0;
    if (control->path[0] == '\0')
    {
        rc = -ENOTSUP;
    }
    else if (control->view.saving)
    {
        rc = -EBUSY;
    }
    else
    {
        control->save_config = control->view.config;
        control->save_revision = control->view.status.revision;
        control->save_pending = true;
        control->view.saving = true;
    }
    (void) osal_mutex_unlock(control->mutex);
    return rc;
}
typedef struct
{
    char text[65537];
    size_t length;
} config_text_t;
static int append_json(const char *text, size_t len, void *user)
{
    config_text_t *buffer = user;
    if (len >= sizeof(buffer->text) - buffer->length)
    {
        return -E2BIG;
    }
    memcpy(buffer->text + buffer->length, text, len);
    buffer->length += len;
    buffer->text[buffer->length] = '\0';
    return 0;
}
typedef struct
{
    unsigned session, revision, part;
} evidence_context_t;
static int evidence_json(const char *text, size_t len, void *user)
{
    evidence_context_t *context = user;
    return sim_log_detail("SESSION_CONFIG id=%u revision=%u part=%u data=%.*s", context->session, context->revision, ++context->part, (int) len, text);
}
void sim_control_work(sim_control_t *control)
{
    if (control == NULL)
    {
        return;
    }
    sim_config_t config;
    bool save = false, evidence = false;
    unsigned revision = 0u;
    evidence_context_t context = {0};
    (void) osal_mutex_lock(control->mutex, OSAL_WAIT_FOREVER);
    if (control->evidence_count != 0u)
    {
        unsigned index = control->evidence_head;
        config = control->evidence[index];
        context.session = control->evidence_session[index];
        context.revision = control->evidence_revision[index];
        control->evidence_head = (index + 1u) % SIM_EVIDENCE_QUEUE;
        control->evidence_count--;
        evidence = true;
    }
    else if (control->save_pending)
    {
        config = control->save_config;
        revision = control->save_revision;
        control->save_pending = false;
        save = true;
    }
    (void) osal_mutex_unlock(control->mutex);
    if (evidence)
    {
        int rc = sim_log_detail("SESSION_CONFIG_BEGIN id=%u revision=%u", context.session, context.revision);
        if (rc == 0)
        {
            rc = sim_config_snapshot(&config, evidence_json, &context);
        }
        if (rc == 0)
        {
            rc = sim_log_detail("SESSION_CONFIG_END id=%u parts=%u", context.session, context.part);
        }
        if (rc != 0)
        {
            atomic_store(&control->error, rc);
        }
    }
    if (save)
    {
        config_text_t *buffer = osal_malloc(sizeof(*buffer));
        int rc = buffer == NULL ? -ENOMEM : 0;
        if (rc == 0)
        {
            buffer->length = 0u;
            rc = sim_config_snapshot(&config, append_json, buffer);
            sim_config_t verified;
            char error[256];
            if (rc == 0)
            {
                rc = sim_config_parse(buffer->text, &verified, error, sizeof(error));
                if (rc != 0)
                {
                    sim_control_log(control, "SAVE_ERROR", error, 0u);
                }
            }
            if (rc == 0)
            {
                rc = config_store_write(control->path, buffer->text, buffer->length);
            }
            osal_free(buffer);
        }
        (void) osal_mutex_lock(control->mutex, OSAL_WAIT_FOREVER);
        control->view.save_result = rc;
        control->view.saving = false;
        if (rc == 0)
        {
            control->view.saved_revision = revision;
        }
        (void) osal_mutex_unlock(control->mutex);
    }
}
int sim_control_error(sim_control_t *control)
{
    return control == NULL ? 0 : atomic_load(&control->error);
}
static int value_divisor(const gbt27930_field_t *field)
{
    size_t length = strlen(field->key);
    return length >= 3u && (strcmp(field->key + length - 3u, "_mv") == 0 || strcmp(field->key + length - 3u, "_ma") == 0) ? 1000 : 1;
}
int sim_control_parse_value(const gbt27930_field_t *field, const char *text, int64_t *value)
{
    if (field == NULL || text == NULL || value == NULL || strlen(text) > 24u)
    {
        return -EINVAL;
    }
    bool negative = *text == '-';
    if (negative)
    {
        text++;
    }
    if (*text < '0' || *text > '9')
    {
        return -EINVAL;
    }
    int64_t whole = 0, fraction = 0;
    unsigned digits = 0u;
    while (*text >= '0' && *text <= '9')
    {
        whole = whole * 10 + (*text++ - '0');
        if (whole > 4294967295LL)
        {
            return -ERANGE;
        }
    }
    if (*text == '.')
    {
        text++;
        if (*text < '0' || *text > '9')
        {
            return -EINVAL;
        }
        while (*text >= '0' && *text <= '9')
        {
            if (++digits > 3u)
            {
                return -ERANGE;
            }
            fraction = fraction * 10 + (*text++ - '0');
        }
    }
    if (*text != '\0' || (value_divisor(field) == 1 && digits != 0u))
    {
        return -EINVAL;
    }
    while (digits < 3u)
    {
        fraction *= 10;
        digits++;
    }
    *value = whole * value_divisor(field) + fraction;
    if (negative)
    {
        *value = -*value;
    }
    if (*value < field->minimum || *value > field->maximum || (*value - field->offset) % field->scale != 0)
    {
        return -ERANGE;
    }
    gbt27930_values_t checked;
    gbt27930_defaults(&checked);
    return gbt27930_set(&checked, field->key, *value);
}
void sim_control_format_value(const gbt27930_field_t *field, int64_t value, char *text, size_t capacity)
{
    if (value_divisor(field) == 1)
    {
        (void) snprintf(text, capacity, "%" PRId64, value);
    }
    else
    {
        int64_t magnitude = value < 0 ? -value : value;
        (void) snprintf(text, capacity, "%s%" PRId64 ".%03" PRId64, value < 0 ? "-" : "", magnitude / 1000, magnitude % 1000);
    }
}
