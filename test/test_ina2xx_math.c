/*
 * Host-side check of the raw-register conversions. No embedded toolchain
 * needed -- see test/Makefile.
 *
 * Two kinds of check live here. The datasheet section holds worked examples
 * copied straight out of docs/, so the constants are provably the right ones.
 * The edge sections hold the inputs that silently produce a wrong number
 * rather than an obviously broken one: sign boundaries, ignored bits, and the
 * points where a value stops fitting its register.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "ina2xx_math.h"

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-6 * fabs(b) + 1e-9)

/* ---------------- worked examples from the datasheets ---------------- */

static void test_datasheet_examples(void)
{
    /* Sign extension across the widths the parts actually use. */
    assert(ina2xx_sext(0xFFFFF, 20) == -1);
    assert(ina2xx_sext(0x7FFFF, 20) == 524287);
    assert(ina2xx_sext(0x80000, 20) == -524288);
    assert(ina2xx_sext(0xFFFFFFFFFFULL, 40) == -1);
    assert(ina2xx_sext(0x7FFFFFFFFFULL, 40) == 549755813887LL);

    /* INA219: bus data lives in bits 15:3, so 0x1F40<<3 == 32V; shunt 10uV/LSB. */
    assert(NEAR(ina219_bus_v(0x1F40 << 3), 32.0f));
    assert(NEAR(ina219_shunt_v(2000), 0.02f));      /* datasheet table: 2000 -> 20mV */
    assert(NEAR(ina219_shunt_v(0xFFFF), -10e-6f));

    /* INA226 sec. 7.5.1 worked example: 1mA/bit LSB, 2mOhm shunt -> CAL 2560. */
    assert(ina226_cal(1e-3, 2e-3, 1.0) == 2560);
    assert(NEAR(ina226_bus_v(9584), 11.98f));       /* same example: 0x2570 -> 11.98V */
    assert(NEAR(ina226_shunt_v(8000), 0.02f));      /* 8000 * 2.5uV = 20mV */
    assert(NEAR(ina226_power_w(4792, 1e-3f), 119.8f));

    /* The trim knob scales the calibration register linearly. */
    assert(ina226_cal(1e-3, 2e-3, 0.5) == 1280);

    /* INA219 sec. 8.5.1: 0.04096 / (LSB * R). */
    assert(ina219_cal(1e-3, 2e-3, 1.0) == 20480);

    /* INA228 sec. 8.2.2: 10A max -> 19.0735uA/bit; 16.2mOhm shunt -> CAL 4050. */
    assert(NEAR(ina228_current_lsb(10.0f), 19.0734863e-6f));
    assert(ina228_cal(ina228_current_lsb(10.0f), 16.2e-3, 1.0) == 4050);

    /* 20-bit results are left-aligned in 24 bits. */
    assert(NEAR(ina228_bus_v(0x1000 << 4), 4096 * 195.3125e-6f));
    assert(NEAR(ina228_shunt_v((0xFFFFF << 4) & 0xFFFFFF), -312.5e-9f));
    assert(NEAR(ina228_die_temp_c(0x0C80), 25.0f));  /* 3200 * 7.8125m = 25C */
    assert(NEAR(ina228_die_temp_c(0xF380), -25.0f));
    assert(NEAR(ina228_charge_c(0xFFFFFFFFFFULL, 1e-3f), -1e-3f));

    /* INA228 sec. 8.2.2.5 Table 8-4: 10A max, returned values -> results.
     * The datasheet rounds current and power, so those two get its own
     * precision; the rest are exact. 20-bit results sit left of 4 reserved bits. */
    {
        float lsb = ina228_current_lsb(10.0f);

        assert(NEAR(ina228_shunt_v(311040u << 4), 0.0972f));
        assert(fabs(ina228_current_a(314572u << 4, lsb) - 6.0) < 1e-4);
        assert(NEAR(ina228_bus_v(245760u << 4), 48.0f));
        assert(fabs(ina228_power_w(4718604u, lsb) - 288.001) < 1e-3);
        assert(NEAR(ina228_energy_j(1061683200ULL, lsb), 1036800.0f));
        assert(NEAR(ina228_charge_c(1132462080ULL, lsb), 21600.0f));
        assert(NEAR(ina228_die_temp_c(3200), 25.0f));
    }

    /* INA237 sec. 8.2.2.3: 10A max -> 305.1758uA/bit; 16.2mOhm shunt -> CAL 4050. */
    assert(NEAR(ina237_current_lsb(10.0f), 305.17578e-6f));
    assert(ina237_cal(ina237_current_lsb(10.0f), 16.2e-3, 1.0) == 4050);

    /* INA237 sec. 8.2.2.5 Table 8-4, returned values -> results. Current and
     * power are rounded there, so they get its precision. The table gives
     * DIETEMP as the 12-bit field, 200; the register holds it in bits 15:4. */
    {
        float lsb = ina237_current_lsb(10.0f);

        assert(NEAR(ina237_shunt_v(19440), 0.0972f));
        assert(fabs(ina237_current_a(19660, lsb) - 5.9997) < 1e-4);
        assert(NEAR(ina237_bus_v(15360), 48.0f));
        assert(fabs(ina237_power_w(4718604u, lsb) - 288.0) < 1e-2);
        assert(NEAR(ina237_power_w(1, lsb), 61.035156e-6f));
        assert(NEAR(ina237_die_temp_c(200 << 4), 25.0f));
    }
}

/* ---------------- sign extension edges ---------------- */

static void test_sext_edges(void)
{
    /* Contract: bits must be 1..64. Anything else shifts out of range and is
     * undefined, so it is not tested here - the callers pass constants. */

    /* Bits above the requested width are junk and must be masked away, so a
     * caller that reads four bytes into a 24-bit register still decodes. */
    assert(ina2xx_sext(0xFFFFFFFFULL, 20) == -1);
    assert(ina2xx_sext(0xFFF00000ULL | 0x7FFFF, 20) == 524287);
    assert(ina2xx_sext(~0ULL, 40) == -1);

    /* Exactly on the sign boundary, at both widths in use. */
    assert(ina2xx_sext(0x7FFFF, 20) == 524287);     /* largest positive */
    assert(ina2xx_sext(0x80000, 20) == -524288);    /* one step further wraps */
    assert(ina2xx_sext(0x7FFFFFFFFFULL, 40) == 549755813887LL);
    assert(ina2xx_sext(0x8000000000ULL, 40) == -549755813888LL);

    /* Full width: the mask must not overflow when bits == 64. */
    assert(ina2xx_sext(~0ULL, 64) == -1);
    assert(ina2xx_sext(0x7FFFFFFFFFFFFFFFULL, 64) == 9223372036854775807LL);

    /* Degenerate but well defined: one bit holds only 0 and -1. */
    assert(ina2xx_sext(0, 1) == 0);
    assert(ina2xx_sext(1, 1) == -1);

    assert(ina2xx_sext(0, 20) == 0);
}

/* ---------------- INA219 ---------------- */

