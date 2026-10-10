/*
 * Kernel dev_err()-style logging for power monitor drivers and their users.
 *
 * ESP-IDF: everything goes out under the one ESP-IDF tag "powermon",
 * prefixed with the device's name, so one esp_log_level_set("powermon", ...)
 * controls every chip and a line still says which chip it came from. There
 * is deliberately no level machinery of our own; powermon_dbg() is also
 * subject to CONFIG_LOG_MAXIMUM_LEVEL, and CONFIG_POWERMON_DEBUG is not used.
 *
 * STM32 HAL: every line goes out through printf() as "powermon: <name>:
 * ...", so a line still says which chip it came from. Where printf() ends up
 * is the app's business: with newlib, retarget _write() to a UART. There is
 * deliberately no level machinery; powermon_dbg() alone is compiled out
 * unless CONFIG_POWERMON_DEBUG is 1 (stm-forge: POWERMON_DEBUG = y).
 */
#ifndef POWERMON_LOG_H
#define POWERMON_LOG_H

#ifdef ESP_PLATFORM
#include "esp_log.h"
#else
#include <stdio.h>
#endif

#include "powermon.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ESP_PLATFORM
#define POWERMON_LOG_TAG	"powermon"

#define powermon_err(pm, fmt, ...) \
	ESP_LOGE(POWERMON_LOG_TAG, "%s: " fmt, (pm)->name, ##__VA_ARGS__)
#define powermon_warn(pm, fmt, ...) \
	ESP_LOGW(POWERMON_LOG_TAG, "%s: " fmt, (pm)->name, ##__VA_ARGS__)
#define powermon_info(pm, fmt, ...) \
	ESP_LOGI(POWERMON_LOG_TAG, "%s: " fmt, (pm)->name, ##__VA_ARGS__)
#define powermon_dbg(pm, fmt, ...) \
	ESP_LOGD(POWERMON_LOG_TAG, "%s: " fmt, (pm)->name, ##__VA_ARGS__)

#else /* STM32 HAL */
#define powermon_err(pm, fmt, ...) \
	printf("powermon: %s: " fmt "\n", (pm)->name, ##__VA_ARGS__)
#define powermon_warn(pm, fmt, ...) \
	printf("powermon: %s: " fmt "\n", (pm)->name, ##__VA_ARGS__)
#define powermon_info(pm, fmt, ...) \
	printf("powermon: %s: " fmt "\n", (pm)->name, ##__VA_ARGS__)

#if CONFIG_POWERMON_DEBUG
#define powermon_dbg(pm, fmt, ...) \
	printf("powermon: %s: " fmt "\n", (pm)->name, ##__VA_ARGS__)
#else
/* Still type-checked, so a debug line cannot rot while it is off. */
#define powermon_dbg(pm, fmt, ...) do { \
	if (0) \
		printf("powermon: %s: " fmt "\n", (pm)->name, ##__VA_ARGS__); \
} while (0)
#endif
#endif /* ESP_PLATFORM */

/*
 * One info line: timestamp, bus and shunt voltage, current, power, then die
 * temperature, energy and charge ("nan" where the chip has none).
 */
void powermon_log_sample(const struct powermon_dev *pm,
			 const struct powermon_sample *s);

#ifdef __cplusplus
}
#endif

#endif /* POWERMON_LOG_H */
