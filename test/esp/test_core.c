/*
 * Host-side check of the core: powermon_read() and the log macros, against
 * a fake chip whose ops->read hands back whatever the test put in it.
 *
 * The per-chip rules -- -EBUSY on a second probe, -EINVAL for another
 * driver's device -- live in each driver, so each test_<chip>.c checks them
 * against that driver's real probe.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "powermon.h"
#include "powermon_log.h"

static struct powermon_sample fake_sample;
static int fake_ret;
static int fake_calls;

static int fake_read(struct powermon_dev *pm, struct powermon_sample *s)
{
	(void)pm;
	fake_calls++;
	/* The transfer takes time: a stamp taken after it would show this. */
	stub_time_us += 1000;
	if (fake_ret)
		return fake_ret;
	*s = fake_sample;
	return 0;
}

static const struct powermon_ops fake_ops = { .read = fake_read };

static struct powermon_dev fake_dev(void)
{
	struct powermon_dev pm = { .name = "fake", .ops = &fake_ops };

	fake_sample = (struct powermon_sample){
		.bus_v = 12.5f,
		.shunt_v = 0.0125f,
		.current_a = 1.25f,
		.power_w = 15.625f,
		.die_temp_c = 25.5f,
		.energy_j = NAN,
		.charge_c = NAN,
	};
	fake_ret = 0;
	fake_calls = 0;
	return pm;
}

static void test_read_stamps(void)
{
	struct powermon_dev pm = fake_dev();
	struct powermon_sample s = { .t_us = -1 };

	stub_time_us = 123456789;
	assert(powermon_read(&pm, &s) == 0);
	assert(fake_calls == 1);
	/* Stamped before the read, not after: the fake moved the clock. */
	assert(s.t_us == 123456789);
	assert(stub_time_us == 123456789 + 1000);
	assert(s.bus_v == 12.5f && s.shunt_v == 0.0125f &&
	       s.current_a == 1.25f && s.power_w == 15.625f &&
	       s.die_temp_c == 25.5f && isnan(s.energy_j) && isnan(s.charge_c));

	/* A new read, a new stamp. */
	stub_time_us = 123456999;
	assert(powermon_read(&pm, &s) == 0);
	assert(s.t_us == 123456999);
}

static void test_errno_passthrough(void)
{
	struct powermon_dev pm = fake_dev();
	struct powermon_sample s = { .bus_v = 1.0f, .current_a = 99.0f,
				     .t_us = 7 };

	/* Whatever the chip says comes back unchanged, and s is left alone. */
	stub_time_us = 42;
	fake_ret = -ETIMEDOUT;
	assert(powermon_read(&pm, &s) == -ETIMEDOUT);
	fake_ret = -ENXIO;
	assert(powermon_read(&pm, &s) == -ENXIO);
	fake_ret = -ERANGE;
	assert(powermon_read(&pm, &s) == -ERANGE);
	fake_ret = -EIO;
	assert(powermon_read(&pm, &s) == -EIO);
	assert(s.bus_v == 1.0f && s.current_a == 99.0f && s.t_us == 7);

	/* Nothing to call is the caller's mistake, not a chip error. */
	fake_calls = 0;
	assert(powermon_read(NULL, &s) == -EINVAL);
	assert(powermon_read(&pm, NULL) == -EINVAL);
	pm.ops = NULL;
	assert(powermon_read(&pm, &s) == -EINVAL);
	assert(fake_calls == 0);
	assert(s.t_us == 7);
}

static void test_log_prefix(void)
{
	struct powermon_dev pm = fake_dev();
	struct powermon_sample s;

	powermon_err(&pm, "id at 0x%02x is 0x%02x", 0x40, 0x60);
	assert(!strcmp(stub_log_last, "E powermon: fake: id at 0x40 is 0x60"));

	/* No arguments after the format must still compile and work. */
	powermon_warn(&pm, "no arguments");
	assert(!strcmp(stub_log_last, "W powermon: fake: no arguments"));
	powermon_info(&pm, "i");
	assert(!strcmp(stub_log_last, "I powermon: fake: i"));
	powermon_dbg(&pm, "d");
	assert(!strcmp(stub_log_last, "D powermon: fake: d"));

	stub_time_us = 42;
	assert(powermon_read(&pm, &s) == 0);
	powermon_log_sample(&pm, &s);
	assert(!strcmp(stub_log_last,
		       "I powermon: fake: t 42 us   12.500 V   0.012500 V    "
		       "1.2500 A    15.625 W   25.50 C         nan J        "
		       "nan As"));
}

int main(void)
{
	test_read_stamps();
	test_errno_passthrough();
	test_log_prefix();
	printf("all core checks passed\n");
	return 0;
}
