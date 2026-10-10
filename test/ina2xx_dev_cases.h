/*
 * The INA2xx through the powermon API -- ina2xx_probe(), powermon_read(),
 * ina2xx_reset_energy(), ina2xx_remove() -- shared by esp/test_ina2xx.c and
 * stm/test_ina2xx_dev.c. Each includes this after its own fake chip, which
 * provides:
 *
 *   BUS                       the platform's bus handle for the fake
 *   chip_power_on(model)      a fresh chip, every counter zeroed
 *   regs[], wlog[], writes, reads, probed_addr, dev_addr, absent,
 *   allocs, frees             the fake's register file and its counters
 *   bus_fail(on)              every later transfer times out, or not
 *   set_clock_us(us)          what powermon_time_us() returns next
 *   waited_ms()               how long the driver has waited, at least
 */

static struct powermon_dev probe_ok(const ina2xx_config_t *cfg)
{
	struct powermon_dev pm = { 0 };

	chip_power_on(cfg->model);
	assert(ina2xx_probe(&pm, BUS, cfg) == 0);
	assert(pm.ops && pm.priv);
	return pm;
}

/* Each model: its name, its writes, one allocation, freed by remove. */
static void test_dev_probe_remove(void)
{
	static const struct {
		ina2xx_model_t model;
		const char *name;
		int writes;
	} models[] = {
		{ INA219, "ina219", 2 },
		{ INA226, "ina226", 2 },
		{ INA228, "ina228", 3 },
		{ INA237, "ina237", 3 },
	};

	for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
		ina2xx_config_t cfg = { .model = models[i].model, .addr = 0x41,
					.shunt_ohms = 0.1f,
					.max_current_a = 1.0f };
		struct powermon_dev pm = probe_ok(&cfg);

		assert(!strcmp(pm.name, models[i].name));
		assert(probed_addr == 0x41 && dev_addr == 0x41);
		assert(writes == models[i].writes);
		assert(waited_ms() >= 10 && "first conversion waited for");
		assert(allocs == 1 && frees == 0);
		ina2xx_remove(&pm);
		assert(frees == 1);
		assert(!pm.name && !pm.ops && !pm.priv &&
		       "zeroed for a new probe");
	}
}

/* powermon_read(): stamped, scaled, NAN where the chip has nothing. */
static void test_dev_read(void)
{
	ina2xx_config_t cfg = { .model = INA237, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f };
	struct powermon_dev pm = probe_ok(&cfg);
	struct powermon_sample s;
	float lsb = ina237_current_lsb(10.0f);

	/* SBOSA20A Table 8-4's returned values. */
	regs[0x05] = 15360;		/* VBUS: 48 V */
	regs[0x04] = 19440;		/* VSHUNT: 97.2 mV */
	regs[0x07] = 19660;		/* CURRENT */
	regs[0x08] = 4718604;		/* POWER: 24 bits */
	regs[0x06] = 200 << 4;		/* DIETEMP: 25 C */
	set_clock_us(5000000);
	assert(powermon_read(&pm, &s) == 0);
	assert(reads == 5);
	assert(s.t_us == 5000000);
	assert(s.bus_v == 48.0f);
	assert(s.shunt_v == ina237_shunt_v(19440));
	assert(s.current_a == 19660 * lsb);
	assert(s.power_w == ina237_power_w(4718604, lsb));
	assert(s.die_temp_c == 25.0f);
	assert(isnan(s.energy_j) && isnan(s.charge_c));
	ina2xx_remove(&pm);

	cfg.model = INA228;
	pm = probe_ok(&cfg);
	lsb = ina228_current_lsb(10.0f);
	regs[0x09] = 0x0102030405ULL;	/* ENERGY */
	regs[0x0A] = 0xFFFFFFFFFFULL;	/* CHARGE: -1 LSB */
	assert(powermon_read(&pm, &s) == 0);
	assert(s.energy_j == ina228_energy_j(0x0102030405ULL, lsb));
	assert(s.charge_c == -lsb);
	ina2xx_remove(&pm);

	cfg.model = INA226;
	cfg.shunt_ohms = 0.1f;
	cfg.max_current_a = 3.2f;
	pm = probe_ok(&cfg);
	regs[0x02] = 0x2570;		/* 11.98 V */
	assert(powermon_read(&pm, &s) == 0);
	assert(reads == 4);
	assert(fabsf(s.bus_v - 11.98f) < 1e-4f);
	assert(isnan(s.die_temp_c) && isnan(s.energy_j) && isnan(s.charge_c));

	/* A failed read leaves the sample alone. */
	{
		struct powermon_sample before;

		memset(&s, 0x5A, sizeof(s));
		before = s;
		bus_fail(true);
		assert(powermon_read(&pm, &s) == -ETIMEDOUT);
		assert(!memcmp(&s, &before, sizeof(s)));
		bus_fail(false);
	}
	ina2xx_remove(&pm);
}

