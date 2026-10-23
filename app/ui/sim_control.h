/** @file sim_control.h @brief Bounded UI/protocol bridge, independent of LVGL. */
#ifndef SIM_CONTROL_H
#define SIM_CONTROL_H
#include "simulator/sim_session.h"
#define SIM_CONTROL_QUEUE 8u
#define SIM_UI_LOG_LINES 224u
#define SIM_UI_LOG_LINE_SIZE 1152u
typedef struct sim_control_s sim_control_t;
typedef struct
{
    unsigned id;
    int result;
} sim_control_result_t;
typedef enum
{
    SIM_CONTROL_START,
    SIM_CONTROL_APPLY,
    SIM_CONTROL_FAULT,
    SIM_CONTROL_CANCEL
} sim_control_kind_t;
typedef struct
{
    sim_control_kind_t kind;
    unsigned id, session_id;
    gbt27930_values_t values;
    sim_fault_rule_t fault;
} sim_control_request_t;
typedef struct
{
    sim_status_t status;
    sim_config_t config;
    unsigned request_id, saved_revision;
    int request_result, save_result, stop_result;
    unsigned stop_sequence;
    bool saving;
    uint32_t log_sequence, log_dropped;
} sim_control_view_t;
sim_control_t *sim_control_create(const sim_config_t *config, const char *config_path);
void sim_control_destroy(sim_control_t *control);
int sim_control_submit(sim_control_t *control, sim_control_request_t *request);
int sim_control_result(sim_control_t *control, sim_control_result_t *result);
void sim_control_stop(sim_control_t *control, unsigned session_id);
int sim_control_read(sim_control_t *control, sim_control_view_t *view);
/** Protocol thread: never waits for a UI lock. Processes controls before CAN RX. */
int sim_control_step(sim_control_t *control, sim_session_t *session, uint32_t now_ms);
int sim_control_publish(sim_control_t *control, sim_session_t *session);
void sim_control_log(void *control, const char *tag, const char *message, uint32_t now_ms);
int sim_control_log_read(sim_control_t *control, uint32_t *cursor, char *line, size_t capacity);
int sim_control_save(sim_control_t *control);
/** Worker thread only; may serialize and perform file I/O. */
void sim_control_work(sim_control_t *control);
int sim_control_error(sim_control_t *control);
/** Exact decimal conversion; no floating point rounding at field resolution. */
int sim_control_parse_value(const gbt27930_field_t *field, const char *text, int64_t *value);
void sim_control_format_value(const gbt27930_field_t *field, int64_t value, char *text, size_t capacity);
#endif
