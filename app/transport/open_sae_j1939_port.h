/**
 * @file open_sae_j1939_port.h
 * @brief Bind upstream hardware callbacks to the project's CAN and OSAL APIs.
 * @note Infrastructure only; does not validate TP sessions or implement timers.
 */
#ifndef TRANSPORT_OPEN_SAE_J1939_PORT_H
#define TRANSPORT_OPEN_SAE_J1939_PORT_H

/**
 * @brief Install process-global upstream callbacks and clear the error latch.
 * @note Call once before using the library, from the sole protocol thread.
 *       The caller owns can_bus_open/close. This function never opens a bus.
 *       Do not replace callbacks or call this function during a TP transfer.
 */
void open_sae_j1939_port_init(void);

/**
 * @brief Return the first transport error since initialization, or zero.
 * @return Negative errno on failure. The error remains latched until init.
 * @note Upstream's void send callback always reports STATUS_SEND_OK to its
 *       caller. Check this latch after library calls and stop on failure.
 *       Single protocol thread only; not callable from signals or interrupts.
 */
int open_sae_j1939_port_error(void);

#endif /* TRANSPORT_OPEN_SAE_J1939_PORT_H */