/* Bound already, someone else's, absent, refused: nothing touched. */
static void test_dev_refusals(void)
{
	static const struct powermon_ops other_ops = { 0 };
	ina2xx_config_t cfg = { .model = INA228, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f };
	struct powermon_dev pm = probe_ok(&cfg);
	struct powermon_dev other = { .name = "other", .ops = &other_ops,
				      .priv = &cfg };
	void *priv = pm.priv;

	/* A second probe on a bound device: -EBUSY, no traffic. */
	writes = reads = allocs = 0;
	probed_addr = 0;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -EBUSY);
	assert(pm.priv == priv && probed_addr == 0 && writes == 0 &&
	       allocs == 0);

	/* Another driver's device is not ours. */
	assert(ina2xx_reset_energy(&other) == -EINVAL);
	ina2xx_remove(&other);
	assert(other.ops == &other_ops && other.priv == &cfg);
	ina2xx_remove(NULL);
	assert(ina2xx_reset_energy(NULL) == -EINVAL);

	/* RSTACC on the INA228; nothing to reset on the others. */
	writes = 0;
	regs[0x00] = 0;
	assert(ina2xx_reset_energy(&pm) == 0);
	assert(writes == 1 && regs[0x00] == 0x4000);
	ina2xx_remove(&pm);
	cfg.model = INA237;
	pm = probe_ok(&cfg);
	writes = 0;
	assert(ina2xx_reset_energy(&pm) == -ENOTSUP && writes == 0);
	ina2xx_remove(&pm);

	/* Nothing ACKs: -ENXIO, before anything is allocated. */
	chip_power_on(INA226);
	absent = true;
	memset(&pm, 0, sizeof(pm));
	cfg.model = INA226;
	cfg.shunt_ohms = 0.1f;
	cfg.max_current_a = 3.2f;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -ENXIO);
	assert(allocs == 0 && writes == 0 && !pm.ops && !pm.priv);

	/* Refused before the bus is touched. */
	chip_power_on(INA226);
	cfg.model = (ina2xx_model_t)7;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -EINVAL);
	cfg.model = INA226;
	cfg.addr = 0x80;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -EINVAL);
	cfg.addr = 0;
	cfg.shunt_ohms = 0.0f;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -EINVAL);
	cfg.shunt_ohms = 0.1f;
	assert(ina2xx_probe(NULL, BUS, &cfg) == -EINVAL);
	assert(ina2xx_probe(&pm, NULL, &cfg) == -EINVAL);
	assert(ina2xx_probe(&pm, BUS, NULL) == -EINVAL);
	assert(probed_addr == 0 && writes == 0 && allocs == 0);

	/* A calibration that cannot fit: refused, nothing written or kept. */
	cfg.max_current_a = 0.001f;
	assert(ina2xx_probe(&pm, BUS, &cfg) == -EINVAL);
	assert(writes == 0 && allocs == frees);

	/* A transfer failing after the presence check: mapped, nothing kept. */
	chip_power_on(INA226);
	cfg.max_current_a = 3.2f;
	bus_fail(true);
	assert(ina2xx_probe(&pm, BUS, &cfg) == -ETIMEDOUT);
	assert(allocs == 1 && frees == 1);
	bus_fail(false);
	assert(!pm.name && !pm.ops && !pm.priv);
}

static void run_dev_cases(void)
{
	test_dev_probe_remove();
	test_dev_read();
	test_dev_refusals();
}
