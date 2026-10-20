/** @file gbt27930_codec.h
 * @brief Explicit little-endian GB/T 27930 application payloads; no platform types.
 */
#ifndef APP_PROTOCOL_GBT27930_CODEC_H
#define APP_PROTOCOL_GBT27930_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GBT27930_FIELD_CAPACITY 128u
#define GBT27930_PAYLOAD_CAPACITY 1785u
typedef enum
{
    GBT27930_CHM,
    GBT27930_BHM,
    GBT27930_CRM,
    GBT27930_BRM,
    GBT27930_BCP,
    GBT27930_CTS,
    GBT27930_CML,
    GBT27930_BRO,
    GBT27930_CRO,
    GBT27930_BCL,
    GBT27930_BCS,
    GBT27930_CCS,
    GBT27930_BSM,
    GBT27930_BMV,
    GBT27930_BMT,
    GBT27930_BSP,
    GBT27930_BST,
    GBT27930_CST,
    GBT27930_BSD,
    GBT27930_CSD,
    GBT27930_BEM,
    GBT27930_CEM,
    GBT27930_MESSAGE_COUNT
} gbt27930_message_t;
typedef struct
{
    const char *name;
    uint32_t pgn;
    uint8_t priority;
    uint16_t len;
    uint32_t period_ms;
    uint32_t timeout_ms;
    bool charger;
} gbt27930_descriptor_t;
typedef struct
{
    const char *key;
    gbt27930_message_t message;
    uint16_t bit;
    uint8_t width;
    int32_t scale;
    int64_t offset;
    int64_t minimum;
    int64_t maximum;
    int64_t initial;
    bool required;
} gbt27930_field_t;
typedef struct
{
    int64_t field[GBT27930_FIELD_CAPACITY];
    bool available[GBT27930_FIELD_CAPACITY];
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
} gbt27930_values_t;
/** Static descriptors; NULL for invalid index. Lifetimes span the process. */
const gbt27930_descriptor_t *gbt27930_descriptor(gbt27930_message_t message);
const gbt27930_field_t *gbt27930_field(size_t index);
/** Resolve exact uppercase message mnemonic; MESSAGE_COUNT means unknown. */
gbt27930_message_t gbt27930_find_message(const char *name);
gbt27930_message_t gbt27930_find_pgn(uint32_t pgn);
/** Fill all physical values with documented defaults. Caller owns values; NULL allowed. */
void gbt27930_defaults(gbt27930_values_t *values);
/** Set/get a documented physical field by key. Set rejects nonrepresentable values. */
int gbt27930_set(gbt27930_values_t *values, const char *key, int64_t value);
int64_t gbt27930_get(const gbt27930_values_t *values, const char *key);
/** Encode into caller's buffer; capacity>=1785 recommended; out_len valid only on 0. */
int gbt27930_encode(gbt27930_message_t message, unsigned version, const gbt27930_values_t *values,
    uint8_t *out_data, size_t capacity, size_t *out_len);
/** Validate lengths/required fields before decoding; output valid only on 0. */
int gbt27930_decode(gbt27930_message_t message, unsigned version, const uint8_t *data,
    size_t len, gbt27930_values_t *out_values);
/** Construct an extended PDU1 CAN identifier. Caller validates addresses. */
uint32_t gbt27930_can_id(gbt27930_message_t message, uint8_t source, uint8_t destination);
#endif
