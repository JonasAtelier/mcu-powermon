/*
 * Host-side check of the driver against a fake INA2xx behind a fake HAL. No
 * hardware, no ARM toolchain -- see test/Makefile.
 *
 * The fake is a register file with each part's own widths: every INA219 and
 * INA226 register is 16 bits, the INA228 has 24-bit results and 40-bit
 * accumulators, and the INA237 16-bit results with a 24-bit POWER. It serves them MSB first, as the parts do, and insists on the
 * HAL's 8-bit address form, the 7-bit address shifted left one. The
 * conversions themselves are test_math.c's job; this checks what reaches the
 * bus and what comes back.
 */
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32_hal_stub.h"

#include "ina2xx.h"
#include "ina2xx_math.h"

static I2C_HandleTypeDef bus;
static ina2xx_model_t chip;		/* which part the fake is */
static uint64_t regs[16];
static struct { uint8_t reg; uint16_t val; } wlog[8];
static int writes, reads;
static uint16_t probed_addr, dev_addr;	/* 7-bit */
static HAL_StatusTypeDef bus_status;	/* every transfer returns this if set */
static HAL_StatusTypeDef probe_status;	/* IsDeviceReady on a present chip */
static int fail_reg = -1;		/* a read of this register times out */
static bool absent;			/* nothing ACKs at the address */
static uint32_t delayed_ms;
static int allocs, frees;

static void chip_power_on(ina2xx_model_t model)
{
	chip = model;
	memset(regs, 0, sizeof(regs));
	memset(wlog, 0, sizeof(wlog));
	writes = reads = 0;
	probed_addr = dev_addr = 0;
	bus_status = probe_status = HAL_OK;
	fail_reg = -1;
	absent = false;
	delayed_ms = 0;
	allocs = frees = 0;
}

/*
 * Register width in bytes: INA228 SLYS021A Table 7-5, INA237 SBOSA20A Table
 * 7-3; INA219/226 are all 16-bit.
 */
static size_t width(uint16_t reg)
{
	if (chip == INA237)
		return reg == 0x08 ? 3 : 2;
	if (chip != INA228)
		return 2;
	switch (reg) {
	case 0x04: case 0x05: case 0x07: case 0x08:
		return 3;
	case 0x09: case 0x0A:
		return 5;
	default:
		return 2;
	}
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c,
					uint16_t address, uint32_t trials,
					uint32_t timeout)
{
	assert(hi2c == &bus);
	assert(!(address & 1) && "the HAL takes the 7-bit address << 1");
	assert(trials >= 1 && timeout > 0);
	probed_addr = address >> 1;
	if (absent)
		return HAL_ERROR;
	return probe_status;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c,
				    uint16_t address, uint16_t reg,
				    uint16_t reg_size, uint8_t *data,
				    uint16_t len, uint32_t timeout)
{
	assert(hi2c == &bus);
	assert(!(address & 1) && "the HAL takes the 7-bit address << 1");
	assert(reg_size == I2C_MEMADD_SIZE_8BIT && reg < 16);
	assert(len == 2 && width(reg) == 2 && "writes are 16-bit registers");
	assert(timeout > 0);
	if (absent)
		return HAL_ERROR;
	if (bus_status != HAL_OK)
		return bus_status;
	dev_addr = address >> 1;
	regs[reg] = (uint16_t)(data[0] << 8 | data[1]);	/* MSB first */
	if (writes < 8) {
		wlog[writes].reg = (uint8_t)reg;
		wlog[writes].val = (uint16_t)regs[reg];
	}
	writes++;
	return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t address,
				   uint16_t reg, uint16_t reg_size,
				   uint8_t *data, uint16_t len,
				   uint32_t timeout)
{
	assert(hi2c == &bus);
	assert(!(address & 1) && "the HAL takes the 7-bit address << 1");
	assert(reg_size == I2C_MEMADD_SIZE_8BIT && reg < 16);
	assert(len == width(reg) && "read the register's own width");
	assert(timeout > 0);
	if (absent)
		return HAL_ERROR;
	if (bus_status != HAL_OK)
		return bus_status;
	if (reg == fail_reg)
		return HAL_TIMEOUT;
	dev_addr = address >> 1;
	reads++;
	for (uint16_t i = 0; i < len; i++)	/* MSB first */
		data[i] = (uint8_t)(regs[reg] >> (8 * (len - 1 - i)));
	return HAL_OK;
}

void HAL_Delay(uint32_t ms)
{
	delayed_ms += ms;
}

/* Counts the driver's calloc() and free() calls; see test/Makefile. */
void *__real_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size);
void *__wrap_calloc(size_t n, size_t size)
{
	allocs++;
	return __real_calloc(n, size);
}
void __real_free(void *p);
void __wrap_free(void *p);
void __wrap_free(void *p)
{
	frees++;
	__real_free(p);
}

