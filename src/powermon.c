#include <errno.h>

#include "powermon.h"

int powermon_read(struct powermon_dev *pm, struct powermon_sample *s)
{
	struct powermon_sample tmp;
	int64_t t;
	int ret;

	if (!pm || !pm->ops || !pm->ops->read || !s)
		return -EINVAL;

	/* Stamp before the bus transfer: that is closer to the sample. */
	t = powermon_time_us();
	ret = pm->ops->read(pm, &tmp);
	if (ret)
		return ret;
	tmp.t_us = t;

	*s = tmp;
	return 0;
}
