/** @file gb27930_codec.h
 * @brief Explicit little-endian GB/T 27930 application payloads; no platform types.
 */
#ifndef APP_PROTOCOL_GB27930_CODEC_H
#define APP_PROTOCOL_GB27930_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GB27930_FIELD_CAPACITY 128u
#define GB27930_PAYLOAD_CAPACITY 1785u
typedef enum
{
    GB27930_CHM,
    GB27930_BHM,
    GB27930_CRM,
    GB27930_BRM,
    GB27930_BCP,
    GB27930_CTS,
    GB27930_CML,
    GB27930_BRO,
    GB27930_CRO,
    GB27930_BCL,
    GB27930_BCS,
    GB27930_CCS,
    GB27930_BSM,
    GB27930_BMV,
    GB27930_BMT,
    GB27930_BSP,
    GB27930_BST,
    GB27930_CST,
    GB27930_BSD,
    GB27930_CSD,
    GB27930_BEM,
    GB27930_CEM,
    GB27930_MESSAGE_COUNT
} gb27930_message_t;
typedef struct
{
    const char *name;
    uint32_t pgn;
    uint8_t priority;
    uint16_t len;
    uint32_t period_ms;
    uint32_t timeout_ms;
    bool charger;
} gb27930_descriptor_t;
typedef struct
{
    const char *key;
    gb27930_message_t message;
    uint16_t bit;
    uint8_t width;
    int32_t scale;
    int64_t offset;
    int64_t minimum;
    int64_t maximum;
    int64_t initial;
    bool required;
} gb27930_field_t;
typedef struct
{
    int64_t field[GB27930_FIELD_CAPACITY];
    bool available[GB27930_FIELD_CAPACITY];
    uint16_t cells_mv[256];
    uint8_t cell_groups[256];
    size_t cell_count;
    int16_t temperatures_degc[128];
    size_t temperature_count;
    uint8_t reserved[16];
    char manufacturer[4];
    char vin[17];
    uint8_t software[8];
    char region[3];
} gb27930_values_t;
/** Static descriptors; NULL for invalid index. Lifetimes span the process. */
const gb27930_descriptor_t *gb27930_descriptor(gb27930_message_t message);
const gb27930_field_t *gb27930_field(size_t index);
/** Resolve exact uppercase message mnemonic; MESSAGE_COUNT means unknown. */
gb27930_message_t gb27930_find_message(const char *name);
gb27930_message_t gb27930_find_pgn(uint32_t pgn);
/** Fill all physical values with documented defaults. Caller owns values; NULL allowed. */
void gb27930_defaults(gb27930_values_t *values);
/** Set/get a documented physical field by key. Set rejects nonrepresentable values. */
int gb27930_set(gb27930_values_t *values, const char *key, int64_t value);
int64_t gb27930_get(const gb27930_values_t *values, const char *key);
/** Encode into caller's buffer; capacity>=1785 recommended; out_len valid only on 0. */
int gb27930_encode(gb27930_message_t message, unsigned version, const gb27930_values_t *values,
    uint8_t *out_data, size_t capacity, size_t *out_len);
/** Validate lengths/required fields before decoding; output valid only on 0. */
int gb27930_decode(gb27930_message_t message, unsigned version, const uint8_t *data,
    size_t len, gb27930_values_t *out_values);
/** Construct an extended PDU1 CAN identifier. Caller validates addresses. */
uint32_t gb27930_can_id(gb27930_message_t message, uint8_t source, uint8_t destination);
#endif
