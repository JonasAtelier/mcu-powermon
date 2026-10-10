#ifndef ESP_TIMER_H
#define ESP_TIMER_H

#include <stdint.h>

/* Host stub: returns stub_time_us, which tests set. */

extern int64_t stub_time_us;

int64_t esp_timer_get_time(void);

#endif