static void test_ina219_edges(void)
{
    /* The low three bits are the CNVR and OVF flags, not voltage. Setting
     * them must not move the reading by even one LSB. */
    assert(ina219_bus_v(0x1F40 << 3) == ina219_bus_v((0x1F40 << 3) | 0x7));
    assert(ina219_bus_v(0) == ina219_bus_v(0x7));

    /* Bus voltage is unsigned: the top bit is data, not a sign. */
    assert(ina219_bus_v(0x0000) == 0.0f);
    assert(ina219_bus_v(0xFFFF) > 32.0f);           /* 8191 counts * 4mV */
    assert(NEAR(ina219_bus_v(0xFFFF), 8191 * 4e-3f));
    assert(ina219_bus_v(0x8000) > 0.0f);            /* must not read negative */

    /* Shunt voltage is two's complement: current flows both ways. */
    assert(ina219_shunt_v(0x0000) == 0.0f);
    assert(NEAR(ina219_shunt_v(0x7FFF), 0.32767f)); /* largest positive */
    assert(NEAR(ina219_shunt_v(0x8000), -0.32768f));/* largest negative */
    assert(ina219_shunt_v(0x7FFF) > 0.0f);
    assert(ina219_shunt_v(0x8000) < 0.0f);          /* the whole point */
    assert(NEAR(ina219_shunt_v(0xFF9C), -0.001f));  /* -100 counts */

    /* Power is unsigned; the top bit must stay magnitude. */
    assert(ina219_power_w(0xFFFF, 1e-3f) > 0.0f);
    assert(NEAR(ina219_power_w(0xFFFF, 1e-3f), 65535 * 20.0f * 1e-3f));
    assert(ina219_power_w(0, 1e-3f) == 0.0f);

    /* Current LSB is a plain division, so it stays linear and finite. */
    assert(NEAR(ina219_current_lsb(3.2f), 3.2f / 32768.0f));
    assert(ina219_current_lsb(1e-6f) > 0.0f);       /* no underflow to zero */
    assert(isfinite(ina219_current_lsb(1e6f)));
}

/* ---------------- INA226 ---------------- */

static void test_ina226_edges(void)
{
    /* Bus voltage uses all sixteen bits, unsigned, with no flags to mask. */
    assert(ina226_bus_v(0x0000) == 0.0f);
    assert(NEAR(ina226_bus_v(0xFFFF), 65535 * 1.25e-3f));
    assert(ina226_bus_v(0x8000) > 0.0f);

    /* Shunt voltage is two's complement. */
    assert(NEAR(ina226_shunt_v(0x7FFF), 32767 * 2.5e-6f));
    assert(NEAR(ina226_shunt_v(0x8000), -32768 * 2.5e-6f));
    assert(ina226_shunt_v(0x8000) < 0.0f);
    assert(ina226_shunt_v(0xFFFF) < 0.0f);

    assert(ina226_power_w(0xFFFF, 1e-3f) > 0.0f);
    assert(NEAR(ina226_current_lsb(8.0f), 8.0f / 32768.0f));
}

/* ---------------- INA228 ---------------- */

