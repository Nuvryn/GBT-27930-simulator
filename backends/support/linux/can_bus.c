/*
 * can_bus.c - Linux (SocketCAN) backend for the project supplementary
 * can_bus adapter (contract: src/support/can_bus.h).
 *
 * Architecture decision (see .omo/plans/framework-port.md D-A, todo #9):
 *  - Framework hal_can contract vendored at hal/hal_can.h; real backend at
 *    ports/hal/linux/hal_can.c.
 *  - rx_cb runs on the backend worker pthread. It does NO protocol parsing:
 *    it only copies the frame into the project-owned 256-frame ring (the
 *    precedent size from the old src/platform/can_virtual.c) under an
 *    osal_mutex created on first use. The application main loop drains the
 *    ring with can_bus_recv() and does the routing/parsing.
 *  - DETERMINISTIC ORDERING: can_bus_send() calls
 *    hal_can_send(bus, &m, CAN_BUS_TX_TIMEOUT_MS, NULL, NULL). With
 *    done_cb == NULL the Linux backend blocks on an osal_sem until the
 *    deferred-flush reports its outcome (ports/hal/linux/hal_can.c), so one
 *    send completes before the next frame is queued. A non-NULL done_cb
 *    would move completion onto the SocketCAN worker pthread and break the
 *    order guarantee, therefore it is NEVER passed.
 *  - Error policy: errors are counted (s_send_errors) and surfaced through
 *    can_bus_drop_count(); at least one observable one-time stderr line per
 *    drop class is emitted. Nothing is silently ignored.
 *
 * hal_can lifecycle: there is NO destroy/close API in the framework contract
 * (framework issue I6). can_bus_close() therefore resets project-owned state
 * and detaches the RX filter via the existing hal_can_remove_filter; the
 * backend worker thread self-exits once idle and the socket fd is reclaimed
 * by the backend's own lifecycle / process exit. No hal API is invented.
 */
#include "support/can_bus.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>   /* IF_NAMESIZE */

#include "osal/osal.h"
#include "hal/hal_can.h"
/* hal_can_get_bus (device discovery) is declared in hal_dev.h, the
 * framework's discovery contract — not in hal_can.h. Keep both. */
#include "hal/hal_dev.h"

/* ------------------------------------------------------------------------- */
/* Tunables                                                                  */
/* ------------------------------------------------------------------------- */

/* Virtual interface name used when use_virtual == true. */
#ifndef CAN_BUS_VIRTUAL_IF
#define CAN_BUS_VIRTUAL_IF "vcan0"
#endif

/* TX timeout: conservative bound for one hal_can_send synchronous wait.
 * MUST exceed the hal_can worker poll cadence with margin; see DEVIATIONS.md
 * row #4 in ports/hal/linux (the worker observes a deferred-TX deadline only at
 * the top of its poll loop). */
#ifndef CAN_BUS_TX_TIMEOUT_MS
#define CAN_BUS_TX_TIMEOUT_MS 100
#endif
_Static_assert(CAN_BUS_TX_TIMEOUT_MS >= 100,
               "CAN_BUS_TX_TIMEOUT_MS must exceed the hal_can poll cadence with margin (DEVIATIONS.md #4)");

/* RX ring capacity (256-frame precedent from old src/platform/can_virtual.c,
 * now fed by the backend worker rx_cb instead of an in-process queue). */
#define CAN_BUS_RX_RING_CAP 256

/* ------------------------------------------------------------------------- */
/* Project-owned state (single shared bus: dual-role in-process topology)    */
/* ------------------------------------------------------------------------- */

typedef struct {
    uint32_t id;
    uint8_t  data[8];
    uint8_t  dlc;
} can_bus_rx_frame_t;

static osal_mutex_t      s_lock = NULL;         /* created on first use */
static hal_can_bus_t     s_bus  = NULL;         /* current hal_can handle */
static char              s_ifname[IF_NAMESIZE]; /* resolved interface bound */
static int               s_open = 0;            /* project-level open flag */
static int               s_filter_id = -1;      /* accept-all filter slot */

