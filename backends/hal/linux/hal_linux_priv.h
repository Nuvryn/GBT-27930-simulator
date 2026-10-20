/*
 * hal_linux_priv.h
 *
 * Private helpers shared by the Linux HAL backend files (ports/hal/linux/).
 * NOT part of the public contract; never include this from application code.
 *
 * Contents:
 *   - Handle convention (C1): every hal_xxx_bus_t / hal_xxx_dev_t returned
 *     to the app is a void* pointing at a hal_linux_dev_t { fd, lock }.
 *     NULL means "not found / not open".
 *   - M8 shared-bus serialization: the pthread_mutex_t inside the handle
 *     serializes I2C/SPI/CAN bus access INSIDE the backend. The app does
 *     not lock.
 *   - Logical-name -> /dev path conventions and small sysfs text helpers.
 */
#ifndef HAL_LINUX_PRIV_H
#define HAL_LINUX_PRIV_H

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <errno.h>

#include "hal/hal.h"

/* Bounded snprintf() target size used for sysfs/dev path composition. */
#define HAL_LINUX_PATH_MAX 160

/**
 * @brief Common opaque handle payload for fd-backed devices.
 *
 * The application-visible handle is a `void *` to one of these. fd == -1
 * means the device is not open. The mutex implements M8 bus
 * serialization and is released in the matching hal_linux_dev_unlock().
 */
typedef struct hal_linux_dev {
	int fd;	               /* OS-level file descriptor, -1 = closed */
	pthread_mutex_t lock;  /* M8: serializes access to this bus/dev   */
} hal_linux_dev_t;

/** Allocate + init an fd-backed handle (mutex init, fd = -1). NULL on
 *  out-of-memory or mutex-init failure. */
hal_linux_dev_t *hal_linux_dev_alloc(void);

/** Destroy the mutex and free the handle. */
void hal_linux_dev_free(hal_linux_dev_t *d);

/** Lock the handle's M8 serialization mutex (busy-waits; M8 contract). */
void hal_linux_dev_lock(hal_linux_dev_t *d);
void hal_linux_dev_unlock(hal_linux_dev_t *d);

/**
 * @brief Parse the trailing decimal index of a logical device name.
 *
 *   "i2c1"   -> *index = 1, returns 0
 *   "uart3"  -> *index = 3, returns 0
 *   "gpio12" -> *index = 12, returns 0
 *   "gpioa"  -> -EINVAL (no trailing digits)
 *
 * @param name  Logical device name (must end in digits).
 * @param index Out parameter receiving the trailing number.
 * @return 0 on success, negative errno otherwise.
 */
int hal_linux_parse_trailing_index(const char *name, int *index);

/** Write a NUL-terminated string (no newline added) to a sysfs/dev file.
 *  @return 0 on success, -errno on failure. */
int hal_linux_write_text_file(const char *path, const char *text);

/** Read a whole sysfs file into buf (NUL-terminated, trailing newline kept).
 *  @return 0 on success, -errno on failure. */
int hal_linux_read_text_file(const char *path, char *buf, size_t buflen);

/** Write a decimal integer to a sysfs file. @return 0 or -errno. */
int hal_linux_write_int_file(const char *path, long value);

/** Read a decimal integer from a sysfs file. @return 0 or -errno. */
int hal_linux_read_int_file(const char *path, long *out);

/** Check that a directory exists and is searchable.
 *  @return 0 if present, -errno otherwise. */
int hal_linux_dir_exists(const char *path);

#endif /* HAL_LINUX_PRIV_H */