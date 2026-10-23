cmake_minimum_required(VERSION 3.16)
project(gbt27930_simulator VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ===========================================================================
# Single-axis platform switch. See docs/architecture-spec.md.
#   linux  -> backends/osal/posix + backends/hal/linux + backends/support/linux
#   zephyr -> backends/osal/zephyr + backends/hal/zephyr + backends/support/zephyr
#             (compile-checked against cmake/zephyr_stubs; no Zephyr SDK needed)
# There is NO source-level #ifdef PLATFORM in the backends or the app.
# ===========================================================================
set(PLATFORM "linux" CACHE STRING "Target platform: linux|zephyr")
set_property(CACHE PLATFORM PROPERTY STRINGS linux zephyr)
if(NOT (PLATFORM STREQUAL "linux" OR PLATFORM STREQUAL "zephyr"))
    message(FATAL_ERROR "Unknown PLATFORM='${PLATFORM}' (expected: linux|zephyr)")
endif()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR})
option(SIM_UI "Build the Linux LVGL touch interface" OFF)
if(SIM_UI AND NOT PLATFORM STREQUAL "linux")
    message(FATAL_ERROR "SIM_UI is supported only on Linux; Zephyr remains a headless skeleton")
endif()


add_custom_target(sim_build_metadata
    COMMAND ${CMAKE_COMMAND}
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated"
        "-DSIM_VERSION=${PROJECT_VERSION}"
        "-DSIM_TARGET=${PLATFORM}/${CMAKE_SYSTEM_PROCESSOR}"
        "-DSIM_COMPILER=${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
        -P "${ROOT}/cmake/generate_build_info.cmake"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/sim_build_info_generated.h"
    VERBATIM)

# ---------------------------------------------------------------------------
# Platform libraries. Each backend exposes its contract include dir:
#   PUBLIC ${ROOT}               -> consumers write "osal/osal.h", "hal/hal_can.h"
#   PUBLIC ${CMAKE_CURRENT...}   -> that backend's private header ("*_priv.h")
# ---------------------------------------------------------------------------
if(PLATFORM STREQUAL "linux")
    message(STATUS "Platform: Linux (native POSIX)")
    find_package(Threads REQUIRED)

    add_library(scaffold_osal STATIC
        backends/osal/posix/osal_task.c
        backends/osal/posix/osal_mutex.c
        backends/osal/posix/osal_sem.c
        backends/osal/posix/osal_mq.c
        backends/osal/posix/osal_mem.c
        backends/osal/posix/osal_posix_internal.c)
    target_include_directories(scaffold_osal PUBLIC ${ROOT} ${ROOT}/backends/osal/posix)
    target_link_libraries(scaffold_osal PUBLIC Threads::Threads)

    add_library(scaffold_hal STATIC
        backends/hal/linux/hal_linux_priv.c
        backends/hal/linux/hal_i2c.c
        backends/hal/linux/hal_spi.c
        backends/hal/linux/hal_uart.c
        backends/hal/linux/hal_can.c
        backends/hal/linux/hal_gpio.c
        backends/hal/linux/hal_adc.c
        backends/hal/linux/hal_pwm.c
        backends/hal/linux/hal_flash.c
        backends/hal/linux/hal_rng.c)
    target_include_directories(scaffold_hal PUBLIC ${ROOT} ${ROOT}/backends/hal/linux)
    target_link_libraries(scaffold_hal PUBLIC Threads::Threads scaffold_osal)

    add_library(scaffold_support STATIC
        backends/support/linux/console.c
        backends/support/linux/args.c
        backends/support/linux/config_source.c
        backends/support/linux/can_bus.c)
    target_include_directories(scaffold_support PUBLIC ${ROOT})
    target_link_libraries(scaffold_support PUBLIC scaffold_osal scaffold_hal)

