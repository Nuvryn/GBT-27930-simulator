/** @file gb27930_codec.c
 * @brief Field layouts transcribed from local 2015/2023 tables 8..29.
 * @note Positive charging-current magnitudes map to the negative-current wire convention.
 */
#include "protocol/gb27930_codec.h"
#include <errno.h>
#include <string.h>
static const gb27930_descriptor_t s_gb27930_messages[] = {
    {"CHM", 0x2600u, 6u, 3u, 250u, 10000u, true}, {"BHM", 0x2700u, 6u, 2u, 250u, 10000u, false},
    {"CRM", 0x0100u, 6u, 8u, 250u, 5000u, true}, {"BRM", 0x0200u, 7u, 49u, 250u, 5000u, false},
    {"BCP", 0x0600u, 7u, 13u, 500u, 5000u, false}, {"CTS", 0x0700u, 6u, 7u, 500u, 5000u, true},
    {"CML", 0x0800u, 6u, 8u, 250u, 5000u, true}, {"BRO", 0x0900u, 4u, 1u, 250u, 5000u, false},
    {"CRO", 0x0A00u, 4u, 1u, 250u, 5000u, true}, {"BCL", 0x1000u, 6u, 5u, 50u, 1000u, false},
    {"BCS", 0x1100u, 7u, 9u, 250u, 5000u, false}, {"CCS", 0x1200u, 6u, 8u, 50u, 1000u, true},
    {"BSM", 0x1300u, 6u, 7u, 250u, 5000u, false}, {"BMV", 0x1500u, 7u, 0u, 10000u, 0u, false},
    {"BMT", 0x1600u, 7u, 0u, 10000u, 0u, false}, {"BSP", 0x1700u, 7u, 16u, 10000u, 0u, false},
    {"BST", 0x1900u, 4u, 4u, 10u, 5000u, false}, {"CST", 0x1A00u, 4u, 4u, 10u, 5000u, true},
    {"BSD", 0x1C00u, 6u, 7u, 250u, 10000u, false}, {"CSD", 0x1D00u, 6u, 8u, 250u, 10000u, true},
    {"BEM", 0x1E00u, 2u, 4u, 250u, 0u, false}, {"CEM", 0x1F00u, 2u, 4u, 250u, 0u, true}};
