/*
 * Host-side check of the INA2xx through the powermon API, against the same
 * fake INA2xx behind a fake HAL as test_ina2xx.c, which checks the
 * ina2xx_init() handle API. The cases themselves are ../ina2xx_dev_cases.h,
 * shared with esp/test_ina2xx.c.
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
#include "powermon.h"
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

#define BUS (&bus)

static void bus_fail(bool on)
{
	bus_status = on ? HAL_TIMEOUT : HAL_OK;
}

/* The weak default clock: HAL_GetTick() milliseconds, as microseconds. */
static void set_clock_us(int64_t us)
{
	stub_tick = (uint32_t)(us / 1000);
}

static uint32_t waited_ms(void)
{
	return delayed_ms;
}

#include "../ina2xx_dev_cases.h"

int main(void)
{
	run_dev_cases();
	printf("ina2xx powermon checks passed\n");
	return 0;
}
