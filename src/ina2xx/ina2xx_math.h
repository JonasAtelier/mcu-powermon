/*
 * Pure raw-register <-> physical-unit conversions, split out so they can be
 * compiled and asserted on the host (see test/test_ina2xx_math.c) without
 * an embedded toolchain. Every constant here is quoted from the datasheet in
 * docs/.
 */
#pragma once

#include <stdint.h>

/* ---- INA219 (SBOS448G) ---- */
#define INA219_BUS_SHIFT      3           /* bus result sits in bits 15:3 */
#define INA219_BUS_LSB_V      4e-3f       /* 4 mV per count */
#define INA219_SHUNT_LSB_V    10e-6f      /* 10 uV per count */
#define INA219_CURRENT_FS     32768.0f    /* 2^15 counts of current full scale */
#define INA219_CAL_CONST      0.04096     /* fixed numerator of the cal equation */
#define INA219_POWER_RATIO    20.0f       /* power LSB = 20 x current LSB */

/* ---- INA226 (SBOS547C) ---- */
#define INA226_BUS_LSB_V      1.25e-3f    /* 1.25 mV per count */
#define INA226_SHUNT_LSB_V    2.5e-6f     /* 2.5 uV per count */
#define INA226_CURRENT_FS     32768.0f    /* 2^15 */
#define INA226_CAL_CONST      0.00512
#define INA226_POWER_RATIO    25.0f       /* power LSB = 25 x current LSB */

/* ---- INA228 (SLYS021A) ---- */
#define INA228_RESULT_SHIFT   4           /* 20-bit result left-aligned in 24 bits */
#define INA228_RESULT_BITS    20
#define INA228_CHARGE_BITS    40          /* charge accumulator is 40 bits */
#define INA228_BUS_LSB_V      195.3125e-6f
#define INA228_SHUNT_LSB_V    312.5e-9f   /* ADCRANGE = 0; +/-163.84 mV full scale */
#define INA228_TEMP_LSB_C     7.8125e-3f
#define INA228_CURRENT_FS     524288.0f   /* 2^19 */
#define INA228_CAL_CONST      13107.2e6
#define INA228_POWER_RATIO    3.2f        /* power LSB = 3.2 x current LSB */
#define INA228_ENERGY_RATIO   16.0f       /* energy LSB = 16 x power LSB */

/* ---- INA237 (SBOSA20A) ---- */
#define INA237_BUS_LSB_V      3.125e-3f   /* Table 7-9 */
#define INA237_SHUNT_LSB_V    5e-6f       /* ADCRANGE = 0, Table 7-8; +/-163.84 mV */
#define INA237_TEMP_SHIFT     4           /* DIETEMP in bits 15:4, Table 7-10 */
#define INA237_TEMP_BITS      12
#define INA237_TEMP_LSB_C     125e-3f     /* Table 7-10 */
#define INA237_CURRENT_FS     32768.0f    /* 2^15, Equation 2 */
#define INA237_CAL_CONST      819.2e6     /* Equation 1 */
#define INA237_POWER_RATIO    0.2f        /* Equation 4: power LSB = 0.2 x current LSB */

/* Two's complement sign extension of the low `bits` of v. */
static inline int64_t ina2xx_sext(uint64_t v, int bits)
{
    uint64_t msk, sign;

    msk = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
    sign = 1ULL << (bits - 1);
    v &= msk;

    return (int64_t)((v ^ sign) - sign);
}


/*
 * Every calibration value has to land in its register's writable field:
 * all 16 bits on the INA219, only bits 14:0 on the INA226 and INA228, whose
 * bit 15 is reserved and dropped on write - so `max` is 65535 or 32767.
 * Converting a double that does not fit is undefined in C, and the inputs
 * reach here unscreened: a zero LSB or a zero shunt makes the division
 * produce infinity, and a small shunt drop makes a value far past `max`.
 * Screen it once, here, and report 0 for anything unrepresentable.
 *
 * 0 is safe as that signal because it is never a usable calibration - the
 * parts report zero current and zero power with the register at 0 - so the
 * caller must treat it as a configuration error rather than program it.
 */
static inline uint16_t ina2xx_cal_fit(double cal, double max)
{
    /* Written as a rejection so that NaN, which compares false against
     * everything, falls into it too. */
    if (!(cal >= 1.0 && cal <= max))
        return 0;

    return (uint16_t)cal;
}

/* ---- INA219: shunt 10uV/LSB, bus 4mV/LSB in bits 15:3 ---- */
static inline float ina219_bus_v(uint16_t raw)
{
    return (raw >> INA219_BUS_SHIFT) * INA219_BUS_LSB_V;
}
static inline float ina219_shunt_v(uint16_t raw)
{
    return (int16_t)raw * INA219_SHUNT_LSB_V;
}
static inline float ina219_current_lsb(float max_a)
{
    return max_a / INA219_CURRENT_FS;
}
/* Calibration is FS15:FS1, all 16 bits wide (SBOS448G sec. 8.6.4.1, Figure 27). */
static inline uint16_t ina219_cal(double lsb, double r_ohms, double trim)
{
    return ina2xx_cal_fit(INA219_CAL_CONST / (lsb * r_ohms) * trim, 65535.0);
}
static inline float ina219_power_w(uint16_t raw, float lsb)
{
    return raw * INA219_POWER_RATIO * lsb;
}

/* ---- INA226: shunt 2.5uV/LSB, bus 1.25mV/LSB ---- */
static inline float ina226_bus_v(uint16_t raw)
{
    return raw * INA226_BUS_LSB_V;
}
static inline float ina226_shunt_v(uint16_t raw)
{
    return (int16_t)raw * INA226_SHUNT_LSB_V;
}
static inline float ina226_current_lsb(float max_a)
{
    return max_a / INA226_CURRENT_FS;
}
/* Calibration D15 is reserved; FS14:FS0 only (SBOS547C Table 7-11). */
static inline uint16_t ina226_cal(double lsb, double r_ohms, double trim)
{
    return ina2xx_cal_fit(INA226_CAL_CONST / (lsb * r_ohms) * trim, 32767.0);
}
static inline float ina226_power_w(uint16_t raw, float lsb)
{
    return raw * INA226_POWER_RATIO * lsb;
}

/* ---- INA228: 20-bit results left-aligned in a 24-bit register ---- */
static inline float ina228_bus_v(uint32_t raw24)
{
    return ina2xx_sext(raw24 >> INA228_RESULT_SHIFT, INA228_RESULT_BITS) *
           INA228_BUS_LSB_V;
}
static inline float ina228_shunt_v(uint32_t raw24)
{
    return ina2xx_sext(raw24 >> INA228_RESULT_SHIFT, INA228_RESULT_BITS) *
           INA228_SHUNT_LSB_V;
}
static inline float ina228_die_temp_c(uint16_t raw)
{
    return (int16_t)raw * INA228_TEMP_LSB_C;
}
static inline float ina228_current_lsb(float max_a)
{
    return max_a / INA228_CURRENT_FS;
}
/* SHUNT_CAL bit 15 is reserved; bits 14:0 only (SLYS021A Table 7-7). */
static inline uint16_t ina228_cal(double lsb, double r_ohms, double trim)
{
    return ina2xx_cal_fit(INA228_CAL_CONST * lsb * r_ohms * trim, 32767.0);
}
static inline float ina228_current_a(uint32_t raw24, float lsb)
{
    return ina2xx_sext(raw24 >> INA228_RESULT_SHIFT, INA228_RESULT_BITS) *
           lsb;
}
static inline float ina228_power_w(uint32_t raw24, float lsb)
{
    return INA228_POWER_RATIO * lsb * raw24;
}
static inline float ina228_energy_j(uint64_t raw40, float lsb)
{
    return INA228_ENERGY_RATIO * INA228_POWER_RATIO * lsb * raw40;
}
static inline float ina228_charge_c(uint64_t raw40, float lsb)
{
    return ina2xx_sext(raw40, INA228_CHARGE_BITS) * lsb;
}

/* ---- INA237: 16-bit results, 24-bit unsigned power ---- */
/* Two's complement, "however always positive" (Table 7-9). */
static inline float ina237_bus_v(uint16_t raw)
{
    return (int16_t)raw * INA237_BUS_LSB_V;
}
static inline float ina237_shunt_v(uint16_t raw)
{
    return (int16_t)raw * INA237_SHUNT_LSB_V;
}
/* Bits 3:0 are reserved and read 0; masked anyway by the decode. */
static inline float ina237_die_temp_c(uint16_t raw)
{
    return ina2xx_sext(raw >> INA237_TEMP_SHIFT, INA237_TEMP_BITS) *
           INA237_TEMP_LSB_C;
}
static inline float ina237_current_lsb(float max_a)
{
    return max_a / INA237_CURRENT_FS;
}
/* SHUNT_CAL bit 15 is reserved; bits 14:0 only (SBOSA20A Table 7-7). */
static inline uint16_t ina237_cal(double lsb, double r_ohms, double trim)
{
    return ina2xx_cal_fit(INA237_CAL_CONST * lsb * r_ohms * trim, 32767.0);
}
static inline float ina237_current_a(uint16_t raw, float lsb)
{
    return (int16_t)raw * lsb;
}
/* POWER is 24 bits, unsigned (Table 7-12). */
static inline float ina237_power_w(uint32_t raw24, float lsb)
{
    return INA237_POWER_RATIO * lsb * raw24;
}
