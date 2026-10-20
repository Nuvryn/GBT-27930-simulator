/*
 * hal_adc.h
 *
 * HAL (Hardware Abstraction Layer) ADC interface contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The device is an opaque `void *` handle; the concrete object lives in
 *    the backend (Zephyr `const struct device` / Linux IIO fd or path).
 *    The application never dereferences it.
 *  - Channel numbers are logical indexes resolved by the backend to the
 *    concrete ADC input (Zephyr channel id / Linux iio:deviceN in_voltageN).
 *  - Value unit: `value` is expressed in millivolts (mV) scaled by the
 *    backend. Raw counts are converted internally using the configured
 *    reference and resolution; backends that cannot scale return the raw
 *    count and document that deviation (contract default = millivolts).
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-ENODEV`, ...) on
 *    failure.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_adc.h
 */
#ifndef HAL_ADC_H
#define HAL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** ADC device handle (backend: Zephyr `const struct device` / Linux IIO). */
typedef void *hal_adc_dev_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Perform a single conversion on one ADC channel.
 *
 * @param dev     ADC device handle.
 * @param channel Logical channel index to sample.
 * @param value   Out parameter receiving the sampled voltage in millivolts
 *                (default contract; a backend that cannot scale documents
 *                raw counts instead).
 *
 * @return 0 on success, negative errno on failure.
 */
int hal_adc_read(hal_adc_dev_t dev, uint8_t channel, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */