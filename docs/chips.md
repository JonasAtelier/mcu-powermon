# Chip reference

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## INA2xx

One driver, `src/ina2xx/`, serves four TI current, voltage and power
monitors. They are one register family: the INA219 and INA226 share a layout
of 16-bit registers, and the INA237 sits at the INA228's addresses
(SBOSA20A Table 7-3). What differs — result widths, LSBs, calibration
constants — is per model in `src/ina2xx/ina2xx_math.h`, and
`ina2xx_config_t.model` picks one at run time. Each model's code is built
only under its own option, `POWERMON_INA219` to `POWERMON_INA237`.

| Function | Does |
|---|---|
| `ina2xx_probe(&pm, bus, &cfg)` | Check the chip answers, calibrate it, start continuous conversion, wait 10 ms for the first result. `cfg` is required |
| `ina2xx_remove(&pm)` | Release the device; the bus is the app's |
| `powermon_read(&pm, &s)` | Every quantity the model has, in one call |
| `ina2xx_reset_energy(&pm)` | Zero the INA228's energy and charge accumulators |

```c
ina2xx_config_t cfg = {
    .model         = INA226,                 /* INA219, INA226, INA228 or INA237 */
    .shunt_ohms    = 0.1f,
    .max_current_a = 3.2f,
    .avg_samples   = 16,                     /* optional */
};
```

`addr` defaults to `0x40` (A0 and A1 tied to GND) on all four parts.

### What each model reports

| Field | INA219 | INA226 | INA228 | INA237 |
|---|:-:|:-:|:-:|:-:|
| `bus_v`, `shunt_v`, `current_a`, `power_w` | yes | yes | yes | yes |
| `die_temp_c` | `NAN` | `NAN` | yes | yes |
| `energy_j`, `charge_c` | `NAN` | `NAN` | yes | `NAN` |

The INA237 is the INA228's 16-bit sibling: the same register addresses,
ADC_CONFIG and 15-bit SHUNT_CAL, with 16-bit results (5 µV shunt, 3.125 mV
bus, 125 m°C), an 85 V bus, and no energy or charge accumulators (SBOSA20A
Table 7-3).

### Calibration

`max_current_a` sets the current LSB, so pick the largest current you
actually expect — a value far above reality throws away resolution.

Real shunts are a few percent off their marking, which shows up directly in
the current and power readings. `cal_trim` scales the calibration register
to cancel that:

```c
cfg.cal_trim = measured_current / reported_current;   /* against a trusted meter */
```

Leave it at `0` (treated as `1.0`) if you don't need it.

### When the combination does not fit

The calibration register holds 16 bits on the INA219, 15 on the INA226,
INA228 and INA237 (bit 15 is reserved there: SBOS547C Table 7-11, SLYS021A
Table 7-7, SBOSA20A Table 7-7), and not every sensible-looking pairing of
`max_current_a` and `shunt_ohms` lands inside it. The probe works the value
out before writing anything to the chip and returns `-EINVAL` rather than
programming a wrong one, because a wrong calibration is silent — every
current and power reading comes back scaled by an arbitrary factor, with no
error anywhere.

The usual cause is too little voltage across the shunt. Measuring up to
100 mA through 0.1 Ω is only a 10 mV drop, and needs a calibration of
134218 — twice what fits. Use a larger shunt, or raise `max_current_a` to
what the hardware can really see.

### Averaging

`avg_samples` rounds down to what the part offers: 1, 2, 4, 8, 16, 32, 64 or
128 on the INA219 (SBOS448G Table 5), and 1, 4, 16, 64, 128, 256, 512 or 1024
on the others (SBOS547C Table 7-3, SLYS021A and SBOSA20A Table 7-6). 0 or 1
disables it.

### Fixed choices

These are hardcoded to keep the API small; the values are in
`src/ina2xx/ina2xx_regs.h`, each with its table:

- **INA219** — 32 V bus range, PGA /8 (±320 mV shunt), both ADCs averaging.
- **INA226** — 1.1 ms conversion times, the reset default.
- **INA228** — `ADCRANGE = 0`, so ±163.84 mV shunt at 312.5 nV/LSB;
  accumulators cleared at probe.
- **INA237** — `ADCRANGE = 0`, so ±163.84 mV shunt at 5 µV/LSB; `CONFIG`
  written back to its reset value, in case an earlier firmware changed it.
- Continuous conversion of bus and shunt (and temperature on the INA228 and
  INA237) on all four; the INA228 and INA237 at the 1052 µs default
  conversion times.
- Alert and limit registers are not exposed. An app that wants them can
  write them on the same bus after the probe, as nexus_p does for the
  INA237's `SOVL`, `BUVL` and `DIAG_ALRT`.

One departure from a reset default: the INA226's `CONFIG` bit 14 resets to 1
(SBOS547C Table 7-2) and the driver writes 0 there, as both old libraries
did. The table gives the bit no name or function.

### Provenance

| Claim | Status |
|---|---|
| Register addresses, configuration fields, address straps | **From the datasheets** in `docs/`, cited by table in `src/ina2xx/ina2xx_regs.h`: INA219 SBOS448G, INA226 SBOS547C, INA228 SLYS021A, INA237 SBOSA20A |
| LSBs, shifts, signedness, calibration equations | **From the datasheets**, cited in `src/ina2xx/ina2xx_math.h` |
| The conversions | **Verified here** against the datasheets' own worked examples, in `test/test_ina2xx_math.c` |
| The driver's I2C sequence and error paths | **Verified here.** `test/{esp,stm}/test_ina2xx.c` and `test/stm/test_ina2xx_dev.c`, against a fake chip with each part's own register widths |
| Behaviour on a physical chip | **Never checked** |