static void test_ina228_edges(void)
{
    /* The bottom four bits of a 24-bit result are reserved. Setting them
     * must not change the decoded value. */
    assert(ina228_shunt_v(0x12345 << 4) == ina228_shunt_v((0x12345 << 4) | 0xF));
    assert(ina228_bus_v(0x1000 << 4) == ina228_bus_v((0x1000 << 4) | 0xF));
    assert(ina228_current_a(0x100 << 4, 1e-6f) ==
           ina228_current_a((0x100 << 4) | 0xF, 1e-6f));

    /* Bits above 24 cannot reach the result either. */
    assert(ina228_shunt_v(0x00FFFFF0u) == ina228_shunt_v(0xFFFFFFF0u));

    /* 20-bit sign boundary, shunt and current. */
    assert(NEAR(ina228_shunt_v(0x7FFFF << 4), 524287 * 312.5e-9f));
    assert(ina228_shunt_v((0x80000 << 4) & 0xFFFFFF) < 0.0f);
    assert(NEAR(ina228_current_a(0x7FFFF << 4, 1e-6f), 524287e-6f));
    assert(NEAR(ina228_current_a((0x80000 << 4) & 0xFFFFFF, 1e-6f), -524288e-6f));

    /* Bus voltage runs through the same signed decode, so bit 19 of the
     * 20-bit field is the sign. Below it, every value is positive. */
    assert(ina228_bus_v(0) == 0.0f);
    assert(ina228_bus_v(0x7FFFF << 4) > 0.0f);
    assert(NEAR(ina228_bus_v(0x7FFFF << 4), 524287 * 195.3125e-6f));

    /* Die temperature is signed, and below freezing is a normal reading. */
    assert(ina228_die_temp_c(0x0000) == 0.0f);
    assert(ina228_die_temp_c(0x8000) < 0.0f);
    assert(NEAR(ina228_die_temp_c(0x8000), -32768 * 7.8125e-3f));
    assert(NEAR(ina228_die_temp_c(0xFFFF), -7.8125e-3f));

    /* POWER is 24-bit UNSIGNED and is not shifted. If anyone ever "fixes"
     * this to match its neighbours, these two catch it. */
    assert(ina228_power_w(0xFFFFFF, 1e-6f) > 0.0f);
    assert(NEAR(ina228_power_w(0xFFFFFF, 1e-6f), 3.2f * 1e-6f * 16777215.0f));
    assert(ina228_power_w(0x800000, 1e-6f) > 0.0f);     /* top bit is magnitude */

    /* ENERGY is 40-bit UNSIGNED: it only ever counts up. */
    assert(ina228_energy_j(0, 1e-6f) == 0.0f);
    assert(ina228_energy_j(0x8000000000ULL, 1e-6f) > 0.0f);
    assert(ina228_energy_j(0xFFFFFFFFFFULL, 1e-6f) >
           ina228_energy_j(0x8000000000ULL, 1e-6f));

    /* CHARGE is 40-bit SIGNED: it counts down again when current reverses. */
    assert(ina228_charge_c(0, 1e-6f) == 0.0f);
    assert(ina228_charge_c(0xFFFFFFFFFFULL, 1e-6f) < 0.0f);
    assert(NEAR(ina228_charge_c(0x8000000000ULL, 1e-6f), -549755813888.0 * 1e-6f));
    assert(NEAR(ina228_charge_c(0x7FFFFFFFFFULL, 1e-6f), 549755813887.0 * 1e-6f));

    assert(NEAR(ina228_current_lsb(10.0f), 10.0f / 524288.0f));
}

/* ---------------- INA237 ---------------- */

static void test_ina237_edges(void)
{
    /* DIETEMP's bottom four bits are reserved: they must not move it. */
    assert(ina237_die_temp_c(200 << 4) == ina237_die_temp_c((200 << 4) | 0xF));
    /* Bit 15 is the sign of the 12-bit field. */
    assert(NEAR(ina237_die_temp_c(0xFFF0), -0.125f));
    assert(NEAR(ina237_die_temp_c(0x8000), -2048 * 0.125f));
    assert(NEAR(ina237_die_temp_c(0x7FF0), 2047 * 0.125f));

    /* Shunt and current are two's complement. */
    assert(NEAR(ina237_shunt_v(0x8000), -32768 * 5e-6f));
    assert(ina237_shunt_v(0xFFFF) < 0.0f);
    assert(ina237_current_a(0xFFFF, 1e-3f) == -1e-3f);

    /* 85 V is 27200 counts, well clear of the sign bit. */
    assert(NEAR(ina237_bus_v(27200), 85.0f));

    /* POWER is 24-bit UNSIGNED: the top bit is magnitude. */
    assert(ina237_power_w(0x800000, 1e-6f) > 0.0f);
    assert(NEAR(ina237_power_w(0xFFFFFF, 1e-6f), 0.2f * 1e-6f * 16777215.0f));

    /* Like the INA228, cal grows with LSB and shunt, and drops bit 15. */
    assert(ina237_cal(2e-4, 16.2e-3, 1.0) > ina237_cal(1e-4, 16.2e-3, 1.0));
    assert(ina237_cal(ina237_current_lsb(10.0f), 0.2, 1.0) == 0);   /* 50000 */
    assert(ina237_cal(NAN, 16.2e-3, 1.0) == 0);
    assert(ina237_cal(1e-4, 16.2e-3, 0.0) == 0);
}

/* ---------------- calibration ---------------- */

