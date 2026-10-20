/**
 * @file test_can_hal.c
 * @brief Verify the support adapter against an independent SocketCAN peer.
 * @note Linux integration test; requires an active vcan0. No protocol logic.
 */
#include "support/can_bus.h"
#include "osal/osal.h"

#include <errno.h>
#include <linux/can.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_CAN_ID 0x18EF56F4u
#define TEST_CAN_TIMEOUT_MS 2000u
#define TEST_CAN_SKIP 77

/** Exchange one extended frame in each direction; return zero on success. */
static int test_exchange(int peer_fd)
{
    const uint8_t payload[8] = {0x00u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0xFFu};
    struct can_frame frame = {0};
    struct pollfd event = {.fd = peer_fd, .events = POLLIN};
    uint8_t data[8] = {0};
    uint8_t dlc = 0u;
    uint32_t id = 0u;
    uint32_t start_ms;
    int rc;

    if (can_bus_send(TEST_CAN_ID, NULL, 1u) != -EINVAL ||
        can_bus_send(TEST_CAN_ID, payload, 9u) != -EINVAL)
    {
        return 1;
    }
    if (can_bus_send(TEST_CAN_ID, payload, (uint8_t) sizeof(payload)) != 0 ||
        poll(&event, 1u, (int) TEST_CAN_TIMEOUT_MS) != 1 ||
        read(peer_fd, &frame, sizeof(frame)) != (ssize_t) sizeof(frame))
    {
        return 1;
    }
    if (frame.can_id != (TEST_CAN_ID | CAN_EFF_FLAG) ||
        frame.can_dlc != sizeof(payload) || memcmp(frame.data, payload, sizeof(payload)) != 0)
    {
        return 1;
    }

    frame.can_id = (TEST_CAN_ID + 1u) | CAN_EFF_FLAG;
    if (write(peer_fd, &frame, sizeof(frame)) != (ssize_t) sizeof(frame))
    {
        return 1;
    }
    start_ms = osal_get_tick_ms();
    do
    {
        rc = can_bus_recv(&id, data, &dlc);
        if (rc != 0)
        {
            break;
        }
        osal_delay_ms(1u);
    } while ((uint32_t) (osal_get_tick_ms() - start_ms) < TEST_CAN_TIMEOUT_MS);

    return (rc == 1 && id == TEST_CAN_ID + 1u && dlc == sizeof(payload) &&
               memcmp(data, payload, sizeof(payload)) == 0 && can_bus_drop_count() == 0u)
               ? 0
               : 1;
}

/** Open independent endpoints; always close resources and distinguish SKIP. */
int main(void)
{
    struct sockaddr_can addr = {.can_family = AF_CAN};
    unsigned int index = if_nametoindex("vcan0");
    int peer_fd;
    int rc = 1;

    if (index == 0u)
    {
        puts("SKIP: vcan0 is unavailable");
        return TEST_CAN_SKIP;
    }
    peer_fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (peer_fd < 0)
    {
        perror("SocketCAN peer");
        return 1;
    }
    addr.can_ifindex = (int) index;
    if (bind(peer_fd, (const struct sockaddr *) &addr, sizeof(addr)) == 0)
    {
        if (can_bus_open("vcan0", false) == 0)
        {
            rc = test_exchange(peer_fd);
            can_bus_close();
        }
        else
        {
            puts("FAIL: can_bus_open failed");
        }
    }
    if (close(peer_fd) != 0)
    {
        rc = 1;
    }
    puts(rc == 0 ? "PASS: CAN adapter TX/RX and payload validation" : "FAIL: CAN integration");
    return rc;
}