static ina2xx_handle_t open_ok(const ina2xx_config_t *cfg)
{
	ina2xx_handle_t h = NULL;

	chip_power_on(cfg->model);
	assert(ina2xx_init(&bus, cfg, &h) == 0);
	assert(h);
	return h;
}

/* ------------------------------------------------------------- tests ----- */

static void test_init_ina219(void)
{
	ina2xx_config_t cfg = { .model = INA219, .shunt_ohms = 0.1f,
				.max_current_a = 3.2f, .avg_samples = 16 };
	ina2xx_handle_t h = open_ok(&cfg);

	assert(probed_addr == 0x40 && dev_addr == 0x40 && "addr 0 is 0x40");
	assert(writes == 2);
	/* BRNG=1, PG=11, BADC=SADC=1100 (16 samples), MODE=111 */
	assert(wlog[0].reg == 0x00 && wlog[0].val == 0x3E67);
	assert(wlog[1].reg == 0x05 &&
	       wlog[1].val == ina219_cal(ina219_current_lsb(3.2f), 0.1f, 1.0));
	assert(delayed_ms >= 10 && "first conversion waited for");
	assert(allocs == 1);
	assert(ina2xx_del(h) == 0 && frees == 1);
}

static void test_init_ina226(void)
{
	ina2xx_config_t cfg = { .model = INA226, .addr = 0x45, .shunt_ohms = 0.1f,
				.max_current_a = 3.2f, .avg_samples = 16,
				.cal_trim = 0.5f };
	ina2xx_handle_t h = open_ok(&cfg);

	assert(probed_addr == 0x45 && dev_addr == 0x45);
	assert(writes == 2);
	/* D14=1 (its POR value), AVG=010 (16), VBUSCT=VSHCT=100 (1.1 ms),
	 * MODE=111 */
	assert(wlog[0].reg == 0x00 && wlog[0].val == 0x4527);
	assert(wlog[1].reg == 0x05 &&
	       wlog[1].val == ina226_cal(ina226_current_lsb(3.2f), 0.1f, 0.5));
	ina2xx_del(h);
}

static void test_init_ina228(void)
{
	ina2xx_config_t cfg = { .model = INA228, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f, .avg_samples = 1 };
	ina2xx_handle_t h = open_ok(&cfg);

	assert(writes == 3);
	assert(wlog[0].reg == 0x00 && wlog[0].val == 0x4000 && "RSTACC");
	/* MODE=1111, VBUSCT=VSHCT=VTCT=101 (1052 us), AVG=000 */
	assert(wlog[1].reg == 0x01 && wlog[1].val == 0xFB68);
	assert(wlog[2].reg == 0x02 &&
	       wlog[2].val == ina228_cal(ina228_current_lsb(10.0f), 16.2e-3f, 1.0));
	ina2xx_del(h);
}

static void test_init_ina237(void)
{
	ina2xx_config_t cfg = { .model = INA237, .addr = 0x4F, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f, .avg_samples = 64 };
	ina2xx_handle_t h = open_ok(&cfg);

	assert(probed_addr == 0x4F && dev_addr == 0x4F);
	assert(writes == 3);
	/* ADCRANGE=0; no RSTACC, which is a reserved bit here (Table 7-5). */
	assert(wlog[0].reg == 0x00 && wlog[0].val == 0x0000);
	/* MODE=1111, VBUSCT=VSHCT=VTCT=101 (1052 us), AVG=011 (64) */
	assert(wlog[1].reg == 0x01 && wlog[1].val == 0xFB6B);
	/* sec. 8.2.2.3's own example: 4050. */
	assert(wlog[2].reg == 0x02 && wlog[2].val == 4050);
	ina2xx_del(h);
}

static void test_read_ina237(void)
{
	ina2xx_config_t cfg = { .model = INA237, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f };
	ina2xx_handle_t h = open_ok(&cfg);
	ina2xx_reading_t r;
	float lsb = ina237_current_lsb(10.0f);

	/* Table 8-4's returned values, MSB first on the wire. */
	regs[0x05] = 15360;		/* VBUS: 48 V */
	regs[0x04] = 19440;		/* VSHUNT: 97.2 mV */
	regs[0x07] = 0xB3B4;		/* CURRENT: -19532, signed */
	regs[0x08] = 4718604;		/* POWER: 24 bits */
	regs[0x06] = 200 << 4;		/* DIETEMP: 25 C */
	assert(ina2xx_read(h, &r) == 0);
	assert(reads == 5);
	assert(r.bus_v == 48.0f);
	assert(r.shunt_v == ina237_shunt_v(19440));
	assert(r.current_a == -19532 * lsb);
	assert(r.power_w == ina237_power_w(4718604, lsb));
	assert(r.die_temp_c == 25.0f);
	assert(isnan(r.energy_j) && isnan(r.charge_c));

	writes = 0;
	assert(ina2xx_reset_accumulators(h) == -ENOTSUP);
	assert(writes == 0);
	ina2xx_del(h);
}

