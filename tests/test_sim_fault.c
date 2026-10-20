/** @file test_sim_fault.c @brief Rule boundaries, bounded delayed ownership and no re-injection. */
#include "fault_injection/sim_fault.h"
#include <stdio.h>
#include <string.h>
#define TEST_FAULT_CHECK(expr)                                      \
    do                                                              \
    {                                                               \
        if (!(expr))                                                \
        {                                                           \
            fprintf(stderr, "FAIL: %s line=%d\n", #expr, __LINE__); \
            return 1;                                               \
        }                                                           \
    } while (0)
static unsigned s_test_fault_count;
static uint8_t s_test_fault_byte;
static int test_fault_dispatch(void *user, bool receive, const char *message, uint32_t id, const uint8_t *data, size_t len)
{
    (void) user;
    (void) receive;
    (void) message;
    (void) id;
    s_test_fault_count++;
    s_test_fault_byte = len == 0u ? 0u : data[0];
    return 0;
}
int main(void)
{
    sim_config_t config;
    sim_config_defaults(&config);
    config.fault_count = 1u;
    sim_fault_rule_t *rule = &config.faults[0];
    memcpy(rule->stage, "charging", 9u);
    memcpy(rule->message, "BCL", 4u);
    rule->action = SIM_FAULT_DROP;
    rule->after_ms = 100u;
    rule->duration_ms = 100u;
    uint8_t data[5] = {42u};
    sim_fault_t *fault = sim_fault_create(&config, test_fault_dispatch, NULL);
    TEST_FAULT_CHECK(fault != NULL);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 99u, 999u, 0u, data, 5u) == 0 && s_test_fault_count == 1u);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 100u, 1000u, 0u, data, 5u) == 0 && s_test_fault_count == 1u);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 199u, 1099u, 0u, data, 5u) == 0 && s_test_fault_count == 1u);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 200u, 1100u, 0u, data, 5u) == 0 && s_test_fault_count == 2u);
    sim_fault_destroy(fault);
    rule->after_ms = 0u;
    rule->duration_ms = 0u;
    rule->action = SIM_FAULT_DELAY;
    rule->delay_ms = 50u;
    fault = sim_fault_create(&config, test_fault_dispatch, NULL);
    TEST_FAULT_CHECK(fault != NULL);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 100u, 0u, data, 5u) == 0);
    data[0] = 99u;
    TEST_FAULT_CHECK(sim_fault_tick(fault, 149u) == 0 && s_test_fault_count == 2u);
    TEST_FAULT_CHECK(sim_fault_tick(fault, 150u) == 0 && s_test_fault_count == 3u && s_test_fault_byte == 42u);
    TEST_FAULT_CHECK(sim_fault_tick(fault, 200u) == 0 && s_test_fault_count == 3u);
    sim_fault_destroy(fault);
    rule->action = SIM_FAULT_DUPLICATE;
    rule->count = 1u;
    fault = sim_fault_create(&config, test_fault_dispatch, NULL);
    TEST_FAULT_CHECK(fault != NULL);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 0u, 0u, data, 5u) == 0 && s_test_fault_count == 5u);
    TEST_FAULT_CHECK(sim_fault_apply(fault, false, "BCL", "charging", 0u, 0u, 0u, data, 5u) == 0 && s_test_fault_count == 6u);
    sim_fault_destroy(fault);
    puts("PASS: fault time boundaries, copied delay payload and exact hit limit");
    return 0;
}
