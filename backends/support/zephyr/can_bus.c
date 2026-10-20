/*
 * can_bus.c - Zephyr backend (SKELETON) for the project supplementary can_bus
 * adapter (contract: src/support/can_bus.h).
 *
 * SKELETON — see .omo/plans/framework-port.md (todo #9, D-A).
 *
 * This file MUST NOT include any <zephyr/...> header: it must stay
 * compilable on the host (MinGW) without a Zephyr SDK. The real Zephyr
 * implementation is deferred (framework's ports/hal/zephyr is also skeleton
 * in this plan). Every function returns -ENOSYS consistently rather than
 * faking success: a caller on Zephyr sees a loud, explicit unsupported
 * return, never a silent success.
 *
 * can_bus_close() still resets the project state so a later (future real)
 * implementation starts clean; can_bus_drop_count() reports 0.
 */
#include "support/can_bus.h"

#include <errno.h> /* -ENOSYS */

int can_bus_open(const char *ifname, bool use_virtual)
{
    /* Skeleton: no Zephyr CAN behind this yet. Consistently -ENOSYS. */
    (void)ifname;
    (void)use_virtual;
    return -ENOSYS;
}

int can_bus_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    /* Skeleton: never silently succeed; report unsupported even though no
     * bus has ever been opened (return is -ENOSYS in both cases). */
    (void)id;
    (void)data;
    (void)dlc;
    return -ENOSYS;
}

int can_bus_recv(uint32_t *id, uint8_t *data, uint8_t *dlc)
{
    /* Skeleton: -ENOSYS regardless of open state (consistent, non-silent). */
    (void)id;
    (void)data;
    (void)dlc;
    return -ENOSYS;
}

void can_bus_close(void)
{
    /* Skeleton: nothing was opened, but reset state anyway so a future real
     * implementation starts from a known-clean configuration. No-op. */
}

uint32_t can_bus_drop_count(void)
{
    /* Skeleton: no bus, no drops. */
    return 0u;
}