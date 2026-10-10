#include <stdint.h>

#include "powermon_log.h"

/*
 * Decimal text of v, written backwards from end. newlib-nano's printf, the
 * one stm-forge links, has no %lld, so the timestamp cannot go through PRId64.
 */
static const char *powermon_format_i64(char *end, int64_t v)
{
	uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;

	*--end = '\0';
	do {
		*--end = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	if (v < 0)
		*--end = '-';
	return end;
}

void powermon_log_sample(const struct powermon_dev *pm,
			 const struct powermon_sample *s)
{
	char t[21];	/* "-9223372036854775808" and its NUL */

	powermon_info(pm, "t %s us  %7.3f V  %9.6f V  %8.4f A  %8.3f W  "
		      "%6.2f C  %10.3f J  %9.3f As",
		      powermon_format_i64(t + sizeof(t), s->t_us),
		      (double)s->bus_v, (double)s->shunt_v,
		      (double)s->current_a, (double)s->power_w,
		      (double)s->die_temp_c, (double)s->energy_j,
		      (double)s->charge_c);
}
