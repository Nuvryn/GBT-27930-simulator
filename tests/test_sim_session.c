/** @file test_sim_session.c @brief Repeated sessions with controlled time and queued frame I/O. */
#include "simulator/sim_session.h"
#include <errno.h>
#include <stdio.h>
#define CHECK(expr)                                            \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #expr); \
            return 1;                                          \
        }                                                      \
    } while (0)
static sim_frame_t frames[32768];
static size_t head, tail;
static int send_frame(void *user, const sim_frame_t *frame)
{
    (void) user;
    if (tail >= sizeof(frames) / sizeof(frames[0]))
    {
        return -ENOSPC;
    }
    frames[tail++] = *frame;
    return 0;
}
static int pair(unsigned version)
{
    sim_config_t charger, bms;
    sim_config_defaults(&charger);
    charger.manual_start = true;
    charger.version = version;
    charger.stop_after_ms = 1000u;
    charger.max_session_ms = 15000u;
    bms = charger;
    bms.charger = false;
    bms.source = 0xF4u;
    bms.peer = 0x56u;
    bms.stop_after_ms = 0u;
    sim_session_t *left = sim_session_create(&charger, send_frame, NULL);
    sim_session_t *right = sim_session_create(&bms, send_frame, NULL);
    CHECK(left != NULL && right != NULL);
    uint32_t now = 100000u;
    CHECK(sim_session_tick(left, now) == 0 && sim_session_tick(right, now) == 0 && tail == 0u);
    sim_frame_t chm = {.id = 0x1826F456u, .len = 2u, .data = {1u, 1u, 0u}};
    CHECK(sim_session_receive(right, &chm, now) == 0 && !sim_session_active(right));
    chm.len = 3u;
    chm.id = 0x1826F455u;
    CHECK(sim_session_receive(right, &chm, now) == 0 && !sim_session_active(right));
    for (unsigned round = 0u; round < 3u; round++)
    {
        head = tail = 0u;
        CHECK(sim_session_start(left, now) == 0);
        CHECK(sim_session_start(left, now) == -EBUSY);
        int lrc = 0, rrc = 0;
        for (uint32_t elapsed = 0u; elapsed < 15000u && (lrc == 0 || rrc == 0); elapsed++, now++)
        {
            if (lrc == 0)
            {
                lrc = sim_session_tick(left, now);
            }
            if (rrc == 0)
            {
                rrc = sim_session_tick(right, now);
            }
            while (head < tail)
            {
                sim_frame_t frame = frames[head++];
                CHECK(sim_session_receive((uint8_t) frame.id == 0x56u ? right : left, &frame, now) == 0);
            }
        }
        CHECK(lrc == 1 && rrc == 1);
        CHECK(!sim_session_active(left) && !sim_session_active(right));
        size_t sent = tail;
        now += 100000u;
        CHECK(sim_session_tick(left, now) == 0 && sim_session_tick(right, now) == 0 && tail == sent);
    }
    sim_session_destroy(left);
    sim_session_destroy(right);
    return 0;
}
static int timeout_restart(void)
{
    sim_config_t config;
    char error[256];
    CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015}", &config, error, sizeof(error)) == 0 && !config.manual_start);
    CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015,\"start_mode\":\"manual\",\"retries\":9,\"reconnect_allowed\":true}", &config, error, sizeof(error)) == 0);
    CHECK(config.manual_start && config.retries == 0u && !config.reconnect_allowed);
    CHECK(sim_config_parse("{\"role\":\"bms\",\"version\":2015,\"start_mode\":\"bad\"}", &config, error, sizeof(error)) != 0);
    sim_config_defaults(&config);
    config.manual_start = true;
    config.charger = false;
    config.source = 0xF4u;
    config.peer = 0x56u;
    config.max_session_ms = 100u;
    sim_session_t *session = sim_session_create(&config, send_frame, NULL);
    CHECK(session != NULL);
    sim_frame_t chm = {.id = 0x1826F456u, .len = 3u, .data = {1u, 1u, 0u}};
    CHECK(sim_session_receive(session, &chm, 10000u) == 0 && sim_session_active(session));
    CHECK(sim_session_tick(session, 10100u) < 0 && !sim_session_active(session));
    CHECK(sim_session_receive(session, &chm, 10101u) == 0 && !sim_session_active(session));
    CHECK(sim_session_receive(session, &chm, 11100u) == 0 && !sim_session_active(session));
    CHECK(sim_session_receive(session, &chm, 12100u) == 0 && sim_session_active(session));
    CHECK(sim_session_tick(session, 12199u) == 0);
    CHECK(sim_session_tick(session, 12200u) < 0 && !sim_session_active(session));
    sim_session_destroy(session);
    return 0;
}
int main(void)
{
    CHECK(pair(2015u) == 0);
    head = tail = 0u;
    CHECK(pair(2023u) == 0);
    CHECK(timeout_restart() == 0);
    puts("PASS: repeated sessions, idle waits, CHM validation, failure reset and configuration");
    return 0;
}
