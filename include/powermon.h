/*
 * powermon -- a small Linux-IIO-style power monitor subsystem for ESP-IDF
 * and the STM32 HAL.
 *
 * One struct powermon_dev per sensor. A chip driver's probe fills in its
 * name, its ops and its private state; powermon_read() does the rest:
 * timestamp, then the chip's own read. Code above this header sees one shape
 * of sample whichever chip produced it.
 *
 * Every int-returning function returns 0 or a negative errno.
 */
#ifndef POWERMON_H
#define POWERMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SI units throughout. A quantity the chip does not measure is NAN, so a
 * caller can tell "not measured" from zero with isnan(). Current and power
 * follow the chip's sign: positive when current flows from IN+ to IN-
 * through the shunt.
 */
struct powermon_sample {
	float bus_v;		/* bus voltage, V */
	float shunt_v;		/* voltage across the shunt, V */
	float current_a;	/* A */
	float power_w;		/* W */
	float die_temp_c;	/* degrees C; NAN where there is no sensor */
	float energy_j;		/* J since the last reset; NAN where none */
	float charge_c;		/* C since the last reset; NAN where none */
	int64_t t_us;		/* powermon_time_us() at read */
};

struct powermon_dev;

struct powermon_ops {
	/* Every field but t_us, in the units above; must not touch s->t_us */
	int (*read)(struct powermon_dev *pm, struct powermon_sample *s);
};

/*
 * Must start zeroed -- static storage or `= { 0 }` -- because probe treats a
 * non-NULL ops or priv as "already bound" and returns -EBUSY rather than
 * leak the first binding. remove, or a failed probe, zeroes them again, so
 * the same powermon_dev can be probed afresh.
 */
struct powermon_dev {
	const char *name;		/* "ina226"; log prefix */
	const struct powermon_ops *ops;
	void *priv;			/* chip state, owned by probe/remove */
};

/* Timestamp, then ops->read. *s is written only on success. */
int powermon_read(struct powermon_dev *pm, struct powermon_sample *s);

/*
 * The clock powermon_read() stamps samples with, in microseconds. On ESP-IDF
 * it is esp_timer_get_time(). On the STM32 HAL the default is weak and only
 * HAL_GetTick() * 1000: millisecond steps that wrap after 49.7 days. Define
 * your own powermon_time_us() for real microseconds -- the DWT->CYCCNT cycle
 * counter divided by the core clock in MHz, or a free running 32-bit timer
 * at 1 MHz extended to 64 bits.
 */
int64_t powermon_time_us(void);

#ifdef __cplusplus
}
#endif

#endif /* POWERMON_H */
