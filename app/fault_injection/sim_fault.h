/** @file sim_fault.h @brief Bounded deterministic injection at payload/frame boundaries. */
#ifndef APP_FAULT_INJECTION_SIM_FAULT_H
#define APP_FAULT_INJECTION_SIM_FAULT_H
#include "config/sim_config.h"
typedef struct sim_fault_s sim_fault_t;

typedef struct
{
    bool manual, receive;
    unsigned rule;
    sim_fault_action_t action;
    uint32_t at_ms, delay_ms, value;
    uint16_t byte;
    uint8_t before;
    size_t length;
    char message[16];
} sim_fault_evidence_t;
typedef void (*sim_fault_observer_t)(void *user, const sim_fault_evidence_t *evidence);
/** Optional synchronous evidence hook; called once per hit before dispatch. */
void sim_fault_observe(sim_fault_t *fault, sim_fault_observer_t observer, void *user);
typedef int (*sim_fault_dispatch_cb_t)(void *cb_user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len);
/** Allocate bounded delay queue at startup. Rules/config must outlive the injector. */
sim_fault_t *sim_fault_create(const sim_config_t *config, sim_fault_dispatch_cb_t dispatch_cb, void *cb_user);
void sim_fault_destroy(sim_fault_t *fault);
/** Apply first eligible rule in declaration order; delays/duplicates never re-enter rules. */
int sim_fault_apply(sim_fault_t *fault, bool receive, const char *message, const char *stage, uint32_t stage_ms,
    uint32_t now_ms, uint32_t id, const uint8_t *data, size_t len);
/** Deliver due items; no dynamic allocations. Queue exhaustion is an explicit failure. */
int sim_fault_tick(sim_fault_t *fault, uint32_t now_ms);
/** Clear pending delayed frames at restart; retain evidence counters. */
void sim_fault_cancel(sim_fault_t *fault);
/** Replace one temporary rule; NULL disables and cancels only its pending tasks. */
int sim_fault_manual(sim_fault_t *fault, const sim_fault_rule_t *rule, uint32_t now_ms);
void sim_fault_manual_status(const sim_fault_t *fault, bool *enabled, uint32_t *hits);
#endif
