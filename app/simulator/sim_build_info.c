/** @file sim_build_info.c @brief Only this translation unit depends on the generated build timestamp. */
#include "simulator/sim_build_info.h"
#include "sim_build_info_generated.h"

const sim_build_info_t *sim_build_info_get(void)
{
    static const sim_build_info_t s_build_info = {SIM_BUILD_VERSION, SIM_BUILD_TIME, SIM_BUILD_TARGET, SIM_BUILD_COMPILER};
    return &s_build_info;
}
