# Installation

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

The repository is `mcu-powermon`. The forge package key, C API and
configuration options use the name `powermon`. The clone command below
names the ESP-IDF component directory `powermon` as well.

Three step-by-step guides come first: [ESP-IDF](#esp-idf-step-by-step),
[STM32 with CMake](#with-cmake-a-cubemx-cmake-project) and
[STM32 without CMake](#without-cmake-stm32cubeide-a-makefile). None
needs a forge; if you build with esp-forge or stm-forge, see
[With forge](#with-forge). The steps use the INA226; for another model,
change the model name. The reference follows the guides.

**Arduino is not supported yet.** TODO: make the library usable from
Arduino (Arduino IDE and PlatformIO).

## ESP-IDF, step by step

**You need** ESP-IDF 5.2 or newer, with `idf.py` working in your terminal, and
an ESP-IDF project: a folder with a `CMakeLists.txt` and a `main/` folder.
`idf.py create-project my-project` makes one.

ESP-IDF calls a library a **component**: a folder with its own
`CMakeLists.txt` that the build finds and compiles. This library is the
component `powermon`.

1. **Get the library.** Pick one way.

   - **Component manager**, the simplest: the first build downloads the
     library into `managed_components/powermon`. Create
     `main/idf_component.yml`:

     ```yaml
     dependencies:
       powermon:
         git: https://github.com/JonasAtelier/mcu-powermon.git
         version: stable
     ```

   - **Clone**, if you want to edit the library or build offline. In the
     project folder:

     ```sh
     git clone https://github.com/JonasAtelier/mcu-powermon components/powermon
     ```

     Keep the folder name `powermon`: ESP-IDF names a component after its
     folder.

   ```
   my-project/
   ├── CMakeLists.txt
   ├── sdkconfig.defaults       step 2
   ├── main/
   │   ├── CMakeLists.txt       step 3
   │   ├── idf_component.yml    step 1, component manager
   │   └── my-project.c         step 4
   └── components/
       └── powermon/            step 1, clone
   ```

2. **Turn your model on.** No model is built until you do. Create
   `sdkconfig.defaults` in the project folder, a text file of settings you
   choose, with one line per model:

   ```ini
   CONFIG_POWERMON_INA226=y
   ```

   The other models are `CONFIG_POWERMON_INA219`, `_INA228` and `_INA237`.
   ESP-IDF copies these lines into `sdkconfig`, the project's full settings
   file, only when it creates that file: **if `sdkconfig` already exists,
   delete it** so your line is read. `idf.py menuconfig` → **Component
   config → Power monitor subsystem** does the same from a menu.

3. **Let your code see the library.** A new project's `main/CMakeLists.txt`
   has no `REQUIRES` and already sees every component. If yours has a
   `REQUIRES` list, add `powermon` and `driver` to it:

   ```cmake
   idf_component_register(SRCS "main.c" REQUIRES powermon driver)
   ```

4. **Write the smallest test.** Copy
   [examples/esp/ina2xx/main/main.c](../examples/esp/ina2xx/main/main.c)
   over your main source file and set `SDA_GPIO`, `SCL_GPIO`, `MODEL`,
   `SHUNT_OHMS` and `MAX_CURRENT_A` to your board. [API usage](usage.md)
   explains the calls.

5. **Build.**

   ```sh
   idf.py set-target esp32    # your chip: esp32s3, esp32c3, ...
   idf.py build
   ```

   A good build ends with `Project build complete.` Flash it as any ESP-IDF
   app and open the serial monitor. The driver itself never logs: the first
   line is a sample, starting `powermon: ina226: t`, then the time, bus
   voltage, shunt voltage, current and power.

**If it fails**

| The build prints | Why | Fix |
|---|---|---|
| `Failed to resolve component 'powermon' required by component 'main': unknown name.` | The clone is not in a folder named `powermon` | Rename it to `components/powermon` |
| `fatal error: ina2xx.h: No such file or directory` | `main/CMakeLists.txt` has a `REQUIRES` list without `powermon` | Step 3 |
| `powermon: CONFIG_POWERMON is on but no chip driver is selected` (a CMake warning) | No model turned on | Step 2 |
| ``undefined reference to `ina2xx_probe'`` | No model turned on, often because `sdkconfig` existed before you edited `sdkconfig.defaults` | Delete `sdkconfig`, build again |

## STM32, step by step

**You need** a project for an STM32 (F4, F7 or G0, for instance) that uses
ST's **HAL**, the vendor library behind `HAL_Init()`, `HAL_I2C_Init()` and
friends, with the I2C your INA is wired to already set up. STM32CubeMX makes
such a project; with **Toolchain / IDE: CMake** it makes a CMake one, the
main case below.

Two terms. A **HAL module** is the part of the HAL for one peripheral: I2C,
SPI, UART. It is compiled only when it is switched on in **hal_conf**, the
file `Core/Inc/stm32f4xx_hal_conf.h` (`stm32f7xx_hal_conf.h` on an F7,
`stm32g0xx_hal_conf.h` on a G0). CubeMX switches a module on when you enable
that peripheral.

### With CMake (a CubeMX CMake project)

1. **Get the library.** In the project folder, next to the top-level
   `CMakeLists.txt`:

   ```sh
   git clone https://github.com/JonasAtelier/mcu-powermon
   ```

   ```
   my-board/
   ├── CMakeLists.txt                  step 2
   ├── cmake/stm32cubemx/              made by CubeMX
   ├── Core/
   │   ├── Inc/stm32f4xx_hal_conf.h    hal_conf, step 3
   │   └── Src/main.c                  step 4
   ├── Drivers/                        the HAL, copied in by CubeMX
   └── mcu-powermon/                   step 1
   ```

2. **Add it to the build.** In the top `CMakeLists.txt`, after the line
   `add_subdirectory(cmake/stm32cubemx)`, add:

   ```cmake
   set(CONFIG_POWERMON_INA226 ON)      # your model; one line per model
   set(MCU_HAL_TARGET stm32cubemx)
   set(MCU_DEVICE_HEADER stm32f4xx.h)  # stm32f7xx.h, stm32g0xx.h, ...
   add_subdirectory(mcu-powermon)
   ```

   and add `powermon` to the link line CubeMX already wrote, so it reads:

   ```cmake
   target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx powermon)
   ```

   Do not add `PRIVATE`: CubeMX's line has no such keyword, and CMake refuses
   to mix the two forms on one target.

   `stm32cubemx` is a CMake *target*, a named bundle of settings, that CubeMX
   makes to carry the HAL's include folders and defines; `MCU_HAL_TARGET`
   lends those to the library, which works with any CubeMX version.
   `MCU_DEVICE_HEADER` is your chip family's main header.

3. **Check the HAL module.** The INA226 talks I2C, so hal_conf must have
   `#define HAL_I2C_MODULE_ENABLED`; enabling I2C1 (or any I2C) in CubeMX
   does that. The [HAL module table](#stm32-hal) says the same for every
   model.

4. **Write the smallest test.** It is
   [examples/stm/ina2xx/main.c](../examples/stm/ina2xx/main.c) fitted into
   CubeMX's `Core/Src/main.c`. Paste each part between the `USER CODE BEGIN`
   and `END` comments named, so a regenerate keeps it. The example's
   `i2c1_init()` is left out: CubeMX's `MX_I2C1_Init()` already does it.

   After `/* USER CODE BEGIN Includes */`:

   ```c
   #include "ina2xx.h"
   #include "powermon.h"
   #include "powermon_log.h"
   ```

   After `/* USER CODE BEGIN 2 */`, which comes after `MX_I2C1_Init()`:

   ```c
     static struct powermon_dev pm; /* must start zeroed */
     const ina2xx_config_t cfg = {
       .model = INA226, /* INA219, INA226, INA228 or INA237 */
       .shunt_ohms = 0.1f,
       .max_current_a = 3.2f,
       .avg_samples = 16,
     };

     if (ina2xx_probe(&pm, &hi2c1, &cfg))
       Error_Handler(); /* the driver logs nothing on failure */
   ```

   After `/* USER CODE BEGIN 3 */`, inside the `while (1)` loop:

   ```c
       struct powermon_sample sample;

       if (powermon_read(&pm, &sample) == 0)
         powermon_log_sample(&pm, &sample);
       HAL_Delay(1000);
   ```

   The lines go out through `printf()`: send that to a UART by writing
   newlib's `_write()`, and add this to `CMakeLists.txt` so numbers are
   printed:

   ```cmake
   target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u _printf_float)
   ```

   [API usage](usage.md) explains the calls.

5. **Build** as before, for example `cmake --build` with your build folder.
   A good build shows a `Linking C executable` line and no error. On the
   UART, the first line is a sample, starting `powermon: ina226: t`.

**If it fails**

| You see | Why | Fix |
|---|---|---|
| `powermon: no chip driver is selected; set(CONFIG_POWERMON_<CHIP> ON) before add_subdirectory()` (a CMake warning), then ``undefined reference to `ina2xx_probe'`` | No model turned on, or turned on after `add_subdirectory(mcu-powermon)` | Step 2 |
| `#error "the powermon chip drivers need the HAL I2C module: ...` | The HAL module is off | Enable the peripheral in CubeMX and regenerate |
| `#error "powermon: CMSIS_device_header is not defined; ...` | `MCU_DEVICE_HEADER` is missing | Step 2 |
| `fatal error: stm32f4xx.h: No such file or directory` | `MCU_HAL_TARGET` is missing | Step 2 |
| `Target "stm32cubmx" not found.` (a CMake error) | `MCU_HAL_TARGET` is misspelt | Step 2 |
| `The plain signature for target_link_libraries has already been used with the target` | `PRIVATE` added to the link line | Step 2 |
| No error, but the numbers print as blanks | The C library (newlib-nano) leaves out `%f` | The `-u _printf_float` line of step 4 |

### Without CMake (STM32CubeIDE, a Makefile)

1. Copy `include/` and `src/` of this repository into your project, for
   example to `Middlewares/mcu-powermon/`, keeping the chip folder inside
   `src/`.
2. Add every `.c` file under `src/` to the build, chip folder included.
3. Add two include paths, `Middlewares/mcu-powermon/include` and
   `Middlewares/mcu-powermon/src`, and two defines:
   `CMSIS_device_header="stm32f4xx.h"` (or your family's), quotes included,
   and `CONFIG_POWERMON_INA226=1`, one per model. CubeMX already defines
   `USE_HAL_DRIVER` and your device, such as `STM32F405xx`.
4. Steps 3 to 5 of the CMake guide apply as they are; `-u _printf_float`
   goes in the linker flags.

Where these settings usually live: in STM32CubeIDE, **Project → Properties →
C/C++ Build → Settings → Tool Settings → MCU GCC Compiler** (Include paths,
Preprocessor); in a CubeMX Makefile, `C_SOURCES`, `C_INCLUDES` and `C_DEFS`.
The build errors are the ones in the table above; the fix is the matching
define or include path instead of a CMake line. One more:
`#include expects "FILENAME" or <FILENAME>` means `CMSIS_device_header` lost
its quotes.

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

Setup: [ESP-IDF, step by step](#esp-idf-step-by-step). For reference:

- In `main/idf_component.yml`, `version` is a git ref; `stable` is the
  released one.
- A clone in `components/` is a local component, named after its folder;
  that name is what other components `REQUIRES`. The example in
  `examples/esp/` needs no name: its `main` lists no `REQUIRES`, and ESP-IDF
  gives `main` every component, so it builds from any checkout name.
- With the subsystem on and no model picked, the build warns.
- ESP-IDF 5.2 or newer is required for the `i2c_master` API.

## STM32 HAL

Setup: [With CMake](#with-cmake-a-cubemx-cmake-project) or
[Without CMake](#without-cmake-stm32cubeide-a-makefile). The
`add_subdirectory()` route builds a static library `powermon`. For
reference:

- Each `CONFIG_POWERMON_<CHIP>` (and `CONFIG_POWERMON_DEBUG`) that is true
  reaches the compiler as `CONFIG_POWERMON_<CHIP>=1`, for the library and
  your app.
- `MCU_HAL_TARGET` is a target that carries your `USE_HAL_DRIVER`, device
  define (`STM32F405xx`, ...) and the include dirs of the HAL, CMSIS and your
  `stm32<family>xx_hal_conf.h`. The library copies only those include dirs
  and defines, never the target's sources, so this works with any CubeMX
  version. `MCU_DEVICE_HEADER` becomes `CMSIS_device_header`, which CubeMX
  does not define. Every `mcu-*` library reads these two, so set them once.
  Leave them unset if your project already provides both.
- The CPU flags (`-mcpu`, `-mfloat-abi`, ...) must reach the library too;
  CubeMX's toolchain file sets them for every target.
- Without CMake, `src/` only needs to be on the include path of this
  library's own sources; the defines are what stm-forge passes.

**No chip driver is built by default.** Like a kernel subsystem, the core is
always built and each model is opt-in: a model whose option is absent or 0
compiles to nothing, and probing it is `-EINVAL`. With the library on and no
model picked, the CMakeLists warns (ESP-IDF and `add_subdirectory()`) and
`src/powermon_port_stm.c` emits a `#warning`. Leaving models out saves
flash: on nexus_p's STM32G030, INA237 alone is about 2.2 KB smaller than
stm-ina2xx-sensor's all-four driver.

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
to your app's `CMakeLists.txt` (in a CubeMX project the target is
`${CMAKE_PROJECT_NAME}`), or `powermon_log_sample()` prints blanks where the
numbers go.