static void test_cal_edges(void)
{
    /* Trim scales linearly and is the knob for shunt tolerance. */
    assert(ina219_cal(1e-3, 2e-3, 1.0) == 20480);
    assert(ina219_cal(1e-3, 2e-3, 0.5) == 10240);
    assert(ina226_cal(1e-3, 2e-3, 2.0) == 5120);

    /* Truncation, not rounding: the register is an integer. */
    assert(ina226_cal(1e-3, 2e-3, 1.0 + 1e-9) == 2560);

    /* A bigger LSB or a bigger shunt means a smaller calibration value. */
    assert(ina219_cal(2e-3, 2e-3, 1.0) < ina219_cal(1e-3, 2e-3, 1.0));
    assert(ina219_cal(1e-3, 4e-3, 1.0) < ina219_cal(1e-3, 2e-3, 1.0));

    /* INA228 goes the other way: cal is proportional to LSB and shunt. */
    assert(ina228_cal(2e-6, 16.2e-3, 1.0) > ina228_cal(1e-6, 16.2e-3, 1.0));

}

/* ----------------------------------------------------------------------
 * Arithmetic that has no answer. None of these can be reached through
 * ina2xx_init, which rejects a non-positive shunt or current first, but the
 * helpers are callable on their own and a double that does not fit a
 * uint16_t is undefined behaviour, not a wrong number. They must all come
 * back 0, meaning "no representable calibration".
 * ---------------------------------------------------------------------- */

static void test_cal_invalid_math(void)
{
    /* Division by zero, from either operand or both. */
    assert(ina219_cal(0.0, 2e-3, 1.0) == 0);        /* -> +inf */
    assert(ina219_cal(1e-3, 0.0, 1.0) == 0);        /* -> +inf */
    assert(ina219_cal(0.0, 0.0, 1.0) == 0);         /* -> NaN  */
    assert(ina226_cal(0.0, 2e-3, 1.0) == 0);
    assert(ina226_cal(1e-3, 0.0, 1.0) == 0);

    /* A zero trim scales the whole thing away. */
    assert(ina219_cal(1e-3, 2e-3, 0.0) == 0);
    assert(ina228_cal(1e-6, 16.2e-3, 0.0) == 0);

    /* Not-a-number in any position. NaN compares false against everything,
     * so the guard has to be written as a rejection to catch it. */
    assert(ina219_cal(NAN, 2e-3, 1.0) == 0);
    assert(ina219_cal(1e-3, NAN, 1.0) == 0);
    assert(ina219_cal(1e-3, 2e-3, NAN) == 0);
    assert(ina228_cal(NAN, 16.2e-3, 1.0) == 0);

    /* Infinities, which divide down to zero or multiply up past the range. */
    assert(ina219_cal(INFINITY, 2e-3, 1.0) == 0);
    assert(ina219_cal(1e-3, 2e-3, INFINITY) == 0);
    assert(ina228_cal(INFINITY, 16.2e-3, 1.0) == 0);

    /* Negatives, which have no unsigned register to land in. */
    assert(ina219_cal(-1e-3, 2e-3, 1.0) == 0);
    assert(ina228_cal(1e-6, -16.2e-3, 1.0) == 0);

    /* Denormal inputs that underflow the product to zero. */
    assert(ina219_cal(1e-300, 1e-300, 1.0) == 0);

    /* Real configurations that simply do not fit. A 100mA range across a
     * 0.1 ohm shunt needs 134218, and used to wrap to 3145 - a calibration
     * 43 times too small, with every reading silently wrong. */
    assert(ina219_cal(ina219_current_lsb(0.1f), 0.1, 1.0) == 0);
    assert(ina219_cal(ina219_current_lsb(0.01f), 1.0, 1.0) == 0);
    assert(ina228_cal(ina228_current_lsb(10.0f), 0.3, 1.0) == 0);

    /* And one that rounds away to nothing at the other end. */
    assert(ina219_cal(ina219_current_lsb(100.0f), 20.0, 1.0) == 0);

    /* INA226 and INA228 drop bit 15 of the calibration register, so 41943
     * (0xA3D7) would be programmed as 9175 and every reading would be 0.22x.
     * Both have to be refused, not wrapped. */
    assert(ina226_cal(ina226_current_lsb(2.0f), 0.002, 1.0) == 0);
    assert(ina228_cal(ina228_current_lsb(20.0f), 0.1, 1.0) == 0);

    /* Exactly on the boundaries. */
    assert(ina2xx_cal_fit(1.0, 65535.0) == 1);
    assert(ina2xx_cal_fit(0.999, 65535.0) == 0);    /* would truncate to 0 */
    assert(ina2xx_cal_fit(65535.0, 65535.0) == 65535);
    assert(ina2xx_cal_fit(65535.9, 65535.0) == 0);  /* past max, even if it truncates */
    assert(ina2xx_cal_fit(65536.0, 65535.0) == 0);
    assert(ina2xx_cal_fit(32767.0, 32767.0) == 32767);
    assert(ina2xx_cal_fit(32768.0, 32767.0) == 0);
    assert(ina2xx_cal_fit(0.0, 65535.0) == 0);
    assert(ina2xx_cal_fit(-1.0, 65535.0) == 0);

    /* Through the part helpers: 0.00512 / (0.00512 * 1) is exactly 1, so the
     * trim lands the INA226 exactly on 32767 and one past it. The INA219
     * keeps its sixteenth bit. */
    assert(ina226_cal(0.00512, 1.0, 32767.0) == 32767);
    assert(ina226_cal(0.00512, 1.0, 32768.0) == 0);
    assert(ina219_cal(0.04096, 1.0, 32768.0) == 32768);

    /* Everything valid still comes back untouched. */
    assert(ina219_cal(1e-3, 2e-3, 1.0) == 20480);
    assert(ina226_cal(1e-3, 2e-3, 1.0) == 2560);
    assert(ina228_cal(ina228_current_lsb(10.0f), 16.2e-3, 1.0) == 4050);
}


