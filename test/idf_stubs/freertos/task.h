#ifndef TASK_H
#define TASK_H

#include "FreeRTOS.h"

/*
 * Each test that links a driver defines both, to model its own clock; the
 * weak ones in idf_stubs.c stop any other test that reaches them.
 */
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

#endif