static void test_read_16bit_byte_order(void)
{
	ina2xx_config_t cfg = { .model = INA226, .shunt_ohms = 0.1f,
				.max_current_a = 3.2f };
	ina2xx_handle_t h = open_ok(&cfg);
	ina2xx_reading_t r;
	float lsb = ina226_current_lsb(3.2f);

	/*
	 * 0x2570 is 11.98 V (INA226 sec. 7.5.1). Byte-swapped it would be
	 * 0x7025, 35.9 V; 0xFF9C swapped is a positive 0x9CFF.
	 */
	regs[0x02] = 0x2570;
	regs[0x01] = 0xFF9C;
	regs[0x04] = 0xFFFE;
	regs[0x03] = 0x0102;
	assert(ina2xx_read(h, &r) == 0);
	assert(reads == 4);
	assert(fabsf(r.bus_v - 11.98f) < 1e-4f);
	assert(r.shunt_v == ina226_shunt_v(0xFF9C) && r.shunt_v < 0.0f);
	assert(r.current_a == -2.0f * lsb && "current is signed");
	assert(r.power_w == ina226_power_w(0x0102, lsb));
	assert(isnan(r.die_temp_c) && isnan(r.energy_j) && isnan(r.charge_c));
	ina2xx_del(h);

	/* INA219: bus in bits 15:3, flags below. */
	cfg.model = INA219;
	h = open_ok(&cfg);
	regs[0x02] = (0x1F40 << 3) | 0x3;
	assert(ina2xx_read(h, &r) == 0);
	assert(fabsf(r.bus_v - 32.0f) < 1e-4f);
	assert(isnan(r.die_temp_c));
	ina2xx_del(h);
}

static void test_read_ina228_widths(void)
{
	ina2xx_config_t cfg = { .model = INA228, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f };
	ina2xx_handle_t h = open_ok(&cfg);
	ina2xx_reading_t r;
	float lsb = ina228_current_lsb(10.0f);

	regs[0x05] = 0x010000;		/* VBUS: 0x1000 << 4 */
	regs[0x04] = 0xFFFFF0;		/* VSHUNT: -1 LSB */
	regs[0x07] = 0x7FFFF0;		/* CURRENT: largest positive */
	regs[0x08] = 0x800001;		/* POWER: unsigned, top bit is magnitude */
	regs[0x06] = 0x0C80;		/* DIETEMP: 25 C */
	regs[0x09] = 0x0102030405ULL;	/* ENERGY */
	regs[0x0A] = 0xFFFFFFFFFFULL;	/* CHARGE: -1 LSB */
	assert(ina2xx_read(h, &r) == 0);
	assert(reads == 7);
	assert(r.bus_v == ina228_bus_v(0x010000));
	assert(fabsf(r.bus_v - 0.8f) < 1e-6f);
	assert(r.shunt_v == ina228_shunt_v(0xFFFFF0) && r.shunt_v < 0.0f);
	assert(r.current_a == ina228_current_a(0x7FFFF0, lsb) && r.current_a > 0.0f);
	assert(r.power_w == ina228_power_w(0x800001, lsb));
	assert(r.die_temp_c == 25.0f);
	assert(r.energy_j == ina228_energy_j(0x0102030405ULL, lsb));
	assert(r.charge_c == -lsb);
	ina2xx_del(h);
}

static void test_read_error_leaves_output(void)
{
	ina2xx_config_t cfg = { .model = INA228, .shunt_ohms = 16.2e-3f,
				.max_current_a = 10.0f };
	ina2xx_handle_t h = open_ok(&cfg);
	ina2xx_reading_t r, before;

	memset(&r, 0x5A, sizeof(r));
	before = r;

	/* The last register of seven: everything before it has been read. */
	fail_reg = 0x0A;
	assert(ina2xx_read(h, &r) == -ETIMEDOUT);
	assert(!memcmp(&r, &before, sizeof(r)) && "reading untouched");

	fail_reg = -1;
	bus_status = HAL_ERROR;
	assert(ina2xx_read(h, &r) == -EIO);
	bus_status = HAL_BUSY;
	assert(ina2xx_read(h, &r) == -EAGAIN);
	assert(!memcmp(&r, &before, sizeof(r)));
	bus_status = HAL_OK;
	assert(ina2xx_read(h, &r) == 0);
	ina2xx_del(h);
}

