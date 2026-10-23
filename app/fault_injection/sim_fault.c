/** @file sim_fault.c @brief Single-thread rule execution, one-shot or repeated scenarios. */
#include "fault_injection/sim_fault.h"
#include "logging/sim_log.h"
#include "osal/osal.h"
#include <errno.h>
#include <string.h>
#define SIM_FAULT_QUEUE_CAPACITY 64u
typedef struct
{
    bool occupied, manual;
    bool receive;
    char message[16];
    uint32_t due_ms;
    uint32_t id;
    size_t len;
    uint8_t data[GBT27930_PAYLOAD_CAPACITY];
} sim_fault_pending_t;
struct sim_fault_s
{
    sim_fault_rule_t manual;
    bool manual_enabled, dispatch_manual;
    uint32_t manual_hits, manual_start_ms, now_ms;
    const sim_config_t *config;
    sim_fault_dispatch_cb_t dispatch_cb;
    void *cb_user;
    sim_fault_observer_t observer;
    void *observer_user;
    uint32_t hits[SIM_CONFIG_RULE_CAPACITY];
    sim_fault_pending_t queue[SIM_FAULT_QUEUE_CAPACITY];
};
sim_fault_t *sim_fault_create(const sim_config_t *config, sim_fault_dispatch_cb_t dispatch_cb, void *cb_user)
{
    if (config == NULL || dispatch_cb == NULL)
    {
        return NULL;
    }
    sim_fault_t *fault = osal_malloc(sizeof(*fault));
    if (fault != NULL)
    {
        memset(fault, 0, sizeof(*fault));
        fault->config = config;
        fault->dispatch_cb = dispatch_cb;
        fault->cb_user = cb_user;
    }
    return fault;
}
void sim_fault_destroy(sim_fault_t *fault)
{
    osal_free(fault);
}
void sim_fault_cancel(sim_fault_t *fault)
{
    if (fault != NULL)
    {
        for (size_t idx = 0u; idx < SIM_FAULT_QUEUE_CAPACITY; idx++)
        {
            fault->queue[idx].occupied = false;
        }
    }
}
/** Copy before returning; caller buffers never escape the injection call. */
static int sim_fault_delay(sim_fault_t *fault, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len, uint32_t due_ms)
{
    for (size_t idx = 0u; idx < SIM_FAULT_QUEUE_CAPACITY; idx++)
    {
        if (!fault->queue[idx].occupied)
        {
            sim_fault_pending_t *pending = &fault->queue[idx];
            pending->occupied = true;
            pending->manual = fault->dispatch_manual;
            pending->receive = receive;
            pending->id = id;
            pending->len = len;
            pending->due_ms = due_ms;
            memcpy(pending->message, message, strlen(message) + 1u);
            memcpy(pending->data, data, len);
            return 0;
        }
    }
    return -ENOSPC;
}
int sim_fault_apply(sim_fault_t *fault, bool receive, const char *message, const char *stage, uint32_t stage_ms,
    uint32_t now_ms, uint32_t id, const uint8_t *data, size_t len)
{
    if (fault == NULL || data == NULL || len > GBT27930_PAYLOAD_CAPACITY || strlen(message) >= 16u)
    {
        return -EINVAL;
    }
    uint8_t changed[GBT27930_PAYLOAD_CAPACITY];
    memcpy(changed, data, len);
    fault->now_ms = now_ms;
    for (size_t idx = 0u; idx <= fault->config->fault_count; idx++)
    {
        bool manual = idx == 0u;
        if (manual && !fault->manual_enabled)
        {
            continue;
        }
        const sim_fault_rule_t *rule = manual ? &fault->manual : &fault->config->faults[idx - 1u];
        uint32_t *hits = manual ? &fault->manual_hits : &fault->hits[idx - 1u];
        uint32_t elapsed = manual ? now_ms - fault->manual_start_ms : (strcmp(rule->stage, "*") == 0 ? now_ms : stage_ms);
        if (rule->receive != receive || strcmp(rule->message, message) != 0 || (strcmp(rule->stage, "*") != 0 && strcmp(rule->stage, stage) != 0) ||
            elapsed < rule->after_ms || (rule->duration_ms != 0u && elapsed - rule->after_ms >= rule->duration_ms) || (rule->count != 0u && *hits >= rule->count))
        {
            continue;
        }
        (*hits)++;
        fault->dispatch_manual = manual;
        if (fault->observer != NULL)
        {
            sim_fault_evidence_t evidence = {.manual = manual, .receive = receive, .rule = manual ? 0u : (unsigned) (idx - 1u), .action = rule->action, .at_ms = now_ms, .delay_ms = rule->delay_ms, .value = rule->value, .byte = rule->byte, .before = rule->byte < len ? changed[rule->byte] : 0u, .length = len};
            memcpy(evidence.message, message, strlen(message) + 1u);
            fault->observer(fault->observer_user, &evidence);
        }
        sim_log_event("FAULT", "source=%s rule=%u message=%s direction=%s action=%u hit=%u before_len=%u byte=%u before=%u after=%u", manual ? "manual" : "config", manual ? 0u : (unsigned) (idx - 1u), message, receive ? "rx" : "tx",
            (unsigned) rule->action, (unsigned) *hits, (unsigned) len, (unsigned) rule->byte, rule->byte < len ? (unsigned) changed[rule->byte] : 0u, (unsigned) rule->value);
        switch (rule->action)
        {
            case SIM_FAULT_DROP:
                return 0;
            case SIM_FAULT_DELAY:
                return sim_fault_delay(fault, receive, message, id, changed, len, now_ms + rule->delay_ms);
            case SIM_FAULT_DUPLICATE:
            {
                int rc = fault->dispatch_cb(fault->cb_user, receive, message, id, changed, len);
                if (rc != 0)
                {
                    return rc;
                }
                break;
            }
            case SIM_FAULT_CORRUPT:
                if (rule->byte >= len)
                {
                    return -ERANGE;
                }
                changed[rule->byte] = (uint8_t) rule->value;
                break;
            case SIM_FAULT_LENGTH:
                if (rule->value > sizeof(changed))
                {
                    return -ERANGE;
                }
                if (rule->value > len)
                {
                    memset(changed + len, 0xFF, rule->value - len);
                }
                len = rule->value;
                break;
            case SIM_FAULT_ID:
                id = rule->value;
                break;
            default:
                return -EINVAL;
        }
        break;
    }
    return fault->dispatch_cb(fault->cb_user, receive, message, id, changed, len);
}
int sim_fault_tick(sim_fault_t *fault, uint32_t now_ms)
{
    fault->now_ms = now_ms;
    for (size_t idx = 0u; idx < SIM_FAULT_QUEUE_CAPACITY; idx++)
    {
        sim_fault_pending_t *pending = &fault->queue[idx];
        if (pending->occupied && (int32_t) (now_ms - pending->due_ms) >= 0)
        {
            pending->occupied = false;
            int rc = fault->dispatch_cb(fault->cb_user, pending->receive, pending->message, pending->id, pending->data, pending->len);
            if (rc != 0)
            {
                return rc;
            }
        }
    }
    return 0;
}

