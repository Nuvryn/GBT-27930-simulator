/** @file can_capture.h @brief Owned external CAN capture; no protocol parsing. */
#ifndef SUPPORT_CAN_CAPTURE_H
#define SUPPORT_CAN_CAPTURE_H
#include <stdbool.h>
/** Start candump -L -d on one literal interface. Empty path generates a Shanghai-time name.
 * Automatic naming requires role "bms" or "charger"; explicit paths ignore role.
 * One instance per process. Files are created exclusively, never overwritten.
 * Returns negative errno on tool/file/startup failure. Linux requires can-utils.
 */
int can_capture_start(const char *interface_name, const char *path, const char *role);
/** Thread-safe persistent error: unexpected child exit, stderr or output failure. */
int can_capture_error(void);
/** Actual capture file name, immutable until next start; empty before first start. */
const char *can_capture_path(void);
/** Stop child, drain its output, join worker and reap child. Safe if not started. */
int can_capture_stop(void);
#endif