elseif(PLATFORM STREQUAL "zephyr")
    message(STATUS "Platform: Zephyr skeleton (compile-check; no SDK - cmake/zephyr_stubs)")
    set(ZSTUBS ${ROOT}/cmake/zephyr_stubs)

    add_library(scaffold_osal STATIC
        backends/osal/zephyr/osal_task.c
        backends/osal/zephyr/osal_mutex.c
        backends/osal/zephyr/osal_sem.c
        backends/osal/zephyr/osal_mq.c
        backends/osal/zephyr/osal_mem.c)
    target_include_directories(scaffold_osal PUBLIC
        ${ROOT} ${ROOT}/backends/osal/zephyr ${ZSTUBS})

    add_library(scaffold_hal STATIC
        backends/hal/zephyr/hal_i2c.c
        backends/hal/zephyr/hal_spi.c
        backends/hal/zephyr/hal_uart.c
        backends/hal/zephyr/hal_can.c
        backends/hal/zephyr/hal_gpio.c
        backends/hal/zephyr/hal_adc.c
        backends/hal/zephyr/hal_pwm.c
        backends/hal/zephyr/hal_flash.c
        backends/hal/zephyr/hal_rng.c)
    target_include_directories(scaffold_hal PUBLIC
        ${ROOT} ${ROOT}/backends/hal/zephyr ${ZSTUBS})
    target_link_libraries(scaffold_hal PUBLIC scaffold_osal)

    add_library(scaffold_support STATIC
        backends/support/zephyr/console.c
        backends/support/zephyr/args.c
        backends/support/zephyr/config_source.c
        backends/support/zephyr/can_bus.c)
    target_include_directories(scaffold_support PUBLIC ${ROOT} ${ZSTUBS})
    target_link_libraries(scaffold_support PUBLIC scaffold_osal scaffold_hal)
endif()

foreach(platform_target scaffold_osal scaffold_hal scaffold_support)
    target_compile_options(${platform_target} PRIVATE -Wall -Wextra)
endforeach()

# Vendored, pinned upstream library plus the platform-contract adapter.
include(cmake/open_sae_j1939.cmake)

add_library(cjson STATIC third_party/cjson/cJSON.c)
target_compile_definitions(cjson PRIVATE CJSON_NESTING_LIMIT=32)
target_include_directories(cjson SYSTEM PUBLIC ${ROOT}/third_party/cjson)
target_link_libraries(cjson PUBLIC m)
add_library(easylogger STATIC third_party/easylogger/src/elog.c third_party/easylogger/src/elog_utils.c
    third_party/easylogger/src/elog_async.c)
# Keep upstream unchanged; the project wrapper rejects overflow before upstream's truncation.
set_source_files_properties(third_party/easylogger/src/elog_async.c PROPERTIES
    COMPILE_DEFINITIONS "elog_async_output=elog_async_output_upstream")
target_include_directories(easylogger SYSTEM PUBLIC ${ROOT}/third_party/easylogger/inc)
target_include_directories(easylogger PUBLIC ${ROOT}/app/logging)
target_sources(scaffold_support PRIVATE backends/support/${PLATFORM}/config_store.c)