static can_bus_rx_frame_t s_ring[CAN_BUS_RX_RING_CAP];
static unsigned           s_head;   /* read index  */
static unsigned           s_tail;   /* write index */
static unsigned           s_count;  /* frames in ring */

static uint32_t s_drop_rx;        /* ring-full drops (rx path) */
static uint32_t s_drop_tx;        /* hal_can_send failures */
static int      s_rx_overflow_logged; /* one-time stderr per drop class */
static int      s_tx_error_logged;

/* ------------------------------------------------------------------------- */
/* RX callback (backend worker thread context only)                          */
/* ------------------------------------------------------------------------- */

/* Backend worker context: copies the frame into the ring, nothing more.
 * No protocol parsing, no business logging. On a full ring the NEW frame
 * is dropped and the drop counter incremented (documented policy:
 * ring-full = latest frame discarded, oldest preserved for the drainer). */
static void can_bus_rx_cb(void *user_data, const hal_can_msg_t *msg)
{
    can_bus_rx_frame_t *slot;
    (void)user_data;

    if (msg == NULL)
        return;

    /* Mutex is lazily created in can_bus_open; rx_cb only fires after a
     * successful open, so it is guaranteed non-NULL here. */
    (void)osal_mutex_lock(s_lock, OSAL_WAIT_FOREVER);

    if (s_count >= CAN_BUS_RX_RING_CAP) {
        s_drop_rx++;
        if (!s_rx_overflow_logged) {
            fprintf(stderr,
                    "[can_bus] rx ring full (%u), dropping new frame id=0x%08X\n",
                    s_drop_rx, (unsigned)msg->id);
            s_rx_overflow_logged = 1;
        }
        (void)osal_mutex_unlock(s_lock);
        return;
    }

    slot = &s_ring[s_tail];
    slot->id  = msg->id;
    slot->dlc = (msg->len > (uint8_t)sizeof(slot->data))
                    ? (uint8_t)sizeof(slot->data)
                    : msg->len;
    if (slot->dlc > 0)
        memcpy(slot->data, msg->data, slot->dlc);

    s_tail = (s_tail + 1) % CAN_BUS_RX_RING_CAP;
    s_count++;

    (void)osal_mutex_unlock(s_lock);
}

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

int can_bus_open(const char *ifname, bool use_virtual)
{
    const char *resolve;
    int         rc;

    if (use_virtual)
        resolve = CAN_BUS_VIRTUAL_IF;
    else if (ifname != NULL && ifname[0] != '\0')
        resolve = ifname;
    else
        return -EINVAL;

    if (s_open) {
        /* Idempotent open: same resolved interface -> no-op so a repeated
         * open does not open a second socket / register a second filter.
         * A different interface while open is refused; close first. */
        if (strcmp(s_ifname, resolve) == 0)
            return 0;
        return -EBUSY;
    }

    if (s_lock == NULL) {
        if (osal_mutex_create(&s_lock) != OSAL_OK)
            return -ENOMEM;
    }

    /* Framework backend: resolves if_nametoindex + opens + binds + starts
     * the worker. NULL => interface absent or socket failure. */
    s_bus = hal_can_get_bus(resolve);
    if (s_bus == NULL)
        return -ENODEV;

    /* Accept-all filter (NULL filter = accept all), cb = rx_cb. The return
     * is a filter slot id >= 0 on success, negative errno on failure. */
    rc = hal_can_add_rx_filter(s_bus, NULL, can_bus_rx_cb, NULL);
    if (rc < 0) {
        s_bus = NULL;
        return rc;
    }
    s_filter_id = rc;

    (void)strncpy(s_ifname, resolve, IF_NAMESIZE - 1);
    s_ifname[IF_NAMESIZE - 1] = '\0';
    s_head   = 0;
    s_tail   = 0;
    s_count  = 0;
    s_drop_rx = 0;
    s_drop_tx = 0;
    s_rx_overflow_logged = 0;
    s_tx_error_logged    = 0;
    s_open   = 1;

    return 0;
}

