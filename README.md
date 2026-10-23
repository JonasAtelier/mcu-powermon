<p align="center">
  <img src="assets/module.jpg"
       alt="An Adafruit INA219 current sensor breakout"
       width="520">
  <br><sub>Adafruit INA219 breakout &mdash; photo <a href="https://www.adafruit.com/product/904">Adafruit Industries</a></sub>
</p>

# mcu-powermon

<p align="center">
  <img alt="platform: ESP-IDF | STM32 HAL" src="https://img.shields.io/badge/platform-ESP--IDF%20%7C%20STM32%20HAL-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="language: C" src="https://img.shields.io/badge/language-C-70e1b2?style=flat-square&labelColor=0d2238">
  <img alt="license: MIT" src="https://img.shields.io/badge/license-MIT-f2bd68?style=flat-square&labelColor=0d2238">
</p>

A small C power monitor library for **ESP-IDF 5.2+** and **STM32 HAL**. One
driver for the TI INA219, INA226, INA228 and INA237 gives timestamped
samples of bus and shunt voltage in V, current in A, power in W, and die
temperature, energy and charge where the chip has them. This library
replaces `esp-ina2xx-sensor` and `stm-ina2xx-sensor`, and keeps their
`ina2xx_init()` API working unchanged.

The repository is named `mcu-powermon`; the C API, `CONFIG_POWERMON_*`
options and forge package key are `powermon`. The clone command below names
the ESP-IDF component directory `powermon` as well.

> **Hardware unverified.** Host tests use simulated devices and platform stubs.
> Nothing here has run on a physical chip.

