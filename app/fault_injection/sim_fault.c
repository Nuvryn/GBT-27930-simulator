/** @file sim_fault.c @brief Single-thread rule execution, one-shot or repeated scenarios. */
#include "fault_injection/sim_fault.h"
#include "logging/sim_log.h"
#include "osal/osal.h"
#include <errno.h>
#include <string.h>
#define SIM_FAULT_QUEUE_CAPACITY 64u
typedef struct
{
    bool occupied;
    bool receive;
    char message[16];
    uint32_t due_ms;
    uint32_t id;
    size_t len;
    uint8_t data[GBT27930_PAYLOAD_CAPACITY];
} sim_fault_pending_t;
struct sim_fault_s
{
    const sim_config_t *config;
    sim_fault_dispatch_cb_t dispatch_cb;
    void *cb_user;
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
    for (size_t idx = 0u; idx < fault->config->fault_count; idx++)
    {
        const sim_fault_rule_t *rule = &fault->config->faults[idx];
        uint32_t elapsed = strcmp(rule->stage, "*") == 0 ? now_ms : stage_ms;
        if (rule->receive != receive || strcmp(rule->message, message) != 0 || (strcmp(rule->stage, "*") != 0 && strcmp(rule->stage, stage) != 0) ||
            elapsed < rule->after_ms || (rule->duration_ms != 0u && elapsed - rule->after_ms >= rule->duration_ms) || (rule->count != 0u && fault->hits[idx] >= rule->count))
        {
            continue;
        }
        fault->hits[idx]++;
        sim_log_event("FAULT", "rule=%u message=%s direction=%s action=%u hit=%u before_len=%u byte=%u before=%u after=%u", (unsigned) idx, message, receive ? "rx" : "tx",
            (unsigned) rule->action, (unsigned) fault->hits[idx], (unsigned) len, (unsigned) rule->byte, rule->byte < len ? (unsigned) changed[rule->byte] : 0u, (unsigned) rule->value);
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