int can_bus_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    hal_can_msg_t m;
    int           rc;

    if (!s_open || s_bus == NULL)
        return -ENODEV;
    /* Validate rather than clamp: dlc > 8 is a protocol bug, rejects it. */
    if (dlc > (uint8_t)sizeof(m.data))
        return -EINVAL;
    if (dlc > 0 && data == NULL)
        return -EINVAL;

    memset(&m, 0, sizeof(m));
    m.id    = id;
    m.len   = dlc;
    m.flags = HAL_CAN_EXT_ID; /* use 29-bit extended IDs */
    if (dlc > 0)
        memcpy(m.data, data, dlc);

    /* done_cb ALWAYS NULL: deterministic order (see file header). */
    rc = hal_can_send(s_bus, &m, CAN_BUS_TX_TIMEOUT_MS, NULL, NULL);
    if (rc != 0) {
        (void)osal_mutex_lock(s_lock, OSAL_WAIT_FOREVER);
        s_drop_tx++;
        if (!s_tx_error_logged) {
            fprintf(stderr,
                    "[can_bus] hal_can_send failed (drop counter %u, rc=%d, "
                    "id=0x%08X)\n",
                    s_drop_tx, rc, (unsigned)id);
            s_tx_error_logged = 1;
        }
        (void)osal_mutex_unlock(s_lock);
    }
    return rc;
}

int can_bus_recv(uint32_t *id, uint8_t *data, uint8_t *dlc)
{
    if (id == NULL || data == NULL || dlc == NULL)
        return -EINVAL;
    if (!s_open || s_bus == NULL)
        return -ENODEV;

    (void)osal_mutex_lock(s_lock, OSAL_WAIT_FOREVER);

    if (s_count == 0) {
        (void)osal_mutex_unlock(s_lock);
        return 0; /* no frame, never blocks */
    }

    *id  = s_ring[s_head].id;
    *dlc = s_ring[s_head].dlc;
    if (*dlc > 0)
        memcpy(data, s_ring[s_head].data, *dlc);

    s_head = (s_head + 1) % CAN_BUS_RX_RING_CAP;
    s_count--;

    (void)osal_mutex_unlock(s_lock);
    return 1;
}

void can_bus_close(void)
{
    /* Detach our filter first: the backend stops delivering new frames to
     * can_bus_rx_cb. A callback already mid-flight from a concurrent drain
     * may still run; the mutex is deliberately NOT destroyed (it lives for
     * the process) so such a callback always finds valid state. */
    if (s_open && s_bus != NULL && s_filter_id >= 0) {
        (void)hal_can_remove_filter(s_bus, s_filter_id);
        s_filter_id = -1;
    }

    (void)osal_mutex_lock(s_lock, OSAL_WAIT_FOREVER);
    s_head     = 0;
    s_tail     = 0;
    s_count    = 0;
    s_drop_rx  = 0;
    s_drop_tx  = 0;
    s_rx_overflow_logged = 0;
    s_tx_error_logged    = 0;
    (void)osal_mutex_unlock(s_lock);

    /* Framework has no hal_can destroy (issue I6); the socket is reclaimed
     * by the backend worker idle self-exit / process exit. Drop the handle
     * and the open flag so the next can_bus_open() starts fresh. */
    s_ifname[0] = '\0';
    s_open      = 0;
    s_bus       = NULL;
}

uint32_t can_bus_drop_count(void)
{
    uint32_t drops;

    if (s_lock == NULL)
        return 0u;

    (void)osal_mutex_lock(s_lock, OSAL_WAIT_FOREVER);
    drops = s_drop_rx + s_drop_tx;
    (void)osal_mutex_unlock(s_lock);
    return drops;
}