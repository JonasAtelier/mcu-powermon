# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-powermon`. The forge package key, C API and
configuration options use the name `powermon`. The clone command below
names the ESP-IDF component directory `powermon` as well.

## With forge

stm-forge must be at `ee921c3` or later ("build: libraries may keep sources
in src/ subfolders"); older ones only compile `src/*.c`, so the driver in
`src/ina2xx/` is never built and the link fails with undefined references
to the chip probes. esp-forge is unaffected: the component's CMakeLists
lists its sources.

With [esp-forge](https://github.com/JonasAtelier/esp-forge) or
[stm-forge](https://github.com/JonasAtelier/stm-forge). The catalog line in
`profiles/pkgversion` is the same for both:

```make
powermon_GIT=https://github.com/JonasAtelier/mcu-powermon@stable
```

and a target turns the library and its chips on in `configs/<target>_defconfig`:

```make
PKG_powermon        = y
POWERMON_INA226     = y
# POWERMON_INA219 is not set
# POWERMON_INA228 is not set
# POWERMON_INA237 is not set
```

or with `make menu`. esp-forge turns each line into `CONFIG_POWERMON_<CHIP>=y`
in `sdkconfig.defaults`; stm-forge passes it as
`-DCONFIG_POWERMON_<CHIP>=1`. `POWERMON_DEBUG = y` turns on `powermon_dbg()`
lines on the HAL; on ESP-IDF the log level decides.

### From ina2xx

The old libraries were the forge key `ina2xx`, one source file with no chip
options. The key is `powermon` now, because the forges pass a package's own
`<KEY>_*` lines as `CONFIG_` defines, and each model is opt-in. A target
moves over with two defconfig lines and no code change:

```make
# was: PKG_ina2xx = y
PKG_powermon        = y
POWERMON_INA237     = y     # every model the target's code uses
```

and the catalog line `ina2xx_GIT=...` in `profiles/pkgversion` becomes the
`powermon_GIT=` line above. `ina2xx.h`, `ina2xx_init()` and the rest of the
handle API are still there; see [API usage](usage.md#the-ina2xx_init-api).

## ESP-IDF

Clone into your project's `components/` directory. A local
component is named after its directory, and that name is what you
`REQUIRES`:

```sh
git clone https://github.com/JonasAtelier/mcu-powermon components/powermon
```

```cmake
idf_component_register(SRCS "main.c" REQUIRES powermon driver)
```

Enable the chips under `idf.py menuconfig` → **Component config → Power
monitor subsystem**, or put `CONFIG_POWERMON_<CHIP>=y` in your
`sdkconfig.defaults` as the example in `examples/esp/` does. With the
subsystem on and no chip picked, the build warns. ESP-IDF 5.2 or newer is
required for the `i2c_master` API.

## STM32 HAL

Copy `include/` and `src/` into an STM32 project, compile every `.c` under
`src/`, chip folders included, add `include/` and `src/` to the include path
(`src/` only needs to reach this library's own sources), and define what the
forge would: `CMSIS_device_header` as your family header (`"stm32f4xx.h"`,
`"stm32f7xx.h"`, `"stm32g0xx.h"`, ...), `USE_HAL_DRIVER`, and
`CONFIG_POWERMON_<CHIP>=1` for each model you use.

**No chip driver is built by default.** Like a kernel subsystem, the core is
always built and each model is opt-in: a model whose option is absent or 0
compiles to nothing, and probing it is `-EINVAL`. With the library on and no
model picked, the ESP-IDF CMakeLists warns and `src/powermon_port_stm.c`
emits a `#warning`. Leaving models out saves flash: on nexus_p's
STM32G030, INA237 alone is about 2.2 KB smaller than stm-ina2xx-sensor's
all-four driver.

**The HAL module is yours to enable.** The driver calls the HAL's I2C, so
your app's `stm32<family>xx_hal_conf.h` must define it, and its `.c` must be
in the build:

| Option | Needs in hal_conf | HAL source |
|---|---|---|
| `POWERMON_INA219`, `POWERMON_INA226`, `POWERMON_INA228`, `POWERMON_INA237` | `HAL_I2C_MODULE_ENABLED` | `stm32<family>xx_hal_i2c.c` |

The bus layer under the driver also has SPI and UART code, which no chip
uses yet; it sits under `HAL_SPI_MODULE_ENABLED` and
`HAL_UART_MODULE_ENABLED`, so an app without those modules builds as it is,
and an app with them links none of it unless it is called. A model whose
module is off stops the build with an `#error` that says which define is
missing.

**No libm needed.** The driver calls no maths-library function (`NAN` is a
macro), so it links with or without `-lm`.

**printf() is the log.** The driver itself never logs. `powermon_log.h`'s
macros and `powermon_log_sample()` go out through `printf()`, so retarget
newlib's `_write()` to a UART. stm-forge links newlib-nano
(`--specs=nano.specs`), whose `printf()` drops `%f` unless the app links with
`-u _printf_float`: add `target_link_options(firmware PRIVATE -u _printf_float)`
to your app's `CMakeLists.txt`, or `powermon_log_sample()` prints blanks
where the numbers go.
