/** @file sim_config.h
 * @brief Validated startup configuration. Times are protocol milliseconds.
 */
#ifndef APP_CONFIG_SIM_CONFIG_H
#define APP_CONFIG_SIM_CONFIG_H
#include "protocol/gbt27930_codec.h"
#include <stdbool.h>
#include <stdint.h>
#define SIM_CONFIG_RULE_CAPACITY 32u
#define SIM_CONFIG_EVENT_CAPACITY 32u
typedef enum
{
    SIM_FAULT_DROP,
    SIM_FAULT_DELAY,
    SIM_FAULT_DUPLICATE,
    SIM_FAULT_CORRUPT,
    SIM_FAULT_LENGTH,
    SIM_FAULT_ID
} sim_fault_action_t;
typedef struct
{
    char stage[24];
    char message[16];
    bool receive;
    sim_fault_action_t action;
    uint32_t after_ms;
    uint32_t duration_ms;
    uint32_t count;
    uint32_t delay_ms;
    uint16_t byte;
    uint32_t value;
} sim_fault_rule_t;
typedef struct
{
    char stage[24];
    char action[24];
    char parameter[64];
    uint32_t after_ms;
    int64_t value;
} sim_event_t;
typedef struct
{
    bool charger;
    bool manual_start;
    bool ui_enabled;
    char ui_backend[8], ui_web_listen[16];
    unsigned ui_web_port, ui_web_vnc_port, ui_web_width, ui_web_height, ui_web_fps;
    char ui_framebuffer[256];
    char ui_input_device[256];
    unsigned ui_rotation;
    unsigned version;
    char can_interface[16];
    uint8_t source;
    uint8_t peer;
    unsigned speed;
    bool software_test;
    bool file_log;
    char log_path[256];
    bool candump;
    char candump_path[256];
    bool self_test_pass;
    bool parameters_match;
    bool precharge_pass;
    bool ready;
    bool reconnect_allowed;
    bool auto_aux_power_off;
    uint32_t aux_power_off_delay_ms;
    uint32_t ready_delay_ms;
    uint32_t ready_timeout_ms;
    uint32_t stop_after_ms;
    uint32_t max_session_ms;
    uint32_t recovery_wait_ms;
    unsigned retries;
    uint32_t dt_interval_ms;
    uint32_t tp_t1_ms;
    uint32_t tp_t2_ms;
    uint32_t tp_t3_ms;
    uint32_t tp_t4_ms;
    uint8_t tp_rx_window;
    uint32_t period_ms[GBT27930_MESSAGE_COUNT];
    uint32_t timeout_ms[GBT27930_MESSAGE_COUNT];
    uint32_t first_timeout_ms[GBT27930_MESSAGE_COUNT];
    bool enabled[GBT27930_MESSAGE_COUNT];
    gbt27930_values_t values;
    size_t cell_group_count;
    sim_fault_rule_t faults[SIM_CONFIG_RULE_CAPACITY];
    size_t fault_count;
    sim_event_t events[SIM_CONFIG_EVENT_CAPACITY];
    size_t event_count;
} sim_config_t;
int sim_config_validate_fault(const sim_fault_rule_t *rule);
int sim_config_validate_values(const sim_config_t *config);
/** Defaults are usable values; role/version still required in JSON. */
void sim_config_defaults(sim_config_t *out_config);
/** Parse strict JSON (unknown/duplicate keys rejected). Error buffer required, >=1 byte. */
int sim_config_parse(const char *text, sim_config_t *out_config, char *out_error, size_t error_capacity);
/** Configuration-source wrapper; ownership of source text remains internal. */
int sim_config_load(const char *path, sim_config_t *out_config, char *out_error, size_t error_capacity);
/** Receives non-NUL-terminated JSON chunks <=512 bytes; concatenate without separators.
 * Buffer is borrowed for this call only. Return zero or negative errno to abort.
 */
typedef int (*sim_config_emit_cb_t)(const char *data, size_t len, void *cb_user);
/** Serialize a validated effective configuration, including defaults, for startup evidence.
 * No I/O is performed. All pointers are required except cb_user. Allocates only during this call.
 */
int sim_config_snapshot(const sim_config_t *config, sim_config_emit_cb_t emit_cb, void *cb_user);
#endif