[Supported hardware](#supported-hardware) · [Drivers](#drivers) ·
[Install](#install) ·
[Quick start](#quick-start) · [Examples](#examples) ·
[Documentation](#documentation) · [Tests](#tests)

## Supported hardware

All four models support both platforms. Enable only the ones you need.

| Chip | Bus | Notes |
|---|---|---|
| **INA219** | I2C | TI current and power monitor, 26 V bus |
| **INA226** | I2C | TI current and power monitor, 36 V bus |
| **INA228** | I2C | TI current and power monitor, 85 V bus |
| **INA237** | I2C | TI current and power monitor, 85 V bus |

Every part answers at `0x40` with A0 and A1 grounded; sixteen addresses in
all. Wiring is in the [wiring guide](docs/hardware.md).

## Drivers

<details><summary><b>INA219</b> — I2C</summary>

- Measures: bus and shunt voltage, current, power; 26 V bus
- Examples: ESP-IDF [ina2xx](examples/esp/ina2xx/) · STM32 HAL
  [ina2xx](examples/stm/ina2xx/main.c)
- Chip reference: [INA2xx](docs/chips.md#ina2xx)

</details>

<details><summary><b>INA226</b> — I2C</summary>

- Measures: the same as the INA219 (bus and shunt voltage, current, power),
  16-bit, 36 V bus
- Examples: ESP-IDF [ina2xx](examples/esp/ina2xx/) · STM32 HAL
  [ina2xx](examples/stm/ina2xx/main.c)
- Chip reference: [INA2xx](docs/chips.md#ina2xx)

</details>

<details><summary><b>INA228</b> — I2C</summary>

- Measures: the same as the INA219 (bus and shunt voltage, current, power),
  20-bit, 85 V bus, plus die temperature and energy and charge accumulators
- Examples: ESP-IDF [ina2xx](examples/esp/ina2xx/) · STM32 HAL
  [ina2xx](examples/stm/ina2xx/main.c)
- Chip reference: [INA2xx](docs/chips.md#ina2xx)

</details>

<details><summary><b>INA237</b> — I2C</summary>

- Measures: the same as the INA219 (bus and shunt voltage, current, power),
  16-bit, 85 V bus, plus die temperature
- Examples: ESP-IDF [ina2xx](examples/esp/ina2xx/) · STM32 HAL
  [ina2xx](examples/stm/ina2xx/main.c)
- Chip reference: [INA2xx](docs/chips.md#ina2xx)

</details>

## Install

### ESP-IDF

Clone into your project's components directory:

```sh
git clone https://github.com/JonasAtelier/mcu-powermon components/powermon
```

Enable your chips in `idf.py menuconfig` → **Component config → Power
monitor subsystem**, or add this to `sdkconfig.defaults` for the INA226:

```ini
CONFIG_POWERMON=y
CONFIG_POWERMON_INA226=y
```

The `main` component sees the library automatically. Other components declare
`REQUIRES powermon driver` in `idf_component_register()`.

### esp-forge / stm-forge

stm-forge must be at `ee921c3` or later, which builds the chip folders in
`src/`; older ones fail at link with undefined chip probes.

Add to `profiles/pkgversion`:

```make
powermon_GIT=https://github.com/JonasAtelier/mcu-powermon@stable
```

Enable the package and your chip in `configs/<target>_defconfig`:

```make
PKG_powermon        = y
POWERMON_INA226     = y
```

A target that used `PKG_ina2xx = y` swaps that line for these two; its code
builds unchanged. See [installation](docs/installation.md#from-ina2xx).

### STM32 HAL without forge

Compile every `.c` under `src/`, chip folders included, add `include/` and
`src/` to the include path, and define
`CMSIS_device_header` as your family header (`"stm32f4xx.h"`,
`"stm32f7xx.h"`, `"stm32g0xx.h"`, ...), `USE_HAL_DRIVER`, and
`CONFIG_POWERMON_<CHIP>=1` for each model you use. Enable and compile the
HAL's I2C module.

**No chip driver is enabled by default.** See the
[installation guide](docs/installation.md) for all chip options, the HAL
module requirement and float logging.

## Quick start

Initialise the bus in your application, then probe the sensor and read it.
This STM32 helper assumes the passed I2C handle is already initialised. The
part converts continuously, so a read never waits.

```c
#include "ina2xx.h"
#include "powermon.h"
#include "powermon_log.h"

int read_once(I2C_HandleTypeDef *bus)
{
    static const ina2xx_config_t cfg = {
        .model         = INA226,
        .shunt_ohms    = 0.1f,
        .max_current_a = 3.2f,
    };
    struct powermon_dev pm = { 0 }; /* Must start zeroed. */
    struct powermon_sample sample;
    int ret = ina2xx_probe(&pm, bus, &cfg); /* 0x40 */
    if (ret)
        return ret;

    ret = powermon_read(&pm, &sample);
    if (!ret)
        powermon_log_sample(&pm, &sample);

    ina2xx_remove(&pm);
    return ret;
}
```

Call `read_once(&hi2c1)` from thread context. On ESP-IDF, pass an
`i2c_master_bus_handle_t` instead; the
[example](examples/esp/ina2xx/main/main.c) shows the bus setup. For
continuous sampling, probe once, read in your loop, and remove when
finished.

- `powermon_read()` returns volts, amperes, watts, °C, joules and coulombs,
  plus `t_us`; a quantity the chip does not measure is `NAN`.
- Integer-returning API calls return **0 or a negative errno**. Use a sample
  only on success; errors leave it unchanged.
- `max_current_a` sets the resolution, and `cal_trim` cancels shunt
  tolerance; see [calibration](docs/chips.md#calibration).

See [API usage](docs/usage.md) for device lifetime, bus handles, timestamps,
the `ina2xx_init()` API, logging and error codes.

## Examples

Each driver's examples are listed under [Drivers](#drivers).

Both log a sample once a second; set `MODEL` and turn its option on. ESP-IDF
examples already locate this library through `EXTRA_COMPONENT_DIRS`. With
ESP-IDF activated, build from the example directory:

```sh
cd examples/esp/ina2xx
idf.py set-target esp32 # Replace with your board's target.
idf.py build
```

STM32 examples are snippets
to integrate into your application, with clock setup and logging retargeting
provided by you. Check the example's pin definitions against your board and
read the [wiring guide](docs/hardware.md).

## Scope and limitations

The library provides a shared sample format and a synchronous driver.
Your application owns bus setup, scheduling, and locking.

- **I2C only**, which is all these parts have.
- **Blocking bus calls:** no interrupts or DMA. Probe waits 10 ms for the
  first conversion; reads never wait.
- **Fixed choices:** the INA219 at 32 V and PGA /8, the INA228 and INA237 at
  `ADCRANGE = 0`, continuous conversion with the default conversion times.
  Alert and limit registers are not exposed.
- **Concurrency:** use one thread per device and bus handle, or supply a mutex.
  The HAL's own lock answers a clash with `HAL_BUSY`, which comes back as
  `-EAGAIN`.
- **No bus setup:** pins, clocks and speed are the app's.
- **STM32 timestamps:** the default clock has millisecond resolution and wraps
  after 49.7 days. Override `powermon_time_us()` if needed.

## Documentation

| Guide | Contents |
|---|---|
| [Installation](docs/installation.md) | Forge, ESP-IDF, STM32 HAL, build options, logging setup, moving from `ina2xx` |
| [Hardware](docs/hardware.md) | Wiring, pins and addresses |
| [API usage](docs/usage.md) | Lifecycle, bus handles, timing, the `ina2xx_init()` API and errors |
| [Chip reference](docs/chips.md) | What each model reports, calibration, fixed choices and provenance |
| [Development](docs/development.md) | Platform ports, bus abstraction, adding a driver and test coverage |

Public interfaces live in [include/](include/); the driver and platform ports
live in [src/](src/). The [repository layout](docs/development.md#repository-layout)
maps every directory and the file-naming rule for chip folders.

## Tests

```sh
make -C test test
```

Host-only tests use stubbed HAL and ESP-IDF APIs; no embedded toolchain or
physical sensor is needed. See the [test reference](docs/development.md#tests)
for compiler requirements and coverage.

## License

[MIT](LICENSE)
