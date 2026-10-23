/** @file log_retention.h @brief Retain the latest three automatic sessions per role. */
#ifndef SUPPORT_LOG_RETENTION_H
#define SUPPORT_LOG_RETENTION_H

/** Register successfully created automatic files; NULL/empty paths are ignored.
 * role must be "bms" or "charger" when a path is supplied. Paths must use the
 * generated logs/<Shanghai timestamp>_<role> naming scheme. Never pass explicit
 * user paths. May be called again to add the same session's capture files.
 * One session per process; call from the startup thread, not the protocol loop.
 * Returns zero or negative errno. Cleanup failures are visible to the caller.
 */
int log_retention_track(const char *role, const char *application_path, const char *capture_path);
/** Release the session lease after all output files are closed. Safe if unused. */
void log_retention_close(void);

#endif
