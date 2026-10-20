/** @file sim_frame.h
 *  @brief Platform-independent classic extended CAN frame; payload length in bytes.
 */
#ifndef COMMON_SIM_FRAME_H
#define COMMON_SIM_FRAME_H
#include <stdint.h>
typedef struct
{
    uint32_t id;
    uint8_t data[8];
    uint8_t len;
} sim_frame_t;
#endif