/* ----------------------------------------------------------------------
 * Exhaustive sweeps. These functions take a 16- or 24-bit input, so the
 * whole domain fits in a loop and no input can hide. Each check is a
 * property derived from the parameter type alone: what the sign bit means,
 * which bits are dropped, and whether the result can ever go negative.
 * ---------------------------------------------------------------------- */

static void test_all_16bit_inputs(void)
{
    float prev_bus219 = -1.0f;
    float prev_bus226 = -1.0f;
    uint32_t r;

    for (r = 0; r <= 0xFFFF; r++) {
        uint16_t raw = (uint16_t)r;
        int16_t signed_raw = (int16_t)raw;
        int negative = (raw & 0x8000u) != 0;

        /* --- unsigned decodes can never produce a negative reading --- */
        assert(ina219_bus_v(raw) >= 0.0f);
        assert(ina226_bus_v(raw) >= 0.0f);
        assert(ina219_power_w(raw, 1e-3f) >= 0.0f);
        assert(ina226_power_w(raw, 1e-3f) >= 0.0f);

        /* ... and they rise with the raw value, never fold over --- */
        assert(ina219_bus_v(raw) >= prev_bus219);
        assert(ina226_bus_v(raw) > prev_bus226 || raw == 0);
        prev_bus219 = ina219_bus_v(raw);
        prev_bus226 = ina226_bus_v(raw);

        /* --- the INA219 bus field is bits 15:3; the low 3 are flags --- */
        assert(ina219_bus_v(raw) == ina219_bus_v((uint16_t)(raw & ~7u)));
        assert(ina219_bus_v(raw) == (raw >> 3) * 4e-3f);

        /* --- signed decodes: bit 15 alone decides the sign --- */
        assert((ina219_shunt_v(raw) < 0.0f) == (negative != 0));
        assert((ina226_shunt_v(raw) < 0.0f) == (negative != 0));
        assert((ina228_die_temp_c(raw) < 0.0f) == (negative != 0));

        /* --- and the magnitude is exactly the two's complement value --- */
        assert(ina219_shunt_v(raw) == signed_raw * 10e-6f);
        assert(ina226_shunt_v(raw) == signed_raw * 2.5e-6f);
        assert(ina228_die_temp_c(raw) == signed_raw * 7.8125e-3f);

        /* --- INA237: all signed, DIETEMP's low nibble ignored --- */
        assert((ina237_shunt_v(raw) < 0.0f) == (negative != 0));
        assert(ina237_shunt_v(raw) == signed_raw * 5e-6f);
        assert(ina237_bus_v(raw) == signed_raw * 3.125e-3f);
        assert(ina237_current_a(raw, 1e-3f) == signed_raw * 1e-3f);
        assert((ina237_die_temp_c(raw) < 0.0f) == (negative != 0));
        assert(ina237_die_temp_c(raw) ==
               ina237_die_temp_c((uint16_t)(raw & ~0xFu)));
    }
}

