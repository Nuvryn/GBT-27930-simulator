/** @file sim_log.h @brief Protocol-thread logging wrapper over EasyLogger. */
#ifndef APP_LOGGING_SIM_LOG_H
#define APP_LOGGING_SIM_LOG_H
#include "common/sim_frame.h"
#include <stdbool.h>
#include <stdint.h>
/** Start logger/worker. Empty/NULL file path generates a local-time file name. */
int sim_log_open(bool file, const char *path);
/** Protocol elapsed time; does not affect wall clocks or protocol scheduling. */
void sim_log_time(uint32_t now_ms);
/** printf format; bounded to one line; call only from protocol thread. */
void sim_log_event(const char *tag, const char *format, ...);
/** Raw frame evidence, NULL ignored. */
void sim_log_frame(const char *direction, const sim_frame_t *frame);
/** Stop producer, drain/join worker, then close file. Returns any output/overflow error. */
int sim_log_close(void);
#endif
