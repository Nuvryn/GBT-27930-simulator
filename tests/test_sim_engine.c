/** @file test_sim_engine.c
 * @brief Independent pair harness: controlled time and bounded in-memory CAN link.
 */
#include "simulator/sim_engine.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define TEST_ENGINE_QUEUE_CAPACITY 32768u
static sim_frame_t s_test_engine_frames[TEST_ENGINE_QUEUE_CAPACITY];
static size_t s_test_engine_head, s_test_engine_tail;
static int test_engine_send(void *user, const sim_frame_t *frame)
{
    (void) user;
    if (s_test_engine_tail >= TEST_ENGINE_QUEUE_CAPACITY)
    {
        return -ENOSPC;
    }
    s_test_engine_frames[s_test_engine_tail++] = *frame;
    return 0;
}
/** Only published frame I/O is used; no state-machine internals are inspected. */
static int test_engine_pair(unsigned version, gb27930_message_t dropped, bool expect_failure)
{
    sim_config_t charger, bms;
    sim_config_defaults(&charger);
    sim_config_defaults(&bms);
    charger.version = version;
    bms.version = version;
    bms.charger = false;
    bms.source = 0xF4u;
    bms.peer = 0x56u;
    charger.stop_after_ms = 1500u;
    bms.stop_after_ms = 0u;
    charger.max_session_ms = 20000u;
    bms.max_session_ms = 20000u;
    charger.retries = 0u;
    bms.retries = 0u;
    charger.recovery_wait_ms = 300u;
    bms.recovery_wait_ms = 300u;
    if ((unsigned) dropped < GB27930_MESSAGE_COUNT)
    {
        sim_config_t *source = gb27930_descriptor(dropped)->charger ? &charger : &bms;
        sim_config_t *destination = gb27930_descriptor(dropped)->charger ? &bms : &charger;
        destination->timeout_ms[dropped] = 500u;
        destination->first_timeout_ms[dropped] = 500u;
        source->fault_count = 1u;
        source->faults[0].action = SIM_FAULT_DROP;
        source->faults[0].count = 0u;
        memcpy(source->faults[0].stage, "*", 2u);
        const char *name = gb27930_descriptor(dropped)->name;
        memcpy(source->faults[0].message, name, strlen(name) + 1u);
    }
    s_test_engine_head = s_test_engine_tail = 0u;
    sim_engine_t *left = sim_engine_create(&charger, test_engine_send, NULL), *right = sim_engine_create(&bms, test_engine_send, NULL);
    if (left == NULL || right == NULL)
    {
        return 1;
    }
    int left_result = 0, right_result = 0;
    for (uint32_t now = 0u; now <= 20000u && (left_result == 0 || right_result == 0); now++)
    {
        if (left_result == 0)
        {
            left_result = sim_engine_tick(left, now);
        }
        if (right_result == 0)
        {
            right_result = sim_engine_tick(right, now);
        }
        while (s_test_engine_head < s_test_engine_tail)
        {
            sim_frame_t frame = s_test_engine_frames[s_test_engine_head++];
            int rc = sim_engine_receive((uint8_t) frame.id == 0x56u ? right : left, &frame, now);
            if (rc != 0)
            {
                left_result = rc;
                break;
            }
        }
    }
    sim_engine_destroy(left);
    sim_engine_destroy(right);
    int destination_result = (unsigned) dropped < GB27930_MESSAGE_COUNT && gb27930_descriptor(dropped)->charger ? right_result : left_result;
    if (expect_failure ? destination_result >= 0 : (left_result != 1 || right_result != 1))
    {
        fprintf(stderr, "FAIL: version=%u dropped=%d results=%d/%d\n", version, (int) dropped, left_result, right_result);
        return 1;
    }
    return 0;
}
int main(void)
{
    if (test_engine_pair(2015u, GB27930_MESSAGE_COUNT, false) != 0 || test_engine_pair(2023u, GB27930_MESSAGE_COUNT, false) != 0)
    {
        return 1;
    }
    const gb27930_message_t required[] = {GB27930_BRM, GB27930_BCP, GB27930_CML, GB27930_BRO, GB27930_CRO,
        GB27930_BCL, GB27930_BCS, GB27930_CCS, GB27930_BSM, GB27930_BST, GB27930_CST, GB27930_BSD, GB27930_CSD};
    for (size_t idx = 0u; idx < sizeof(required) / sizeof(required[0]); idx++)
    {
        if (test_engine_pair(2015u, required[idx], true) != 0)
        {
            return 1;
        }
    }
    if (test_engine_pair(2023u, GB27930_BSM, false) != 0)
    {
        return 1;
    }
    puts("PASS: two versions, required-message timeouts and 2023 BSM first-reception rule");
    return 0;
}
