# Architecture, driver development and tests

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Repository layout

```
include/                    public API, the only directory apps include
  powermon.h                struct powermon_dev, powermon_read(), powermon_time_us()
  powermon_log.h            powermon_err/warn/info/dbg, powermon_log_sample()
  ina2xx.h                  the INA2xx driver: probe, remove, extras, ina2xx_init()
src/                        private to the library
  powermon.c                the core: timestamp, ops.read
  powermon_log.c            powermon_log_sample()
  powermon_priv.h           bus ops, attach functions, wait; shared by all chips
  powermon_port_esp.c       that header on ESP-IDF; empty elsewhere
  powermon_port_stm.c       that header on the STM32 HAL; empty under ESP-IDF
  ina2xx/                   ina2xx.c  ina2xx_regs.h  ina2xx_math.h
test/                       host tests, `make -C test test`
  test_ina2xx_math.c        maths test: no bus and no platform API
  ina2xx_dev_cases.h        powermon API cases, shared by esp/ and stm/
  esp/                      suites on idf_stubs/, through powermon_port_esp.c
  stm/                      suites on hal_stubs/, through powermon_port_stm.c
  idf_stubs/  hal_stubs/    the slice of ESP-IDF and of the HAL the code calls
examples/
  esp/ina2xx/               ESP-IDF project
  stm/ina2xx/               STM32 snippet
docs/                       the five guides; datasheet PDFs here are gitignored
assets/                     README figure
CMakeLists.txt              ESP-IDF only; stm-forge globs src/**/*.c instead
Kconfig                     POWERMON and POWERMON_<CHIP>, for ESP-IDF and both forges' menus
idf_component.yml           ESP-IDF component manifest
```

Naming inside `src/<chip>/`, the same in every category library:

| File | Holds |
|---|---|
| `<chip>.c` | the driver: probe, remove, the `ops.read`, the chip's extras; may split into `<chip>_<part>.c` |
| `<chip>_regs.h` | register map: addresses, bit fields, values, each with its datasheet citation |
| `<chip>_proto.h` | serial protocol: command bytes, frames, checksums, for parts without registers |
| `<chip>_math.h` | pure conversions, compiled and asserted on the host by `test/<chip>_math` tests |
| `<chip>_priv.h` | driver state, only if needed |

A chip has `_regs.h` or `_proto.h`, never both; `_math.h` and `_priv.h` only
when it needs them. Everything in `src/` is private: apps see `include/` only.
A chip folder may hold a family, as `ina2xx/` does: one driver whose model
table covers parts that share a register map. Its state lives in `ina2xx.c`,
the only file that uses it, so it has no `_priv.h`.

## Platform ports

It replaces esp-ina2xx-sensor and stm-ina2xx-sensor. The driver in `src/` is
the same C on both; only the bus, the clock and the wait under it differ, in
`src/powermon_port_esp.c` (ESP-IDF `i2c_master`, `spi_master`, UART driver,
`esp_timer`, FreeRTOS) and `src/powermon_port_stm.c` (HAL). Each port is
empty on the other platform, chosen by `ESP_PLATFORM`, which ESP-IDF defines
for every component. Probes and configs take the platform's own handles,
exactly as the two old libraries did: on ESP-IDF an
`i2c_master_bus_handle_t`, with the device added at 400 kHz; on the HAL an
`I2C_HandleTypeDef *` with the clock the handle's.

