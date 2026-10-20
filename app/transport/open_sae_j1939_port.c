/**
 * @file open_sae_j1939_port.c
 * @brief Open-SAE-J1939 hardware callback adapter for the platform contracts.
 * @note No platform headers; all callbacks run on the sole protocol thread.
 */
#include "transport/open_sae_j1939_port.h"
#include "support/can_bus.h"
#include "osal/osal.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "Hardware/Hardware.h"

#define OPEN_SAE_J1939_CAN_ID_MAX 0x1FFFFFFFu
#define OPEN_SAE_J1939_FRAME_LEN 8u

static int s_open_sae_j1939_error;

/** Preserve the first error; upstream callbacks have no error return channel. */
static void open_sae_j1939_record_error(int error)
{
    if (s_open_sae_j1939_error == 0 && error != 0)
    {
        s_open_sae_j1939_error = error;
    }
}

/** Forward synchronous CAN sends and retain failures for the application. */
static void open_sae_j1939_send(uint32_t id, uint8_t dlc, uint8_t data[])
{
    if (id > OPEN_SAE_J1939_CAN_ID_MAX || data == NULL || dlc > OPEN_SAE_J1939_FRAME_LEN)
    {
        open_sae_j1939_record_error(-EINVAL);
        return;
    }
    if (s_open_sae_j1939_error == 0)
    {
        open_sae_j1939_record_error(can_bus_send(id, data, dlc));
    }
}

/** Forward full CAN frames only; upstream receive callbacks cannot carry DLC.
 *  Session-level validation is a separate prerequisite for production use.
 */
static void open_sae_j1939_read(uint32_t *out_id, uint8_t out_data[], bool *out_is_new)
{
    uint8_t dlc = 0u;
    int rc;

    *out_is_new = false;
    if (s_open_sae_j1939_error != 0)
    {
        return;
    }
    rc = can_bus_recv(out_id, out_data, &dlc);
    if (rc < 0)
    {
        open_sae_j1939_record_error(rc);
    }
    else if (rc == 1)
    {
        if (dlc != OPEN_SAE_J1939_FRAME_LEN || *out_id > OPEN_SAE_J1939_CAN_ID_MAX)
        {
            open_sae_j1939_record_error(-EMSGSIZE);
        }
        else
        {
            *out_is_new = true;
        }
    }
    else
    {
        /* No frame available; polling is non-blocking. */
    }
}

/** Preserve the upstream delay contract; TP scheduling is added separately. */
static void open_sae_j1939_delay(uint8_t delay_ms)
{
    osal_delay_ms(delay_ms);
}

void open_sae_j1939_port_init(void)
{
    s_open_sae_j1939_error = 0;
    CAN_Set_Callback_Functions(open_sae_j1939_send, open_sae_j1939_read, NULL, open_sae_j1939_delay);
}

int open_sae_j1939_port_error(void)
{
    return s_open_sae_j1939_error;
}
