/** @file j1939_tp.c
 * @brief Session validation/timers; wire encoding and DT storage reused from upstream.
 * @note Upstream RTS advertises one DT per CTS; this adapter honors that window.
 */
#include "transport/j1939_tp.h"
#include "osal/osal.h"
#include "SAE_J1939/SAE_J1939-21_Transport_Layer/Transport_Layer.h"
#include <errno.h>
#include <string.h>

#define J1939_TP_CM 0xEC00u
#define J1939_TP_DT 0xEB00u
typedef struct
{
    J1939 node;
    bool active;
    bool ready;
    bool hold;
    uint16_t len;
    uint16_t next;
    uint16_t window_remaining;
    uint16_t window_size;
    uint16_t highest_sent;
    bool waiting_first;
    uint8_t packets;
    uint32_t pgn;
    uint32_t mark_ms;
} j1939_tp_session_t;
struct j1939_tp_s
{
    j1939_tp_options_t options;
    j1939_tp_session_t tx;
    j1939_tp_session_t rx;
    uint8_t completed[J1939_TP_CAPACITY];
    uint32_t last_dt_ms;
    bool has_sent_dt;
    bool suppress_cts;
    int send_error;
};
static j1939_tp_t *s_j1939_tp_active;

/** Forward the library's void callback while retaining the real send status. */
static void j1939_tp_upstream_send(uint32_t id, uint8_t dlc, uint8_t data[])
{
    sim_frame_t frame = {.id = id, .len = dlc};
    if (s_j1939_tp_active == NULL || dlc > sizeof(frame.data))
    {
        return;
    }
    memcpy(frame.data, data, dlc);
    if (s_j1939_tp_active->suppress_cts && ((id >> 16u) & 255u) == 0xECu && data[0] == 0x11u)
    {
        return;
    }
    if (s_j1939_tp_active->send_error == 0)
    {
        s_j1939_tp_active->send_error = s_j1939_tp_active->options.send_cb(s_j1939_tp_active->options.cb_user, &frame);
    }
}
/** RX is explicitly validated by the wrapper; never use upstream's raw listener. */
static void j1939_tp_upstream_read(uint32_t *id, uint8_t data[], bool *is_new)
{
    (void) id;
    (void) data;
    *is_new = false;
}
/** BAM is not part of this GB/T 27930 adapter; no blocking delay is permitted. */
static void j1939_tp_upstream_delay(uint8_t ms)
{
    (void) ms;
    if (s_j1939_tp_active != NULL)
    {
        s_j1939_tp_active->send_error = -ENOTSUP;
    }
}
/** Install callbacks for the current non-reentrant library call. */
static void j1939_tp_bind(j1939_tp_t *tp)
{
    s_j1939_tp_active = tp;
    tp->send_error = 0;
    CAN_Set_Callback_Functions(j1939_tp_upstream_send, j1939_tp_upstream_read, NULL, j1939_tp_upstream_delay);
}
/** Notify after clearing state, allowing the owner to schedule another message later. */
static void j1939_tp_finish(j1939_tp_t *tp, bool transmit, int result)
{
    j1939_tp_session_t *session = transmit ? &tp->tx : &tp->rx;
    uint32_t pgn = session->pgn;
    session->active = false;
    session->ready = false;
    if (tp->options.event_cb != NULL)
    {
        tp->options.event_cb(tp->options.cb_user, transmit, pgn, result);
    }
}
/** Use upstream to encode CTS/RTS/ACK/Abort. Only validated fields reach it. */
static int j1939_tp_control(j1939_tp_t *tp, j1939_tp_session_t *session, uint8_t control)
{
    session->node.information_this_ECU.this_ECU_address = tp->options.source;
    session->node.this_ecu_tp_cm.control_byte = control;
    session->node.this_ecu_tp_cm.PGN_of_the_packeted_message = session->pgn;
    j1939_tp_bind(tp);
    (void) SAE_J1939_Send_Transport_Protocol_Connection_Management(&session->node, tp->options.peer);
    s_j1939_tp_active = NULL;
    return tp->send_error;
}
/** Abort malformed/timed-out sessions with the GB/T-defined general reason 2/3. */
static int j1939_tp_abort(j1939_tp_t *tp, bool transmit, int result)
{
    j1939_tp_session_t *session = transmit ? &tp->tx : &tp->rx;
    session->node.this_ecu_tp_cm.connecting_abort_reason = result == -ETIMEDOUT ? 3u : 2u;
    session->node.this_ecu_tp_cm.connecting_abort_sender = (uint8_t) (transmit ? 0u : 1u);
    int rc = j1939_tp_control(tp, session, 0xFFu);
    j1939_tp_finish(tp, transmit, result);
    return rc != 0 ? rc : result;
}
j1939_tp_t *j1939_tp_create(const j1939_tp_options_t *options)
{
    if (options == NULL || options->send_cb == NULL || options->recv_cb == NULL ||
        options->source >= 254u || options->peer >= 254u || options->source == options->peer ||
        options->dt_interval_ms == 0u || options->t1_ms == 0u || options->t2_ms == 0u ||
        options->t3_ms == 0u || options->t4_ms == 0u)
    {
        return NULL;
    }
    j1939_tp_t *tp = osal_malloc(sizeof(*tp));
    if (tp != NULL)
    {
        memset(tp, 0, sizeof(*tp));
        tp->options = *options;
        if (tp->options.rx_window == 0u)
        {
            tp->options.rx_window = 16u;
        }
    }
    return tp;
}
void j1939_tp_destroy(j1939_tp_t *tp)
{
    osal_free(tp);
}
bool j1939_tp_busy(const j1939_tp_t *tp)
{
    return tp != NULL && tp->tx.active;
}
void j1939_tp_reset(j1939_tp_t *tp)
{
    if (tp != NULL)
    {
        memset(&tp->tx, 0, sizeof(tp->tx));
        memset(&tp->rx, 0, sizeof(tp->rx));
        tp->has_sent_dt = false;
    }
}
int j1939_tp_send(j1939_tp_t *tp, uint32_t pgn, const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (tp == NULL || data == NULL || len < 9u || len > J1939_TP_CAPACITY || pgn > 0x3FFFFu || (pgn & 0xFFu) != 0u)
    {
        return -EINVAL;
    }
    if (tp->tx.active)
    {
        return -EBUSY;
    }
    j1939_tp_session_t *tx = &tp->tx;
    memset(tx, 0, sizeof(*tx));
    tx->active = true;
    tx->pgn = pgn;
    tx->len = (uint16_t) len;
    tx->packets = (uint8_t) ((len + 6u) / 7u);
    tx->next = 1u;
    tx->mark_ms = now_ms;
    tx->node.this_ecu_tp_cm.total_message_size_being_transmitted = tx->len;
    tx->node.this_ecu_tp_cm.number_of_packages_being_transmitted = tx->packets;
    memcpy(tx->node.this_ecu_tp_dt.data, data, len);
    int rc = j1939_tp_control(tp, tx, 0x10u);
    if (rc != 0)
    {
        j1939_tp_finish(tp, true, rc);
    }
    return rc;
}
/** Validate RTS before any upstream writes; allocate no memory on the RX path. */
static int j1939_tp_rts(j1939_tp_t *tp, const uint8_t data[8], uint32_t pgn, uint32_t now_ms)
{
    uint16_t len = (uint16_t) ((uint16_t) data[1] | ((uint16_t) data[2] << 8u));
    if (tp->rx.active)
    {
        j1939_tp_session_t rejected = {0};
        rejected.pgn = pgn;
        rejected.node.this_ecu_tp_cm.connecting_abort_reason = 1u;
        rejected.node.this_ecu_tp_cm.connecting_abort_sender = 1u;
        int rc = j1939_tp_control(tp, &rejected, 0xFFu);
        return rc != 0 ? rc : -EBUSY;
    }
    j1939_tp_session_t *rx = &tp->rx;
    memset(rx, 0, sizeof(*rx));
    rx->pgn = pgn;
    if (len < 9u || len > J1939_TP_CAPACITY || data[3] != (len + 6u) / 7u || data[4] == 0u)
    {
        return j1939_tp_abort(tp, false, -EBADMSG);
    }
    rx->active = true;
    rx->len = len;
    rx->packets = data[3];
    rx->next = 1u;
    rx->mark_ms = now_ms;
    rx->window_size = tp->options.rx_window < data[4] ? tp->options.rx_window : data[4];
    if (rx->window_size > rx->packets)
    {
        rx->window_size = rx->packets;
    }
    rx->window_remaining = rx->window_size;
    rx->waiting_first = true;
    rx->node.from_other_ecu_tp_cm.control_byte = 0x10u;
    rx->node.from_other_ecu_tp_cm.total_message_size_being_transmitted = len;
    rx->node.from_other_ecu_tp_cm.number_of_packages_being_transmitted = data[3];
    /* PGN zero isolates upstream's application dispatch. Generic payload is delivered below. */
    rx->node.from_other_ecu_tp_cm.PGN_of_the_packeted_message = 0u;
    rx->node.this_ecu_tp_cm.number_of_packets_to_be_transmitted = (uint8_t) rx->window_size;
    rx->node.this_ecu_tp_cm.next_packet_number_transmitted = 1u;
    int rc = j1939_tp_control(tp, rx, 0x11u);
    if (rc != 0)
    {
        j1939_tp_finish(tp, false, rc);
    }
    return rc;
}
/** Keep packet indices in 16 bits: the terminal 255-packet session must not wrap. */
static int j1939_tp_dt(j1939_tp_t *tp, const sim_frame_t *frame, uint32_t now_ms)
{
    j1939_tp_session_t *rx = &tp->rx;
    if (!rx->active)
    {
        return -ENOENT;
    }
    if (frame->data[0] == 0u || frame->data[0] != rx->next || rx->next > rx->packets)
    {
        return j1939_tp_abort(tp, false, -EBADMSG);
    }
    size_t offset = ((size_t) rx->next - 1u) * 7u;
    size_t count = (size_t) rx->len - offset;
    if (count > 7u)
    {
        count = 7u;
    }
    memcpy(&tp->completed[offset], &frame->data[1], count);
    uint8_t data[8];
    memcpy(data, frame->data, sizeof(data));
    rx->window_remaining--;
    bool send_cts = rx->window_remaining == 0u;
    if (send_cts && rx->next < rx->packets)
    {
        uint16_t remaining = (uint16_t) (rx->packets - rx->next);
        rx->window_remaining = remaining < rx->window_size ? remaining : rx->window_size;
        rx->node.this_ecu_tp_cm.number_of_packets_to_be_transmitted = (uint8_t) rx->window_remaining;
    }
    tp->suppress_cts = !send_cts;
    j1939_tp_bind(tp);
    SAE_J1939_Read_Transport_Protocol_Data_Transfer(&rx->node, tp->options.peer, data);
    s_j1939_tp_active = NULL;
    tp->suppress_cts = false;
    if (tp->send_error != 0)
    {
        j1939_tp_finish(tp, false, tp->send_error);
        return tp->send_error;
    }
    rx->mark_ms = now_ms;
    rx->waiting_first = send_cts;
    rx->next++;
    if (rx->next > rx->packets)
    {
        tp->options.recv_cb(tp->options.cb_user, rx->pgn, tp->completed, rx->len);
        j1939_tp_finish(tp, false, 0);
    }
    return 0;
}
/** Match connection PGN before accepting CTS, EOM or Abort from the configured peer. */
static int j1939_tp_cm(j1939_tp_t *tp, const uint8_t data[8], uint32_t now_ms)
{
    uint32_t pgn = (uint32_t) data[5] | ((uint32_t) data[6] << 8u) | ((uint32_t) data[7] << 16u);
    j1939_tp_session_t *tx = &tp->tx;
    if (pgn > 0x3FFFFu || (pgn & 0xFFu) != 0u)
    {
        return -EBADMSG;
    }
    if (data[0] == 0x10u)
    {
        return j1939_tp_rts(tp, data, pgn, now_ms);
    }
    if (data[0] == 0xFFu)
    {
        if (tx->active && tx->pgn == pgn)
        {
            j1939_tp_finish(tp, true, -ECONNABORTED);
        }
        if (tp->rx.active && tp->rx.pgn == pgn)
        {
            j1939_tp_finish(tp, false, -ECONNABORTED);
        }
        return 0;
    }
    if (!tx->active || tx->pgn != pgn)
    {
        return -ENOENT;
    }
    if (data[0] == 0x11u)
    {
        if (tx->ready || data[1] > 1u || (data[1] != 0u && (data[2] == 0u || data[2] > tx->packets || data[2] > tx->next)))
        {
            return j1939_tp_abort(tp, true, -EBADMSG);
        }
        tx->ready = data[1] != 0u;
        if (tx->ready)
        {
            tx->next = data[2];
        }
        tx->hold = data[1] == 0u;
        tx->mark_ms = now_ms;
        return 0;
    }
    if (data[0] == 0x13u)
    {
        uint16_t len = (uint16_t) ((uint16_t) data[1] | ((uint16_t) data[2] << 8u));
        if (tx->highest_sent < tx->packets)
        {
            return 0;
        }
        if (len != tx->len || data[3] != tx->packets)
        {
            return j1939_tp_abort(tp, true, -EBADMSG);
        }
        j1939_tp_finish(tp, true, 0);
        return 0;
    }
    return -ENOTSUP;
}
int j1939_tp_receive(j1939_tp_t *tp, const sim_frame_t *frame, uint32_t now_ms)
{
    if (tp == NULL || frame == NULL || frame->id > 0x1FFFFFFFu)
    {
        return -EINVAL;
    }
    if ((uint8_t) frame->id != tp->options.peer || (uint8_t) (frame->id >> 8u) != tp->options.source)
    {
        return 0;
    }
    uint32_t pgn = (frame->id >> 8u) & 0x3FF00u;
    if (pgn != J1939_TP_CM && pgn != J1939_TP_DT)
    {
        return 0;
    }
    if (frame->len != 8u)
    {
        return -EMSGSIZE;
    }
    return pgn == J1939_TP_CM ? j1939_tp_cm(tp, frame->data, now_ms) : j1939_tp_dt(tp, frame, now_ms);
}
int j1939_tp_tick(j1939_tp_t *tp, uint32_t now_ms)
{
    if (tp == NULL)
    {
        return -EINVAL;
    }
    uint32_t receive_timeout_ms = tp->rx.waiting_first ? tp->options.t2_ms : tp->options.t1_ms;
    if (tp->rx.active && (uint32_t) (now_ms - tp->rx.mark_ms) >= receive_timeout_ms)
    {
        return j1939_tp_abort(tp, false, -ETIMEDOUT);
    }
    j1939_tp_session_t *tx = &tp->tx;
    uint32_t timeout_ms = tx->hold ? tp->options.t4_ms : tp->options.t3_ms;
    if (tx->active && !tx->ready && (uint32_t) (now_ms - tx->mark_ms) >= timeout_ms)
    {
        return j1939_tp_abort(tp, true, -ETIMEDOUT);
    }
    if (!tx->active || !tx->ready || (tp->has_sent_dt && (uint32_t) (now_ms - tp->last_dt_ms) < tp->options.dt_interval_ms))
    {
        return 0;
    }
    tx->node.from_other_ecu_tp_cm.control_byte = 0x11u;
    tx->node.from_other_ecu_tp_cm.number_of_packets_to_be_transmitted = 1u;
    tx->node.from_other_ecu_tp_cm.next_packet_number_transmitted = (uint8_t) tx->next;
    j1939_tp_bind(tp);
    (void) SAE_J1939_Send_Transport_Protocol_Data_Transfer(&tx->node, tp->options.peer);
    s_j1939_tp_active = NULL;
    if (tp->send_error != 0)
    {
        j1939_tp_finish(tp, true, tp->send_error);
        return tp->send_error;
    }
    if (tx->next > tx->highest_sent)
    {
        tx->highest_sent = tx->next;
    }
    tx->next++;
    tx->ready = false;
    tx->mark_ms = now_ms;
    tp->last_dt_ms = now_ms;
    tp->has_sent_dt = true;
    return 0;
}
