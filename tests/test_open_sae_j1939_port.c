/**
 * @file test_open_sae_j1939_port.c
 * @brief Exercise upstream TP output against an independent SocketCAN peer.
 * @note Only trusted frames enter upstream TP handlers in this smoke test.
 */
#include "transport/open_sae_j1939_port.h"
#include "support/can_bus.h"
#include "osal/osal.h"
#include "SAE_J1939/SAE_J1939-21_Transport_Layer/Transport_Layer.h"

#include <errno.h>
#include <linux/can.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define TEST_TP_LENGTH 49u
#define TEST_TP_PACKETS 7u
#define TEST_TP_SOURCE 0xF4u
#define TEST_TP_DESTINATION 0x56u
#define TEST_TP_PGN 0x000200u
#define TEST_TP_TX_CM_ID 0x1CEC56F4u
#define TEST_TP_RX_CM_ID 0x1CECF456u
#define TEST_TP_TX_DT_ID 0x1CEB56F4u
#define TEST_TP_TIMEOUT_MS 2000u

/** Receive one full raw CAN frame within the test deadline. */
static int test_read_peer(int peer_fd, struct can_frame *out_frame)
{
    struct pollfd event = {.fd = peer_fd, .events = POLLIN};
    return (poll(&event, 1u, (int) TEST_TP_TIMEOUT_MS) == 1 &&
               read(peer_fd, out_frame, sizeof(*out_frame)) == (ssize_t) sizeof(*out_frame))
               ? 0
               : 1;
}

/** Poll the library callback interface without invoking its protocol parser. */
static bool test_read_port(uint32_t *out_id, uint8_t out_data[8])
{
    uint32_t start_ms = osal_get_tick_ms();
    do
    {
        if (CAN_Read_Message(out_id, out_data))
        {
            return true;
        }
        if (open_sae_j1939_port_error() != 0)
        {
            break;
        }
        osal_delay_ms(1u);
    } while ((uint32_t) (osal_get_tick_ms() - start_ms) < TEST_TP_TIMEOUT_MS);
    return false;
}

/** Send a trusted CTS through vcan and let the upstream library emit its DT. */
static int test_send_packet(int peer_fd, J1939 *inout_node, uint8_t sequence)
{
    struct can_frame cts = {.can_id = TEST_TP_RX_CM_ID | CAN_EFF_FLAG, .can_dlc = 8u, .data = {0x11u, 1u, sequence, 0xFFu, 0xFFu, 0u, 2u, 0u}};
    struct can_frame dt = {0};
    uint8_t data[8] = {0};
    uint32_t id = 0u;
    size_t offset = ((size_t) sequence - 1u) * 7u;

    if (write(peer_fd, &cts, sizeof(cts)) != (ssize_t) sizeof(cts) ||
        !test_read_port(&id, data) || id != TEST_TP_RX_CM_ID || memcmp(data, cts.data, sizeof(data)) != 0)
    {
        return 1;
    }
    SAE_J1939_Read_Transport_Protocol_Connection_Management(inout_node, TEST_TP_DESTINATION, data);
    if (open_sae_j1939_port_error() != 0 || test_read_peer(peer_fd, &dt) != 0 ||
        dt.can_id != (TEST_TP_TX_DT_ID | CAN_EFF_FLAG) || dt.can_dlc != 8u ||
        dt.data[0] != sequence || memcmp(&dt.data[1], &inout_node->this_ecu_tp_dt.data[offset], 7u) != 0)
    {
        return 1;
    }
    return 0;
}

/** Verify BRM-sized TP output and reject a short frame at the callback boundary. */
static int test_transport(int peer_fd)
{
    J1939 node = {0};
    struct can_frame frame = {0};
    const uint8_t expected_rts[8] = {0x10u, TEST_TP_LENGTH, 0u, TEST_TP_PACKETS, 1u, 0u, 2u, 0u};
    uint8_t data[8] = {0};
    uint32_t id = 0u;

    node.information_this_ECU.this_ECU_address = TEST_TP_SOURCE;
    node.this_ecu_tp_cm.control_byte = CONTROL_BYTE_TP_CM_RTS;
    node.this_ecu_tp_cm.total_message_size_being_transmitted = TEST_TP_LENGTH;
    node.this_ecu_tp_cm.number_of_packages_being_transmitted = TEST_TP_PACKETS;
    node.this_ecu_tp_cm.PGN_of_the_packeted_message = TEST_TP_PGN;
    for (size_t idx = 0u; idx < TEST_TP_LENGTH; idx++)
    {
        node.this_ecu_tp_dt.data[idx] = (uint8_t) idx;
    }
    if (SAE_J1939_Send_Transport_Protocol_Connection_Management(&node, TEST_TP_DESTINATION) != STATUS_SEND_OK ||
        open_sae_j1939_port_error() != 0 || test_read_peer(peer_fd, &frame) != 0 ||
        frame.can_id != (TEST_TP_TX_CM_ID | CAN_EFF_FLAG) || frame.can_dlc != 8u ||
        memcmp(frame.data, expected_rts, sizeof(expected_rts)) != 0)
    {
        return 1;
    }
    for (uint8_t sequence = 1u; sequence <= TEST_TP_PACKETS; sequence++)
    {
        if (test_send_packet(peer_fd, &node, sequence) != 0)
        {
            return 1;
        }
    }

    /* A DLC-less upstream callback must never expose a short frame as full. */
    frame.can_id = TEST_TP_RX_CM_ID | CAN_EFF_FLAG;
    frame.can_dlc = 3u;
    if (write(peer_fd, &frame, sizeof(frame)) != (ssize_t) sizeof(frame) || test_read_port(&id, data))
    {
        return 1;
    }
    return open_sae_j1939_port_error() == -EMSGSIZE ? 0 : 1;
}

/** Confirm real send failures survive the upstream void callback convention. */
static int test_closed_bus(void)
{
    uint8_t data[8] = {0};
    open_sae_j1939_port_init();
    if (open_sae_j1939_port_error() != 0)
    {
        return 1;
    }
    (void) CAN_Send_Message(TEST_TP_TX_CM_ID, data);
    return open_sae_j1939_port_error() == -ENODEV ? 0 : 1;
}

/** Run offline failure checks or open an independent vcan peer for integration. */
int main(int argc, char **argv)
{
    struct sockaddr_can addr = {.can_family = AF_CAN};
    unsigned int index;
    int peer_fd;
    int rc = 1;

    if (argc == 2 && strcmp(argv[1], "--closed-bus") == 0)
    {
        return test_closed_bus();
    }
    index = if_nametoindex("vcan0");
    if (index == 0u)
    {
        puts("SKIP: vcan0 is unavailable");
        return 77;
    }
    peer_fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (peer_fd < 0)
    {
        return 1;
    }
    addr.can_ifindex = (int) index;
    if (bind(peer_fd, (const struct sockaddr *) &addr, sizeof(addr)) == 0 && can_bus_open("vcan0", false) == 0)
    {
        open_sae_j1939_port_init();
        rc = test_transport(peer_fd);
        can_bus_close();
    }
    if (close(peer_fd) != 0)
    {
        rc = 1;
    }
    puts(rc == 0 ? "PASS: upstream TP and CAN callback adapter" : "FAIL: upstream TP adapter");
    return rc;
}
