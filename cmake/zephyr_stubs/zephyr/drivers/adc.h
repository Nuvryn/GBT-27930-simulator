/*
 * adc.h - MOCK STUB for the Zephyr ADC driver API (host-side testing only).
 *
 * Mirrors the subset used by hal_adc.c (Zephyr v4.x):
 *   enum adc_gain / enum adc_reference / ADC_ACQ_TIME_DEFAULT
 *   struct adc_channel_cfg / struct adc_sequence / struct adc_sequence_options
 *   adc_channel_setup / adc_read / adc_ref_internal / adc_raw_to_millivolts
 *   (adc_read is a 2-arg call - no timeout in 4.x.)
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "zephyr/device.h"

#ifdef __cplusplus
extern "C" {
#endif

enum adc_gain {
	ADC_GAIN_1,
};

enum adc_reference {
	ADC_REF_INTERNAL,
	ADC_REF_EXTERNAL0,
};

#define ADC_ACQ_TIME_DEFAULT 0

#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
#define ADC_ADC_CFG_HAS_INPUTS 1
#endif

struct adc_channel_cfg {
	enum adc_gain gain;
	enum adc_reference reference;
	uint16_t acquisition_time;
	uint8_t channel_id : 5;
	uint8_t differential : 1;
#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
	uint8_t input_positive;
	uint8_t input_negative;
#endif
};

struct adc_sequence;

struct adc_sequence_options {
	uint32_t interval_us;
	int (*callback)(const struct device *dev,
			const struct adc_sequence *sequence,
			uint16_t sampling_index);
	void *user_data;
	uint16_t extra_samplings;
};

struct adc_sequence {
	const struct adc_sequence_options *options;
	uint32_t channels;
	void *buffer;
	size_t buffer_size;
	uint8_t resolution;
	uint8_t oversampling;
	bool calibrate;
};

extern int adc_channel_setup(const struct device *dev,
			     const struct adc_channel_cfg *channel_cfg);
extern int adc_read(const struct device *dev,
		    const struct adc_sequence *sequence);
extern uint16_t adc_ref_internal(const struct device *dev);
extern int adc_raw_to_millivolts(int32_t ref_mv, enum adc_gain gain,
				 uint8_t resolution, int32_t *valp);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_H_ */