target_sources(scaffold_support PRIVATE backends/support/${PLATFORM}/log_sink.c)
target_sources(scaffold_support PRIVATE backends/support/${PLATFORM}/can_capture.c)
target_sources(scaffold_support PRIVATE backends/support/${PLATFORM}/log_retention.c)
set_source_files_properties(backends/support/${PLATFORM}/log_retention.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wstrict-prototypes")
set_source_files_properties(backends/support/${PLATFORM}/can_capture.c PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wstrict-prototypes")
add_library(sim_core STATIC app/protocol/gbt27930_codec.c app/config/sim_config.c
    app/config/sim_config_snapshot.c app/ui/sim_control.c app/simulator/sim_build_info.c app/simulator/sim_session.c
    app/logging/sim_log.c app/transport/j1939_tp.c app/fault_injection/sim_fault.c app/simulator/sim_engine.c)
add_dependencies(sim_core sim_build_metadata)
target_include_directories(sim_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_include_directories(sim_core PUBLIC ${ROOT}/app)
target_link_libraries(sim_core PUBLIC scaffold_support opensaej1939 cjson easylogger)
target_compile_options(sim_core PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wstrict-prototypes)

if(SIM_UI)
    include(cmake/lvgl.cmake)
    add_library(sim_ui STATIC app/ui/sim_ui.c app/ui/sim_ui_view.c backends/support/linux/ui_display.c)
    target_link_libraries(sim_ui PUBLIC sim_core lvgl)
else()
    add_library(sim_ui STATIC app/ui/sim_ui_none.c)
    target_link_libraries(sim_ui PUBLIC sim_core)
    if(PLATFORM STREQUAL "zephyr")
        target_sources(sim_ui PRIVATE backends/support/zephyr/ui_display.c)
    endif()
endif()
target_compile_options(sim_ui PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
add_executable(gbt27930_simulator app/simulator/sim_main.c)
target_link_libraries(gbt27930_simulator PRIVATE sim_ui)
target_compile_options(gbt27930_simulator PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
if(PLATFORM STREQUAL "zephyr")
    target_compile_definitions(gbt27930_simulator PRIVATE PLATFORM_ZEPHYR)
endif()

# ---------------------------------------------------------------------------
# Demo application (replace app/ with your own; keep it free of #ifdef PLATFORM
# except the entry-point signature, which the support layer cannot abstract).
# ---------------------------------------------------------------------------
add_executable(scaffold_demo app/main.c)
target_include_directories(scaffold_demo PRIVATE ${ROOT})
target_link_libraries(scaffold_demo PRIVATE scaffold_support)
target_compile_options(scaffold_demo PRIVATE -Wall -Wextra)
if(PLATFORM STREQUAL "zephyr")
    target_compile_definitions(scaffold_demo PRIVATE PLATFORM_ZEPHYR)
endif()

# Check integration without requiring a physical CAN controller.
include(CTest)
if(BUILD_TESTING AND PLATFORM STREQUAL "linux")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(test_log_retention tests/test_log_retention.c)
    target_link_libraries(test_log_retention PRIVATE scaffold_support)
    target_compile_options(test_log_retention PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME log_retention COMMAND test_log_retention)
    set_tests_properties(log_retention PROPERTIES TIMEOUT 15)
    add_executable(test_message_lifecycle tests/test_message_lifecycle.c)
    target_link_libraries(test_message_lifecycle PRIVATE sim_core)
    target_compile_options(test_message_lifecycle PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME message_lifecycle COMMAND test_message_lifecycle)
    add_executable(test_can_capture tests/test_can_capture.c)
    target_link_libraries(test_can_capture PRIVATE scaffold_support)
    target_compile_options(test_can_capture PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME can_capture COMMAND test_can_capture)
    set_tests_properties(can_capture PROPERTIES TIMEOUT 20)
    add_executable(test_sim_log tests/test_sim_log.c)
    target_link_libraries(test_sim_log PRIVATE sim_core)
    target_compile_options(test_sim_log PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME sim_log COMMAND test_sim_log)
    set_tests_properties(sim_log PROPERTIES TIMEOUT 10)
    add_executable(test_sim_fault tests/test_sim_fault.c)
    target_link_libraries(test_sim_fault PRIVATE sim_core)
    add_test(NAME sim_fault COMMAND test_sim_fault)
    add_executable(test_sim_engine tests/test_sim_engine.c)
    target_link_libraries(test_sim_engine PRIVATE sim_core)
    add_test(NAME sim_engine COMMAND test_sim_engine)
    add_test(NAME ui_startup COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_ui_startup.py $<TARGET_FILE:gbt27930_simulator> ${SIM_UI})
    set_tests_properties(ui_startup PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10 RESOURCE_LOCK vcan0)
    add_executable(test_control_concurrency tests/test_control_concurrency.c)
    target_link_libraries(test_control_concurrency PRIVATE sim_core)
    target_compile_options(test_control_concurrency PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME control_concurrency COMMAND test_control_concurrency)
    set_tests_properties(control_concurrency PROPERTIES TIMEOUT 20)
    add_executable(test_sim_control tests/test_sim_control.c)
    target_link_libraries(test_sim_control PRIVATE sim_core)
    target_compile_options(test_sim_control PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME sim_control COMMAND test_sim_control)
    if(SIM_UI)
        add_executable(test_ui_vcan_endpoint app/simulator/sim_main.c tests/ui_offscreen_backend.c)
        target_link_libraries(test_ui_vcan_endpoint PRIVATE sim_ui)
        target_compile_options(test_ui_vcan_endpoint PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
        add_test(NAME ui_vcan COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_ui_vcan.py $<TARGET_FILE:test_ui_vcan_endpoint>)
        set_tests_properties(ui_vcan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 45 RESOURCE_LOCK vcan0)
        add_executable(test_sim_ui tests/test_sim_ui.c)
        target_link_libraries(test_sim_ui PRIVATE sim_ui)
        target_compile_options(test_sim_ui PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
        add_test(NAME sim_ui COMMAND test_sim_ui ${CMAKE_BINARY_DIR} ${ROOT}/third_party/source_han_sans/ui_chars.txt)
        set_tests_properties(sim_ui PROPERTIES TIMEOUT 30)
    endif()
    add_executable(test_sim_session tests/test_sim_session.c)
    target_link_libraries(test_sim_session PRIVATE sim_core)
    target_compile_options(test_sim_session PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME sim_session COMMAND test_sim_session)
    add_test(NAME manual_sessions COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_manual_sessions.py $<TARGET_FILE:gbt27930_simulator>)
    set_tests_properties(manual_sessions PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 45 RESOURCE_LOCK vcan0)
    add_test(NAME simulator_cli COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/test_simulator_cli.py $<TARGET_FILE:gbt27930_simulator>)
    add_test(NAME simulator_scenarios COMMAND ${Python3_EXECUTABLE} ${ROOT}/tests/run_scenarios.py --build-dir ${CMAKE_BINARY_DIR})
    set_tests_properties(simulator_scenarios PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 RESOURCE_LOCK vcan0)
    add_executable(test_sim_core tests/test_sim_core.c)
    target_link_libraries(test_sim_core PRIVATE sim_core)
    add_test(NAME sim_core COMMAND test_sim_core)
    add_executable(test_open_sae_j1939_eom
        third_party/open_sae_j1939/tests/transport_eom.c)
    target_link_libraries(test_open_sae_j1939_eom PRIVATE opensaej1939)
    add_test(NAME open_sae_j1939_eom COMMAND test_open_sae_j1939_eom)
    set_tests_properties(open_sae_j1939_eom PROPERTIES TIMEOUT 10)

    add_executable(test_open_sae_j1939_port tests/test_open_sae_j1939_port.c)
    target_link_libraries(test_open_sae_j1939_port PRIVATE open_sae_j1939_port)
    target_compile_options(test_open_sae_j1939_port PRIVATE
        -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME open_sae_j1939_port COMMAND test_open_sae_j1939_port)
    set_tests_properties(open_sae_j1939_port PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10 RESOURCE_LOCK vcan0)
    add_test(NAME open_sae_j1939_send_failure COMMAND test_open_sae_j1939_port --closed-bus)
    set_tests_properties(open_sae_j1939_send_failure PROPERTIES TIMEOUT 10)

    add_executable(test_can_hal tests/test_can_hal.c)
    target_link_libraries(test_can_hal PRIVATE scaffold_support)
    target_compile_options(test_can_hal PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)
    add_test(NAME can_hal COMMAND test_can_hal)
    set_tests_properties(can_hal PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10 RESOURCE_LOCK vcan0)
    add_test(NAME platform_demo COMMAND scaffold_demo)
    set_tests_properties(platform_demo PROPERTIES WORKING_DIRECTORY "${ROOT}")
    add_test(NAME config_missing COMMAND scaffold_demo --config "${ROOT}/config/missing.json")
    set_tests_properties(config_missing PROPERTIES WILL_FAIL TRUE)
endif()
