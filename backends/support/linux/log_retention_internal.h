/** @file log_retention_internal.h @brief Explicit writers can withdraw old automatic-file ownership. */
#ifndef BACKENDS_SUPPORT_LINUX_LOG_RETENTION_INTERNAL_H
#define BACKENDS_SUPPORT_LINUX_LOG_RETENTION_INTERNAL_H

/** fd is borrowed and already holds an exclusive writer lock; never closed here.
 * If it refers to a previously managed application log, exempt the entire group.
 * Returns zero or negative errno; call before truncating an explicit file.
 */
int log_retention_protect_file(int fd);

#endif