static void test_all_24bit_inputs(void)
{
    uint32_t r;

    /* Step by 0x10 and cover the four reserved bits separately, so the
     * sweep stays a second rather than a minute. */
    for (r = 0; r <= 0xFFFFFF; r += 0x10) {
        int32_t field = (int32_t)ina2xx_sext(r >> 4, 20);
        uint32_t dirty = r | 0xF;                  /* reserved bits set */

        /* The bottom four bits are not part of any result. */
        assert(ina228_shunt_v(r) == ina228_shunt_v(dirty));
        assert(ina228_bus_v(r) == ina228_bus_v(dirty));
        assert(ina228_current_a(r, 1e-6f) == ina228_current_a(dirty, 1e-6f));

        /* Bits above 24 cannot reach the result either. */
        assert(ina228_shunt_v(r) == ina228_shunt_v(r | 0xFF000000u));

        /* Sign of every 20-bit decode follows the sign of the field. */
        assert((ina228_shunt_v(r) < 0.0f) == (field < 0));
        assert((ina228_current_a(r, 1e-6f) < 0.0f) == (field < 0));
        assert((ina228_bus_v(r) < 0.0f) == (field < 0));

        /* Magnitude is exactly the field times the LSB. */
        assert(ina228_shunt_v(r) == field * 312.5e-9f);
        assert(ina228_current_a(r, 1e-6f) == field * 1e-6f);

        /* POWER reads the full 24 bits unsigned, so it never goes negative
         * and never ignores the low nibble. */
        assert(ina228_power_w(r, 1e-6f) >= 0.0f);
        assert(ina228_power_w(dirty, 1e-6f) > ina228_power_w(r, 1e-6f));
    }
}

static void test_40bit_boundaries(void)
{
    /* Too wide to sweep, so walk every bit position instead: each one is a
     * sign-boundary candidate for the signed decode. */
    int bit;

    for (bit = 0; bit < 40; bit++) {
        uint64_t v = 1ULL << bit;

        /* ENERGY is unsigned: no single bit can make it negative. */
        assert(ina228_energy_j(v, 1e-6f) > 0.0f);

        /* CHARGE is signed at 40 bits: only bit 39 is the sign. */
        assert((ina228_charge_c(v, 1e-6f) < 0.0f) == (bit == 39));
    }

    /* All ones is -1 for the signed decode, and the largest value for the
     * unsigned one. */
    assert(ina228_charge_c(0xFFFFFFFFFFULL, 1e-6f) < 0.0f);
    assert(ina228_energy_j(0xFFFFFFFFFFULL, 1e-6f) > 0.0f);

    /* Bits above 40 belong to neither and must be masked by the decode. */
    assert(ina228_charge_c(0xFFFFFFFFFFULL, 1e-6f) ==
           ina228_charge_c(~0ULL, 1e-6f));
}

int main(void)
{
    test_datasheet_examples();
    test_sext_edges();
    test_ina219_edges();
    test_ina226_edges();
    test_ina228_edges();
    test_ina237_edges();
    test_cal_edges();
    test_all_16bit_inputs();
    test_all_24bit_inputs();
    test_40bit_boundaries();
    test_cal_invalid_math();

    printf("ina2xx math: all checks passed\n");
    return 0;
}