The `ina2xx_init()` handle API sits on the same driver: its handle is a
`struct powermon_dev` and the driver's state in one allocation, as the
handle always was one. On ESP-IDF it maps errno back to `esp_err_t`; see
[API usage](usage.md#the-ina2xx_init-api).

## Bus abstraction

Under the chips sits a regmap-style ops table, `struct
powermon_bus_ops` in `src/powermon_priv.h`: each chip holds one `struct
powermon_bus` and talks to it only through `transfer(cmd, tx, rx)`, for
reads by way of `powermon_reg_read()`. The kind — I2C, SPI or UART — is
picked at run time by which attach the probe calls. Every powermon chip
today is on I2C; the SPI and UART kinds are there because every category
library offers all three, and `test/{esp,stm}/test_port_*` check them.

There is no `receive()`: every chip here answers a register read, none
streams, and none wants its command and its answer as two transactions.
mcu-imu and mcu-baro have one for exactly those cases; a powermon chip that
needs it would add it the same way.

`powermon_spi_attach()` takes an SPI host the app has `spi_bus_initialize()`d,
a CS GPIO, a clock and a mode on ESP-IDF, or an `SPI_HandleTypeDef *` and a
CS port and pin on the HAL. `powermon_uart_attach()` takes a `uart_port_t`
whose driver the app has installed, or a `UART_HandleTypeDef *` it has
initialised. On the HAL the SPI and UART code sits under
`HAL_SPI_MODULE_ENABLED` and `HAL_UART_MODULE_ENABLED`, so an app without
those modules builds as before.

## Adding a chip

A driver is a probe, a remove and one read function:

1. `include/foo.h`: `int foo_probe(struct powermon_dev *pm, <bus>, const struct foo_config *cfg);`
   and `void foo_remove(struct powermon_dev *pm);`, with the platform's
   handle types behind `#ifdef ESP_PLATFORM`, as `ina2xx.h` does.
2. `src/foo/foo.c`, in a folder of its own with its private headers
   (`foo_regs.h`, ...); common files and the platform ports stay at the top
   of `src/`. Include siblings and `powermon_priv.h` by plain name, never
   with `../`: siblings resolve from the chip's own folder, and
   `powermon_priv.h` through `src/`, which the ESP-IDF CMakeLists has as
   `PRIV_INCLUDE_DIRS` and stm-forge's `libs.cmake` puts on this library's
   include path while it compiles every `.c` under `src/`. Wrap the whole
   driver in `#if CONFIG_POWERMON_FOO` (never `#ifdef`); the HAL module
   check goes in `src/powermon_priv.h`. Keep a `struct powermon_bus` in your
   state and talk to the chip only through `powermon_reg_read()` or
   `bus.ops->transfer()`. Give it a
   `static const struct powermon_ops foo_ops = { .read = foo_read };`, where
   `foo_read()` fills every field but `t_us`, with `NAN` for what the chip
   does not measure. It must not touch `t_us`; `powermon_read()` does that.
3. In probe: return `-EBUSY` if `pm->ops` or `pm->priv` is already set,
   before touching anything. Refuse a bad config, check presence with
   `powermon_i2c_probe()` on I2C before allocating, allocate your state,
   `powermon_i2c_attach()` its bus, set `pm->name`, put the state into
   `pm->priv` and set `pm->ops = &foo_ops`; on failure `powermon_bus_detach()`
   and leave `pm` as it was. Remove detaches too. Wait with
   `powermon_sleep_ms()`.
4. Guard your chip-specific calls with `pm->ops == &foo_ops`.
5. Add `CONFIG_POWERMON_FOO` to the no-chip checks in `CMakeLists.txt` and
   `src/powermon_port_stm.c` and to the HAL module `#error` in
   `src/powermon_priv.h`, a `src/foo/foo.c` line to `CMakeLists.txt`, a
   `config POWERMON_FOO` bool to `Kconfig`, `# POWERMON_FOO is not set` to
   both forges' defconfigs, and `test/esp/test_foo.c` and
   `test/stm/test_foo.c` (and `test/test_foo_math.c` for its maths header)
   targets to `test/Makefile`.

## Tests

```sh
make -C test test
```

Host-only; no toolchain, HAL or ESP-IDF needed. Every test of both old
libraries runs, against its own stubs, in the layout of
[Repository layout](#repository-layout): `test/esp/` through
`src/powermon_port_esp.c` and `test/idf_stubs/`, `test/stm/` through
`src/powermon_port_stm.c` and `test/hal_stubs/`. The maths test in `test/`
touches no bus or platform API and runs once. Nothing is checked against
real silicon.

| Binary | Checks |
|---|---|
| `test_ina2xx_math` | stm-ina2xx-sensor's `test_math.c` (esp-ina2xx-sensor's plus the INA237): the register conversions against the datasheets' worked examples; sign boundaries and ignored bits over every 16-bit and 24-bit input; calibration values that do not fit, NaN and infinities |
| `stm/test_ina2xx` | stm-ina2xx-sensor's own driver test, unchanged but for the INA226 CONFIG word, which now keeps D14 set: a fake INA219/226/228/237 with each part's own register widths behind a fake HAL; the CONFIG, ADC_CONFIG and CALIBRATION words init writes, MSB-first byte order on reads and writes, 24- and 40-bit INA228 registers, the INA237's 16-bit results and 24-bit POWER against SBOSA20A Table 8-4, the 7-bit address shifted for the HAL, `-ENXIO`/`-EAGAIN`/`-ETIMEDOUT`/`-EIO` mapping, a refused config writes nothing, a failed init leaks nothing, a failed read leaves the reading untouched, `-ENOTSUP` from 219/226/237 |
| `esp/test_ina2xx` | the same cases on a fake `i2c_master`, with the `esp_err_t` answers the handle API gives on ESP-IDF (esp-ina2xx-sensor had no driver test): the device added at 400 kHz and removed by `ina2xx_del()`, presence before anything is added, `ESP_ERR_INVALID_STATE` as `ESP_FAIL`; then the powermon cases below |
| `stm/test_ina2xx_dev`, and the end of `esp/test_ina2xx` | `test/ina2xx_dev_cases.h`, the powermon API: each model's name and writes, one allocation freed by remove and the device zeroed; `powermon_read()` stamped and scaled against SBOSA20A Table 8-4, INA228 energy and charge, `NAN` where a model has nothing; a failed read leaves the sample alone; `-EBUSY` with no traffic; another driver's device refused by every extra; `-ENXIO` before allocating; refused configs before the bus; a calibration that cannot fit writes nothing; a transfer failing after the presence check leaks nothing |
| `{esp,stm}/test_core` | `powermon_read()` stamps before the read, passes the errno through and leaves the sample untouched on error; the default clock is `HAL_GetTick()` in µs; log prefix and `powermon_log_sample()` format, including 64-bit timestamps without `%lld` |
| `esp/test_port_esp`, `stm/test_port_stm` | the SPI and UART ops of each port, which no chip uses yet. SPI: attach (on ESP-IDF the device added in the mode, clock and CS asked for, a refused add leaving nothing to detach; on the HAL chip select left high); one full-duplex transaction of cmd, tx and zeros with rx taken after them; a write without a read; tx and rx together bounded; failures mapped to errno with rx untouched; on the HAL chip select high again after a failure. UART: an ESP-IDF port without an installed driver refused; stale input dropped before the send (on the HAL: overrun cleared, data register read empty); command and arguments in one send; no read for a command without an answer, none after a failed send; a short answer as `-ETIMEDOUT`; the read wait in ticks rounded up; driver and HAL failures mapped to errno; too much to send refused before the line is touched |

All build under `-std=gnu11 -Wall -Wextra -Werror -pedantic`, with
`-Wl,--wrap` (GNU ld) to count allocations and capture `printf()`.
