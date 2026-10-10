# API usage

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Device lifecycle

Initialise the bus, probe once, then read. The
[README quick start](../README.md#quick-start) shows a complete STM32
helper with cleanup. The [ESP-IDF example](../examples/esp/ina2xx/main/main.c)
shows the corresponding bus setup.

One `powermon_log_sample()` line looks like this (values illustrative; `nan`
where the chip has no such quantity, here an INA226):

```
powermon: ina226: t 1234000 us   12.500 V   0.012500 V    1.2500 A    15.625 W     nan C         nan J        nan As
```

On ESP-IDF the same line carries the log's own prefix, as
`I (1234) powermon: ina226: ...`.

The sample is SI throughout: `bus_v` and `shunt_v` in volts, `current_a` in
amperes, `power_w` in watts, `die_temp_c` in degrees Celsius, `energy_j` in
joules and `charge_c` in coulombs since the last
`ina2xx_reset_energy()`, and `t_us` in microseconds. Current follows the
chip's sign: positive from IN+ to IN−. Which quantities each model has is in
the [chip reference](chips.md#what-each-model-reports). Tear down with
`ina2xx_remove(&pm)`; the bus is left alone.

**Bus handles on ESP-IDF.** The probe takes an `i2c_master_bus_handle_t`
the app has created, and the driver adds its own device at 400 kHz;
`ina2xx_remove()` takes it off again.

**Bus handles on the HAL.** The probe takes an `I2C_HandleTypeDef *` the app
has `HAL_I2C_Init()`ed, plus a **7-bit** address in the config (`0x40`, not
the HAL's shifted `0x80`; the driver shifts it). Bus speed is the handle's
business: 400 kHz or less suits all four.

**A `struct powermon_dev` must start zeroed** — static storage or `= { 0 }`.
Probe on one that is already bound (non-NULL `ops` or `priv`) returns
`-EBUSY` and touches nothing, rather than leak the first binding.
`ina2xx_remove()` and a failed probe leave it zeroed, so it can be probed
afresh.

**Timestamps.** `s.t_us` comes from `powermon_time_us()`, taken just before
the bus transfers. On ESP-IDF that is `esp_timer_get_time()`. On the HAL the
library's default is weak and only `HAL_GetTick() * 1000`:
whole milliseconds, wrapping after 49.7 days. **Define your own
`int64_t powermon_time_us(void)` for real microseconds** and it replaces the
default at link time — the `DWT->CYCCNT` cycle counter on a Cortex-M3 and up
(enable `CoreDebug->DEMCR` TRCENA and `DWT->CTRL` CYCCNTENA, then divide by
the core clock in MHz and extend the 32-bit count to 64; an F7 may also need
`DWT->LAR` unlocked first), or a free-running timer at 1 MHz.

**Delays.** Probe waits 10 ms for the first conversion to land, so the first
read is not zero. On ESP-IDF that is `vTaskDelay()`, rounded up to whole
ticks and plus one, so never shorter than asked: 10–20 ms at the default
100 Hz tick. On the HAL it is `HAL_Delay()`, which busy-waits on SysTick:
probe from thread context, never from an interrupt at or above SysTick's
priority. If your app replaces the weak `HAL_Delay()` (an RTOS often does),
the "never short" guarantee is that replacement's. Reads do not wait; in
continuous mode the registers always hold the latest result.

**Logging.** The driver never logs. On ESP-IDF the `powermon_*` macros go
under the log tag `powermon`, prefixed with the chip name, and
`powermon_dbg()` is `ESP_LOGD`. On the HAL every line goes through
`printf()` as `powermon: <chip>: ...`. Your own code can use the same
`powermon_err()`, `powermon_warn()`, `powermon_info()` and `powermon_dbg()`
from `powermon_log.h`. On the HAL `powermon_dbg()` is compiled out unless
`CONFIG_POWERMON_DEBUG` is 1 (stm-forge: `POWERMON_DEBUG = y`).

**Chip extras** take the same `struct powermon_dev *` and return `-EINVAL`
if another driver probed it: `ina2xx_reset_energy()` zeroes the INA228's
energy and charge accumulators, and is `-ENOTSUP` on the other models.

## The ina2xx_init() API

The API of esp-ina2xx-sensor and stm-ina2xx-sensor is still here, on top of
the same driver, and code written for either builds unchanged: `ina2xx.h`,
`ina2xx_init()`, `ina2xx_read()`, `ina2xx_reset_accumulators()`,
`ina2xx_del()`, `ina2xx_config_t`, `ina2xx_reading_t`. Only the model's
`CONFIG_POWERMON_INA2xx` option has to be on; see
[installation](installation.md#from-ina2xx).

```c
ina2xx_handle_t ina;
int err = ina2xx_init(&hi2c1, &cfg, &ina);  /* ESP-IDF: esp_err_t, a bus handle */
ina2xx_reading_t r;
if (!err && !ina2xx_read(ina, &r))
    printf("%.3f V  %.1f mA\n", (double)r.bus_v, (double)(r.current_a * 1e3f));
```

| Function | Does |
|---|---|
| `ina2xx_init(bus, cfg, &h)` | Checks the chip answers, calibrates it, starts continuous conversion |
| `ina2xx_read(h, &reading)` | Reads everything the part supports in one call; `ina2xx_reading_t` has the sample's fields but `t_us` |
| `ina2xx_reset_accumulators(h)` | Zeroes the INA228 energy/charge counters |
| `ina2xx_del(h)` | Frees the handle; on ESP-IDF removes the device from the bus |

On the HAL it returns 0 or a negative errno, exactly as stm-ina2xx-sensor
did. On ESP-IDF it returns an `esp_err_t`, as esp-ina2xx-sensor did, with
three changes:

- **Presence is checked first.** `ina2xx_init()` asks `i2c_master_probe()`
  before adding the device or writing anything, so nothing at the address is
  `ESP_ERR_NOT_FOUND` with nothing allocated. esp-ina2xx-sensor added the
  device and failed at the first write.
- **Bus errors pass through errno.** The bus layer maps IDF's codes to errno
  and the handle API maps them back: `ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_ARG`,
  `ESP_ERR_NOT_FOUND`, `ESP_ERR_INVALID_SIZE`, `ESP_ERR_NO_MEM` and
  `ESP_ERR_NOT_SUPPORTED` come back as themselves, and any other code —
  `ESP_ERR_INVALID_STATE`, which IDF 5.5 gives for a NACK in the middle of a
  transfer, included — as `ESP_FAIL`.
- **A failed read leaves the reading alone.** esp-ina2xx-sensor cleared
  `*out` before reading, so a failure left it half-written; it is written
  only on success now, as on the HAL. `ina2xx_del()` returns `ESP_OK`
  whatever `i2c_master_bus_rm_device()` says.

## Errors

| errno | from | means |
|---|---|---|
| `-EINVAL` | any | NULL argument; `shunt_ohms` or `max_current_a` not positive; `addr` above 0x7F; `model` not one of the four, or not built; a calibration that does not fit (see [chip reference](chips.md#when-the-combination-does-not-fit)); a `powermon_dev` another driver probed |
| `-EBUSY` | probe | that `powermon_dev` is already bound. Nothing was touched |
| `-ENXIO` | probe | nothing ACKed at that address, checked with `i2c_master_probe()` or `HAL_I2C_IsDeviceReady()` before anything is allocated or written |
| `-EAGAIN` | any | HAL only: the HAL answered `HAL_BUSY` — the bus or the handle is in use, or the handle was never `HAL_I2C_Init()`ed. On the F4, also a bus with no pull-ups or SDA stuck low during the probe's presence check — see below |
| `-ETIMEDOUT` | any | a bus transfer timed out (`ESP_ERR_TIMEOUT`, `HAL_TIMEOUT`) |
| `-EIO` | any | any other bus failure, a NACK in the middle of a transfer included |
| `-ENOTSUP` | reset | the part is an INA219, INA226 or INA237, which have no accumulators |
| `-ENOMEM` | probe | out of heap |

On ESP-IDF bus failures map from `esp_err_t`: `ESP_ERR_TIMEOUT` is
`-ETIMEDOUT`, `ESP_ERR_INVALID_ARG` `-EINVAL`, `ESP_ERR_INVALID_SIZE`
`-EMSGSIZE`, `ESP_ERR_NO_MEM` `-ENOMEM` and the rest, a mid-transfer NACK
included, `-EIO`. On the HAL, a chip that NACKs its address is `HAL_ERROR`
from `HAL_I2C_IsDeviceReady()`, `-ENXIO`, on the F4 and the F7. On the F4, a
bus with no pull-ups or SDA stuck low sets the peripheral's BUSY flag, so
the probe gives up after 25 ms with `-EAGAIN`; `-ETIMEDOUT` from the
presence check means a START never completed. Check the wiring for either.
`-EAGAIN` is never `-EBUSY`, which a probe keeps for "already bound".

`strerror(-ret)` names any of them. **Do not use a sample when
`powermon_read()` returns an error**: it writes `*s` only on success, so the
old values are still there. A failed probe leaves the `powermon_dev` alone
and frees whatever it allocated.