#define GB27930_F(key, msg, bit, width, scale, offset, min, max, initial, required) \
    {key, GB27930_##msg, bit, width, scale, offset, min, max, initial, required}
static const gb27930_field_t s_gb27930_fields[] = {
    GB27930_F("bhm_max_voltage_mv", BHM, 0, 16, 100, 0, 0, 750000, 750000, true),
    GB27930_F("crm_result", CRM, 0, 8, 1, 0, 0, 170, 0, true),
    GB27930_F("charger_id", CRM, 8, 32, 1, 0, 0, 4294967295LL, 1, true),
    GB27930_F("battery_type", BRM, 24, 8, 1, 0, 1, 255, 3, true),
    GB27930_F("battery_capacity_mah", BRM, 32, 16, 100, 0, 0, 1000000, 100000, true),
    GB27930_F("battery_rated_voltage_mv", BRM, 48, 16, 100, 0, 0, 750000, 400000, true),
    GB27930_F("battery_serial", BRM, 96, 32, 1, 0, 0, 4294967295LL, 1, false),
    GB27930_F("battery_year", BRM, 128, 8, 1, 1985, 1985, 2235, 2026, false),
    GB27930_F("battery_month", BRM, 136, 8, 1, 0, 1, 12, 1, false),
    GB27930_F("battery_day", BRM, 144, 8, 1, 0, 1, 31, 1, false),
    GB27930_F("battery_charge_count", BRM, 152, 24, 1, 0, 0, 16777214, 0, false),
    GB27930_F("battery_owner", BRM, 176, 8, 1, 0, 0, 1, 1, false),
    GB27930_F("bcp_cell_max_voltage_mv", BCP, 0, 16, 10, 0, 0, 24000, 3650, true),
    GB27930_F("bcp_max_current_ma", BCP, 16, 16, -100, 400000, 0, 400000, 100000, true),
    GB27930_F("battery_energy_wh", BCP, 32, 16, 100, 0, 0, 1000000, 50000, true),
    GB27930_F("bcp_max_voltage_mv", BCP, 48, 16, 100, 0, 0, 750000, 450000, true),
    GB27930_F("bcp_max_temperature_degc", BCP, 64, 8, 1, -50, -50, 200, 60, true),
    GB27930_F("bcp_soc_permille", BCP, 72, 16, 1, 0, 0, 1000, 500, true),
    GB27930_F("bcp_voltage_mv", BCP, 88, 16, 100, 0, 0, 750000, 400000, true),
    GB27930_F("cml_max_voltage_mv", CML, 0, 16, 100, 0, 0, 750000, 750000, true),
    GB27930_F("cml_min_voltage_mv", CML, 16, 16, 100, 0, 0, 750000, 200000, true),
    GB27930_F("cml_max_current_ma", CML, 32, 16, -100, 400000, 0, 400000, 200000, true),
    GB27930_F("cml_min_current_ma", CML, 48, 16, -100, 400000, 0, 400000, 0, true),
    GB27930_F("bro_ready", BRO, 0, 8, 1, 0, 0, 170, 170, true),
    GB27930_F("cro_ready", CRO, 0, 8, 1, 0, 0, 170, 170, true),
    GB27930_F("bcl_voltage_mv", BCL, 0, 16, 100, 0, 0, 750000, 420000, true),
    GB27930_F("bcl_current_ma", BCL, 16, 16, -100, 400000, 0, 400000, 100000, true),
    GB27930_F("bcl_mode", BCL, 32, 8, 1, 0, 1, 2, 2, true),
    GB27930_F("bcs_voltage_mv", BCS, 0, 16, 100, 0, 0, 750000, 400000, true),
    GB27930_F("bcs_current_ma", BCS, 16, 16, -100, 400000, 0, 400000, 100000, true),
    GB27930_F("bcs_cell_max_voltage_mv", BCS, 32, 12, 10, 0, 0, 24000, 3500, true),
    GB27930_F("bcs_cell_group", BCS, 44, 4, 1, 0, 0, 15, 0, true),
    GB27930_F("bcs_soc_pct", BCS, 48, 8, 1, 0, 0, 100, 50, true),
    GB27930_F("remaining_minutes", BCS, 56, 16, 1, 0, 0, 600, 30, true),
    GB27930_F("ccs_voltage_mv", CCS, 0, 16, 100, 0, 0, 750000, 400000, true),
    GB27930_F("ccs_current_ma", CCS, 16, 16, -100, 400000, 0, 400000, 100000, true),
    GB27930_F("ccs_elapsed_minutes", CCS, 32, 16, 1, 0, 0, 600, 0, true),
    GB27930_F("ccs_allow", CCS, 48, 2, 1, 0, 0, 1, 1, true),
    GB27930_F("bsm_cell_index", BSM, 0, 8, 1, 1, 1, 256, 1, true),
    GB27930_F("bsm_max_temperature_degc", BSM, 8, 8, 1, -50, -50, 200, 30, true),
    GB27930_F("bsm_max_temperature_index", BSM, 16, 8, 1, 1, 1, 128, 1, true),
    GB27930_F("bsm_min_temperature_degc", BSM, 24, 8, 1, -50, -50, 200, 25, true),
    GB27930_F("bsm_min_temperature_index", BSM, 32, 8, 1, 1, 1, 128, 1, true),
    GB27930_F("bsm_cell_voltage_state", BSM, 40, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_soc_state", BSM, 42, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_current_state", BSM, 44, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_temperature_state", BSM, 46, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_insulation_state", BSM, 48, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_connector_state", BSM, 50, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bsm_allow", BSM, 52, 2, 1, 0, 0, 1, 1, true),
    GB27930_F("bst_reason", BST, 0, 8, 1, 0, 0, 255, 0, true),
    GB27930_F("bst_fault", BST, 8, 16, 1, 0, 0, 65535, 0, true),
    GB27930_F("bst_error", BST, 24, 8, 1, 0, 0, 255, 240, true),
    GB27930_F("cst_reason", CST, 0, 8, 1, 0, 0, 255, 1, true),
    GB27930_F("cst_fault", CST, 8, 16, 1, 0, 0, 65535, 61440, true),
    GB27930_F("cst_error", CST, 24, 8, 1, 0, 0, 255, 240, true),
    GB27930_F("bsd_soc_pct", BSD, 0, 8, 1, 0, 0, 100, 50, true),
    GB27930_F("bsd_cell_min_voltage_mv", BSD, 8, 16, 10, 0, 0, 24000, 3300, true),
    GB27930_F("bsd_cell_max_voltage_mv", BSD, 24, 16, 10, 0, 0, 24000, 3500, true),
    GB27930_F("bsd_min_temperature_degc", BSD, 40, 8, 1, -50, -50, 200, 25, true),
    GB27930_F("bsd_max_temperature_degc", BSD, 48, 8, 1, -50, -50, 200, 30, true),
    GB27930_F("csd_elapsed_minutes", CSD, 0, 16, 1, 0, 0, 600, 0, true),
    GB27930_F("csd_energy_wh", CSD, 16, 16, 100, 0, 0, 1000000, 0, true),
    GB27930_F("csd_charger_id", CSD, 32, 32, 1, 1, 1, 4294967295LL, 1, true),
    GB27930_F("bem_crm_initial", BEM, 0, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_crm_confirm", BEM, 2, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_cml", BEM, 8, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_cro", BEM, 10, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_ccs", BEM, 16, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_cst", BEM, 18, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("bem_csd", BEM, 24, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_brm", CEM, 0, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bcp", CEM, 8, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bro", CEM, 10, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bcs", CEM, 16, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bcl", CEM, 18, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bst", CEM, 20, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bsd", CEM, 24, 2, 1, 0, 0, 2, 0, true),
    GB27930_F("cem_bsm", CEM, 26, 2, 1, 0, 0, 2, 0, false),
    GB27930_F("cts_second", CTS, 0, 8, 1, 0, 0, 59, 0, false),
    GB27930_F("cts_minute", CTS, 8, 8, 1, 0, 0, 59, 0, false),
    GB27930_F("cts_hour", CTS, 16, 8, 1, 0, 0, 23, 0, false),
    GB27930_F("cts_day", CTS, 24, 8, 1, 0, 1, 31, 1, false),
    GB27930_F("cts_month", CTS, 32, 8, 1, 0, 1, 12, 1, false),
    GB27930_F("cts_year", CTS, 40, 16, 1, 0, 1985, 9999, 2026, false)};
const gb27930_descriptor_t *gb27930_descriptor(gb27930_message_t message)
{
    return (unsigned) message < GB27930_MESSAGE_COUNT ? &s_gb27930_messages[message] : NULL;
}
const gb27930_field_t *gb27930_field(size_t index)
{
    return index < sizeof(s_gb27930_fields) / sizeof(s_gb27930_fields[0]) ? &s_gb27930_fields[index] : NULL;
}
gb27930_message_t gb27930_find_message(const char *name)
{
    for (unsigned idx = 0u; name != NULL && idx < GB27930_MESSAGE_COUNT; idx++)
    {
        if (strcmp(name, s_gb27930_messages[idx].name) == 0)
        {
            return (gb27930_message_t) idx;
        }
    }
    return GB27930_MESSAGE_COUNT;
}
gb27930_message_t gb27930_find_pgn(uint32_t pgn)
{
    for (unsigned idx = 0u; idx < GB27930_MESSAGE_COUNT; idx++)
    {
        if (s_gb27930_messages[idx].pgn == pgn)
        {
            return (gb27930_message_t) idx;
        }
    }
    return GB27930_MESSAGE_COUNT;
}
void gb27930_defaults(gb27930_values_t *values)
{
    if (values == NULL)
    {
        return;
    }
    memset(values, 0, sizeof(*values));
    for (size_t idx = 0u; gb27930_field(idx) != NULL; idx++)
    {
        values->field[idx] = s_gb27930_fields[idx].initial;
        values->available[idx] = true;
    }
    values->cell_count = 16u;
    values->temperature_count = 4u;
    for (size_t idx = 0u; idx < 256u; idx++)
    {
        values->cells_mv[idx] = 3500u;
    }
    for (size_t idx = 0u; idx < 128u; idx++)
    {
        values->temperatures_degc[idx] = 25;
    }
    memset(values->reserved, 0xFF, sizeof(values->reserved));
    memcpy(values->manufacturer, "SIMU", 4u);
    memcpy(values->vin, "SIMULATOR00000001", 17u);
    memset(values->software, 0xFF, 8u);
    memcpy(values->region, "000", 3u);
}
/** Physical range and exact representability are checked before narrowing. */
static bool gb27930_field_valid(const gb27930_field_t *field, int64_t value)
{
    if ((field->message == GB27930_BRO || field->message == GB27930_CRO || (field->message == GB27930_CRM && field->bit == 0u)) && value != 0 && value != 170)
    {
        return false;
    }
    if (field->message == GB27930_BRM && field->bit == 24u && value != 255 && (value < 1 || value > 8))
    {
        return false;
    }
    if (value < field->minimum || value > field->maximum || (value - field->offset) % field->scale != 0)
    {
        return false;
    }
    int64_t raw = (value - field->offset) / field->scale;
    return raw >= 0 && (uint64_t) raw < (UINT64_C(1) << field->width);
}
int gb27930_set(gb27930_values_t *values, const char *key, int64_t value)
{
    if (values == NULL || key == NULL)
    {
        return -EINVAL;
    }
    for (size_t idx = 0u; gb27930_field(idx) != NULL; idx++)
    {
        if (strcmp(key, s_gb27930_fields[idx].key) == 0)
        {
            if (!gb27930_field_valid(&s_gb27930_fields[idx], value))
            {
                return -ERANGE;
            }
            values->field[idx] = value;
            values->available[idx] = true;
            return 0;
        }
    }
    return -ENOENT;
}
int64_t gb27930_get(const gb27930_values_t *values, const char *key)
{
    for (size_t idx = 0u; values != NULL && key != NULL && gb27930_field(idx) != NULL; idx++)
    {
        if (strcmp(key, s_gb27930_fields[idx].key) == 0)
        {
            return values->field[idx];
        }
    }
    return 0;
}
/** Host-independent bit access, LSB-first within each byte (tables use one-based bits). */
static uint32_t gb27930_bits(const uint8_t *data, uint16_t bit, uint8_t width)
{
    uint32_t raw = 0u;
    for (uint8_t idx = 0u; idx < width; idx++)
    {
        raw |= ((uint32_t) (data[(bit + idx) / 8u] >> ((bit + idx) % 8u)) & 1u) << idx;
    }
    return raw;
}
static void gb27930_put(uint8_t *data, uint16_t bit, uint8_t width, uint32_t raw)
{
    for (uint8_t idx = 0u; idx < width; idx++)
    {
        uint16_t pos = (uint16_t) (bit + idx);
        uint8_t mask = (uint8_t) (1u << (pos % 8u));
        data[pos / 8u] = (uint8_t) ((data[pos / 8u] & (uint8_t) ~mask) | (((raw >> idx) & 1u) != 0u ? mask : 0u));
    }
}
/** Resolve version-specific and variable lengths before touching payload bytes. */
static size_t gb27930_length(gb27930_message_t message, unsigned version, const gb27930_values_t *values)
{
    if (message == GB27930_CCS && version == 2023u)
    {
        return 7u;
    }
    if (message == GB27930_BMV)
    {
        return values->cell_count * 2u;
    }
    if (message == GB27930_BMT)
    {
        return values->temperature_count;
    }
    return s_gb27930_messages[message].len;
}
int gb27930_encode(gb27930_message_t message, unsigned version, const gb27930_values_t *values,
    uint8_t *out_data, size_t capacity, size_t *out_len)
{
    if (gb27930_descriptor(message) == NULL || values == NULL || out_data == NULL || out_len == NULL || (version != 2015u && version != 2023u))
    {
        return -EINVAL;
    }
    size_t len = gb27930_length(message, version, values);
    if (len == 0u || len > capacity || len > GB27930_PAYLOAD_CAPACITY || values->cell_count > 256u || values->temperature_count > 128u)
    {
        return -EMSGSIZE;
    }
    memset(out_data, 0xFF, len);
    for (size_t idx = 0u; gb27930_field(idx) != NULL; idx++)
    {
        const gb27930_field_t *field = &s_gb27930_fields[idx];
        if (field->message != message || (version == 2015u && strcmp(field->key, "cem_bsm") == 0))
        {
            continue;
        }
        if (!gb27930_field_valid(field, values->field[idx]))
        {
            return -ERANGE;
        }
        if (!values->available[idx] && !field->required)
        {
            continue;
        }
        uint32_t raw = (uint32_t) ((values->field[idx] - field->offset) / field->scale);
        if (message == GB27930_CTS)
        {
            uint32_t bcd = 0u;
            for (unsigned nibble = 0u; raw != 0u; nibble++)
            {
                bcd |= (raw % 10u) << (nibble * 4u);
                raw /= 10u;
            }
            raw = bcd;
        }
        gb27930_put(out_data, field->bit, field->width, raw);
    }
    if (message == GB27930_CHM || message == GB27930_BRM)
    {
        out_data[0] = 1u;
        out_data[1] = 1u;
        out_data[2] = 0u;
    }
    if (message == GB27930_CRM)
    {
        memcpy(out_data + 5u, values->region, 3u);
    }
    if (message == GB27930_BRM)
    {
        memcpy(out_data + 8u, values->manufacturer, 4u);
        memcpy(out_data + 24u, values->vin, 17u);
        memcpy(out_data + 41u, values->software, 8u);
    }
    if (message == GB27930_BMV)
    {
        for (size_t idx = 0u; idx < values->cell_count; idx++)
        {
            if (values->cells_mv[idx] > 24000u || values->cells_mv[idx] % 10u != 0u)
            {
                return -ERANGE;
            }
            gb27930_put(out_data, (uint16_t) (idx * 16u), 16u, values->cells_mv[idx] / 10u);
        }
    }
    if (message == GB27930_BMT)
    {
        for (size_t idx = 0u; idx < len; idx++)
        {
            if (values->temperatures_degc[idx] < -50 || values->temperatures_degc[idx] > 200)
            {
                return -ERANGE;
            }
            out_data[idx] = (uint8_t) (values->temperatures_degc[idx] + 50);
        }
    }
    if (message == GB27930_BSP)
    {
        memcpy(out_data, values->reserved, 16u);
    }
    *out_len = len;
    return 0;
}
int gb27930_decode(gb27930_message_t message, unsigned version, const uint8_t *data, size_t len, gb27930_values_t *out_values)
{
    if (gb27930_descriptor(message) == NULL || data == NULL || out_values == NULL || (version != 2015u && version != 2023u))
    {
        return -EINVAL;
    }
    if (message == GB27930_BMV)
    {
        if (len < 2u || len > 512u || len % 2u != 0u)
        {
            return -EMSGSIZE;
        }
    }
    else if (message == GB27930_BMT)
    {
        if (len < 1u || len > 128u)
        {
            return -EMSGSIZE;
        }
    }
    else if (len != (message == GB27930_CCS && version == 2023u ? 7u : s_gb27930_messages[message].len))
    {
        return -EMSGSIZE;
    }
    gb27930_values_t decoded;
    gb27930_defaults(&decoded);
    for (size_t idx = 0u; gb27930_field(idx) != NULL; idx++)
    {
        const gb27930_field_t *field = &s_gb27930_fields[idx];
        if (field->message != message || (version == 2015u && strcmp(field->key, "cem_bsm") == 0))
        {
            continue;
        }
        uint32_t raw = gb27930_bits(data, field->bit, field->width);
        uint32_t unavailable = (uint32_t) ((UINT64_C(1) << field->width) - 1u);
        if (message == GB27930_CTS && raw == unavailable)
        {
            decoded.available[idx] = false;
            decoded.field[idx] = 0;
            continue;
        }
        if (message == GB27930_CTS)
        {
            uint32_t decimal = 0u, multiplier = 1u;
            for (unsigned nibble = 0u; nibble < field->width / 4u; nibble++)
            {
                uint32_t digit = (raw >> (nibble * 4u)) & 15u;
                if (digit > 9u)
                {
                    return -EBADMSG;
                }
                decimal += digit * multiplier;
                multiplier *= 10u;
            }
            raw = decimal;
        }
        int64_t value = (int64_t) raw * field->scale + field->offset;
        if (!gb27930_field_valid(field, value))
        {
            if (!field->required && raw == unavailable)
            {
                decoded.available[idx] = false;
                decoded.field[idx] = 0;
                continue;
            }
            return -EBADMSG;
        }
        decoded.field[idx] = value;
    }
    if ((message == GB27930_BRO || message == GB27930_CRO || message == GB27930_CRM) && data[0] != 0u && data[0] != 0xAAu)
    {
        return -EBADMSG;
    }
    if ((message == GB27930_CHM || message == GB27930_BRM) && (data[0] != 1u || data[1] != 1u || data[2] != 0u))
    {
        return -EPROTONOSUPPORT;
    }
    if (message == GB27930_CRM)
    {
        memcpy(decoded.region, data + 5u, 3u);
    }
    if (message == GB27930_BRM)
    {
        memcpy(decoded.manufacturer, data + 8u, 4u);
        memcpy(decoded.vin, data + 24u, 17u);
        memcpy(decoded.software, data + 41u, 8u);
    }
    if (message == GB27930_BMV)
    {
        decoded.cell_count = len / 2u;
        for (size_t idx = 0u; idx < decoded.cell_count; idx++)
        {
            uint32_t raw = gb27930_bits(data, (uint16_t) (idx * 16u), 12u);
            if (raw > 2400u)
            {
                return -EBADMSG;
            }
            decoded.cells_mv[idx] = (uint16_t) (raw * 10u);
        }
    }
    if (message == GB27930_BMT)
    {
        decoded.temperature_count = len;
        for (size_t idx = 0u; idx < len; idx++)
        {
            if (data[idx] > 250u)
            {
                return -EBADMSG;
            }
            decoded.temperatures_degc[idx] = (int16_t) ((int) data[idx] - 50);
        }
    }
    if (message == GB27930_BSP)
    {
        memcpy(decoded.reserved, data, 16u);
    }
    *out_values = decoded;
    return 0;
}
uint32_t gb27930_can_id(gb27930_message_t message, uint8_t source, uint8_t destination)
{
    const gb27930_descriptor_t *desc = gb27930_descriptor(message);
    return desc == NULL ? 0u : ((uint32_t) desc->priority << 26u) | (desc->pgn << 8u) | ((uint32_t) destination << 8u) | source;
}