int sim_fault_manual(sim_fault_t *fault, const sim_fault_rule_t *rule, uint32_t now_ms)
{
    if (fault == NULL || (rule != NULL && sim_config_validate_fault(rule) != 0))
    {
        return -EINVAL;
    }
    for (size_t idx = 0u; idx < SIM_FAULT_QUEUE_CAPACITY; idx++)
    {
        if (fault->queue[idx].manual)
        {
            fault->queue[idx].occupied = false;
        }
    }
    fault->manual_enabled = rule != NULL;
    if (rule != NULL)
    {
        fault->manual = *rule;
        fault->manual_hits = 0u;
        fault->manual_start_ms = now_ms;
    }
    sim_log_event("FAULT_CONTROL", "source=manual enabled=%u", rule != NULL ? 1u : 0u);
    return 0;
}
void sim_fault_manual_status(const sim_fault_t *fault, bool *enabled, uint32_t *hits)
{
    *hits = fault->manual_hits;
    *enabled = fault->manual_enabled && (fault->manual.count == 0u || fault->manual_hits < fault->manual.count) &&
               (fault->manual.duration_ms == 0u || fault->now_ms - fault->manual_start_ms < fault->manual.after_ms + fault->manual.duration_ms);
}
void sim_fault_observe(sim_fault_t *fault, sim_fault_observer_t observer, void *user)
{
    if (fault != NULL)
    {
        fault->observer = observer;
        fault->observer_user = user;
    }
}