static void test_presence(void)
{
	ina2xx_config_t cfg = { .model = INA226, .shunt_ohms = 0.1f,
				.max_current_a = 3.2f };
	ina2xx_handle_t h = (ina2xx_handle_t)&bus;	/* a sentinel */

	/* Nothing ACKs: -ENXIO, before anything is allocated or written. */
	chip_power_on(INA226);
	absent = true;
	assert(ina2xx_init(&bus, &cfg, &h) == -ENXIO);
	assert(probed_addr == 0x40);
	assert(allocs == 0 && writes == 0 && reads == 0);
	assert(h == (ina2xx_handle_t)&bus && "handle untouched");

	/* A busy or hung bus during the presence check is not absence. */
	chip_power_on(INA226);
	probe_status = HAL_BUSY;
	assert(ina2xx_init(&bus, &cfg, &h) == -EAGAIN);
	assert(allocs == 0 && writes == 0);
	chip_power_on(INA226);
	probe_status = HAL_TIMEOUT;
	assert(ina2xx_init(&bus, &cfg, &h) == -ETIMEDOUT);
	assert(allocs == 0);

	/* A transfer failing after the probe: mapped, and nothing leaked. */
	chip_power_on(INA226);
	bus_status = HAL_ERROR;
	assert(ina2xx_init(&bus, &cfg, &h) == -EIO);
	assert(allocs == 1 && frees == 1);
	chip_power_on(INA226);
	bus_status = HAL_BUSY;
	assert(ina2xx_init(&bus, &cfg, &h) == -EAGAIN);
	assert(allocs == 1 && frees == 1);
	assert(h == (ina2xx_handle_t)&bus && "handle untouched");
}

static void test_refused_config(void)
{
	ina2xx_config_t cfg = { .model = INA219, .shunt_ohms = 0.1f,
				.max_current_a = 0.1f };
	ina2xx_handle_t h = (ina2xx_handle_t)&bus;

	/* 100 mA through 0.1 ohm needs CAL 134218: refused, nothing written. */
	chip_power_on(INA219);
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	assert(writes == 0 && allocs == frees);

	/* Not a model. */
	chip_power_on(INA219);
	cfg.max_current_a = 3.2f;
	cfg.model = (ina2xx_model_t)7;
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	assert(writes == 0 && allocs == frees);

	/* Refused before the bus is touched at all. */
	chip_power_on(INA219);
	cfg.model = INA219;
	cfg.addr = 0x80;
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	cfg.addr = 0;
	cfg.shunt_ohms = 0.0f;
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	cfg.shunt_ohms = NAN;
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	cfg.shunt_ohms = 0.1f;
	cfg.max_current_a = -1.0f;
	assert(ina2xx_init(&bus, &cfg, &h) == -EINVAL);
	cfg.max_current_a = 3.2f;
	assert(ina2xx_init(NULL, &cfg, &h) == -EINVAL);
	assert(ina2xx_init(&bus, NULL, &h) == -EINVAL);
	assert(ina2xx_init(&bus, &cfg, NULL) == -EINVAL);
	assert(probed_addr == 0 && writes == 0 && allocs == 0);
	assert(h == (ina2xx_handle_t)&bus);
}

static void test_reset_accumulators_and_del(void)
{
	ina2xx_config_t cfg = { .model = INA226, .shunt_ohms = 0.1f,
				.max_current_a = 3.2f };
	ina2xx_handle_t h = open_ok(&cfg);
	ina2xx_reading_t r;

	writes = 0;
	assert(ina2xx_reset_accumulators(h) == -ENOTSUP);
	assert(writes == 0);
	ina2xx_del(h);

	cfg.model = INA228;
	cfg.shunt_ohms = 16.2e-3f;
	cfg.max_current_a = 10.0f;
	h = open_ok(&cfg);
	writes = 0;
	regs[0x00] = 0;
	assert(ina2xx_reset_accumulators(h) == 0);
	assert(writes == 1 && regs[0x00] == 0x4000);
	bus_status = HAL_TIMEOUT;
	assert(ina2xx_reset_accumulators(h) == -ETIMEDOUT);
	bus_status = HAL_OK;

	assert(ina2xx_reset_accumulators(NULL) == -EINVAL);
	assert(ina2xx_read(NULL, &r) == -EINVAL);
	assert(ina2xx_read(h, NULL) == -EINVAL);
	assert(ina2xx_del(NULL) == -EINVAL);
	assert(ina2xx_del(h) == 0);
	assert(allocs == 1 && frees == 1);
}

int main(void)
{
	test_init_ina219();
	test_init_ina226();
	test_init_ina228();
	test_init_ina237();
	test_read_ina237();
	test_read_16bit_byte_order();
	test_read_ina228_widths();
	test_read_error_leaves_output();
	test_presence();
	test_refused_config();
	test_reset_accumulators_and_del();
	printf("ina2xx driver: all checks passed\n");
	return 0;
}
