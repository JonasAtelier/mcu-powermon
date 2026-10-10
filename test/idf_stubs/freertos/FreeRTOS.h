#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>

/*
 * The tick rate is a variable so a test can move it; it starts at ESP-IDF's
 * default 100 Hz (see idf_stubs.c), which is where pdMS_TO_TICKS() truncates
 * a short delay to zero ticks. pdMS_TO_TICKS truncates as IDF's does.
 */
extern unsigned int stub_tick_hz;

#define configTICK_RATE_HZ	stub_tick_hz
#define portTICK_PERIOD_MS	(1000 / configTICK_RATE_HZ)

typedef uint32_t TickType_t;

#define pdMS_TO_TICKS(msec) \
	((TickType_t)(((uint64_t)(msec) * configTICK_RATE_HZ) / 1000U))

#endif
