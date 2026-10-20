/*
 * device.h - MOCK STUB for the Zephyr device API (host-side testing only).
 *
 * This is NOT part of the shipped Zephyr backend. It exists solely so the
 * backend .c files can be compiled and link-time mock-tested on the Windows
 * host (no Zephyr SDK available - see issues.md I3). The real build uses
 * Zephyr's <zephyr/device.h>.
 *
 * DT helpers:
 *   DT_NODELABEL(lbl)     -> DT_LABEL_<lbl>
 *   DT_NODE_EXISTS(nid)   -> 1 when HAL_MOD_<... match, else default 0;
 *                            add labels to "seen" list below as needed.
 *   DEVICE_DT_GET(nid)    -> &HAL_MOD_DEV_<nid>
 */
#ifndef ZEPHYR_INCLUDE_DEVICE_H_
#define ZEPHYR_INCLUDE_DEVICE_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base utility macros shared by the stubs. */
#ifndef BIT
#define BIT(n) (1U << (n))
#endif
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

struct device {
	const char *name;
	/* Mock-only data: parse result of device_is_ready(). */
	bool ready;
};

extern bool device_is_ready(const struct device *dev);

/* ------------------------- DT macro stubs ------------------------------- */

#define DT_NODELABEL(label) DT_LABEL_##label

/* Label presence for `#if DT_NODE_EXISTS(...)`. The default is 0; each label
 * listed below is "present" in the mock devicetree. */
#define HAL_MOD_DT_NODE_EXISTS(node_id) HAL_MOD_DT_NODE_EXISTS2(node_id)
#define HAL_MOD_DT_NODE_EXISTS2(node_id) HAL_MOD_DT_NODE_##node_id##_X
#define DT_NODE_EXISTS(node_id) HAL_MOD_DT_NODE_EXISTS(node_id)

/* Present labels (add new ones here when a test needs them). */
#define HAL_MOD_DT_NODE_DT_LABEL_i2c1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_i2c2_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_spi1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_uart1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_uart2_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_uart3_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_uart4_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_uart5_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_can0_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_can1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_can2_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_gpioa_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_gpiob_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_gpioc_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_gpiod_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_gpioe_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_adc1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_adc2_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_adc3_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_pwm1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_pwm2_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_pwm3_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_pwm4_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_flash0_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_w25q128_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_flash1_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_rng0_X 1
#define HAL_MOD_DT_NODE_DT_LABEL_trng_X 1

/* Default property accessor used by the SPI backend entry: returns the
 * provided default value (the mock DT has no real properties). */
#define DT_PROP_OR(node_id, prop, def) (def)

/* DEVICE_DT_GET needs TWO expansion layers: node_id is used as an argument
 * to a second macro (not a ## operand here), so DT_NODELABEL(...) is fully
 * expanded BEFORE token pasting inside HAL_MOD_DEV_CAT. A one-layer `##`
 * would paste the unexpanded `DT_NODELABEL` token into HAL_MOD_DEV_ and
 * break. This mirrors how Zephyr's real DEVICE_DT_GET is layered. */
#define HAL_MOD_DEV_CAT_(a, b) a ## b
#define HAL_MOD_DEV_NAME(node_id) HAL_MOD_DEV_CAT_(HAL_MOD_DEV_, node_id)
#define DEVICE_DT_GET(node_id) (&HAL_MOD_DEV_NAME(node_id))
#define DEVICE_DT_NAME_GET(node_id) HAL_MOD_DEV_NAME(node_id)

/* Per-label device objects. Backends only REFERENCE these (DEVICE_DT_GET),
 * so extern declarations are enough for -fsyntax-only; mock_devices.c defines
 * the few a runtime test actually touches. */
#define HAL_MOD_DT_DEV_DEFINE(node_id) extern struct device HAL_MOD_DEV_##node_id
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_i2c1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_i2c2);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_spi1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_uart1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_uart2);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_uart3);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_uart4);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_uart5);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_can0);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_can1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_can2);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_gpioa);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_gpiob);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_gpioc);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_gpiod);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_gpioe);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_adc1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_adc2);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_adc3);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_pwm1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_pwm2);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_pwm3);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_pwm4);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_flash0);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_w25q128);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_flash1);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_rng0);
HAL_MOD_DT_DEV_DEFINE(DT_LABEL_trng);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DEVICE_H_ */