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
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32_hal_stub.h"

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
	stub_tick += 1;
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

	/* The weak default clock: HAL_GetTick() ms, as microseconds. */
	stub_tick = 123456789;
	assert(powermon_read(&pm, &s) == 0);
	assert(fake_calls == 1);
	/* Stamped before the read, not after: the fake moved the clock. */
	assert(s.t_us == 123456789000LL);
	assert(stub_tick == 123456789 + 1);
	assert(s.bus_v == 12.5f && s.shunt_v == 0.0125f &&
	       s.current_a == 1.25f && s.power_w == 15.625f &&
	       s.die_temp_c == 25.5f && isnan(s.energy_j) && isnan(s.charge_c));

	/* A new read, a new stamp. */
	stub_tick = 123456999;
	assert(powermon_read(&pm, &s) == 0);
	assert(s.t_us == 123456999000LL);

	/* 32-bit milliseconds do not wrap in the 64-bit microseconds. */
	stub_tick = UINT32_MAX;
	assert(powermon_read(&pm, &s) == 0);
	assert(s.t_us == (int64_t)UINT32_MAX * 1000);
}

static void test_errno_passthrough(void)
{
	struct powermon_dev pm = fake_dev();
	struct powermon_sample s = { .bus_v = 1.0f, .current_a = 99.0f,
				     .t_us = 7 };

	/* Whatever the chip says comes back unchanged, and s is left alone. */
	stub_tick = 42;
	fake_ret = -ETIMEDOUT;
	assert(powermon_read(&pm, &s) == -ETIMEDOUT);
	fake_ret = -ENXIO;
	assert(powermon_read(&pm, &s) == -ENXIO);
	fake_ret = -ERANGE;
	assert(powermon_read(&pm, &s) == -ERANGE);
	fake_ret = -EIO;
	assert(powermon_read(&pm, &s) == -EIO);
	fake_ret = -EAGAIN;
	assert(powermon_read(&pm, &s) == -EAGAIN);
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
	assert(!strcmp(stub_log_last, "powermon: fake: id at 0x40 is 0x60"));

	/* No arguments after the format must still compile and work. */
	powermon_warn(&pm, "no arguments");
	assert(!strcmp(stub_log_last, "powermon: fake: no arguments"));
	powermon_info(&pm, "i");
	assert(!strcmp(stub_log_last, "powermon: fake: i"));
	/* test/Makefile has CONFIG_POWERMON_DEBUG=1, so this one prints. */
	powermon_dbg(&pm, "d");
	assert(!strcmp(stub_log_last, "powermon: fake: d"));

	stub_tick = 42;
	assert(powermon_read(&pm, &s) == 0);
	powermon_log_sample(&pm, &s);
	assert(!strcmp(stub_log_last,
		       "powermon: fake: t 42000 us   12.500 V   0.012500 V    "
		       "1.2500 A    15.625 W   25.50 C         nan J        "
		       "nan As"));

	/*
	 * The timestamp is formatted by hand, because newlib-nano's printf has
	 * no %lld. Both ends of the range, and zero.
	 */
	s.t_us = 0;
	powermon_log_sample(&pm, &s);
	assert(!strncmp(stub_log_last, "powermon: fake: t 0 us  ", 24));
	s.t_us = INT64_MAX;
	powermon_log_sample(&pm, &s);
	assert(strstr(stub_log_last, " t 9223372036854775807 us "));
	s.t_us = INT64_MIN;
	powermon_log_sample(&pm, &s);
	assert(strstr(stub_log_last, " t -9223372036854775808 us "));
	s.t_us = -1234567;
	powermon_log_sample(&pm, &s);
	assert(strstr(stub_log_last, " t -1234567 us "));
}

int main(void)
{
	test_read_stamps();
	test_errno_passthrough();
	test_log_prefix();
	printf("all core checks passed\n");
	return 0;
}
