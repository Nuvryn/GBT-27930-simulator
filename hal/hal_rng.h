/*
 * hal_rng.h
 *
 * HAL (Hardware Abstraction Layer) random number generator interface
 * contract.
 *
 * Design principles:
 *  - Pure C99/C11 interface contract. No Zephyr/Linux types leak through.
 *  - The device is an opaque `void *` handle; the concrete object lives in
 *    the backend (Zephyr entropy `const struct device` / Linux /dev/urandom
 *    fd or getrandom()). The application never dereferences it.
 *  - `hal_rng_read` provides raw hardware-ish entropy bytes (not a
 *    deterministic PRNG). The backend fills the buffer fully before
 *    returning; it does not return short reads.
 *  - Error codes follow the OSAL convention (M4): functions return 0 on
 *    success and a negative errno value (`-EINVAL`, `-EIO`, ...) on failure.
 *
 * This header must compile standalone:
 *   gcc -c -I hal/ -x c -fsyntax-only hal/hal_rng.h
 */
#ifndef HAL_RNG_H
#define HAL_RNG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Opaque handle types                                                       */
/* ------------------------------------------------------------------------- */

/** RNG device handle (backend: Zephyr entropy dev / Linux /dev/urandom). */
typedef void *hal_rng_dev_t;

/* ------------------------------------------------------------------------- */
/* API                                                                       */
/* ------------------------------------------------------------------------- */

/**
 * @brief Fill a buffer with random bytes.
 *
 * @param dev RNG device handle.
 * @param buf Destination buffer (at least @p len bytes).
 * @param len Number of random bytes to produce.
 *
 * @return 0 on success (buffer fully filled), negative errno on failure.
 */
int hal_rng_read(hal_rng_dev_t dev, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* HAL_RNG_H */