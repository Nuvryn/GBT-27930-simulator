/** @file sim_build_info.h @brief Immutable metadata embedded by the build, with no runtime Git dependency. */
#ifndef APP_SIMULATOR_SIM_BUILD_INFO_H
#define APP_SIMULATOR_SIM_BUILD_INFO_H

typedef struct
{
    const char *version;
    const char *time_utc;
    const char *target;
    const char *compiler;
} sim_build_info_t;
/** Returns a non-NULL immutable object valid for the process lifetime. */
const sim_build_info_t *sim_build_info_get(void);

#endif
