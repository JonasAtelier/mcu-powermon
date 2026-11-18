# Read by ESP-IDF (and so esp-forge) as a component, and by any other CMake
# project that add_subdirectory()s this repo, such as a CubeMX-generated STM32
# one. stm-forge never reads it: it compiles every .c under src/, chip folders
# included, puts src/ on the include path of this library's sources and adds
# include/ -- which is why each port file is empty on the other platform.
# ESP_PLATFORM tells the two apart; IDF sets it in its early requirements pass
# as well as in the build.

if(NOT ESP_PLATFORM)
    # A static library `powermon`. Every .c under src/, as stm-forge does: a
    # chip file compiles to nothing unless its CONFIG_POWERMON_<CHIP> is 1, so
    # there is no source list here to keep in step with the IDF one below.
    file(GLOB_RECURSE powermon_sources CONFIGURE_DEPENDS src/*.c)
    add_library(powermon STATIC ${powermon_sources})
    # include/ for the app; src/ only for this library's own sources, so a
    # chip folder reaches powermon_priv.h by plain name and no private header
    # is ever on the app's include path.
    target_include_directories(powermon PUBLIC include PRIVATE src)

    # The options are the Kconfig bools, read from Kconfig so a new chip needs
    # no line here: a true CMake variable CONFIG_POWERMON_<CHIP> (or
    # CONFIG_POWERMON_DEBUG) becomes CONFIG_POWERMON_<CHIP>=1, what stm-forge
    # passes. Only bools: an int option cannot be 1-or-absent. Each `config`
    # line in Kconfig is followed directly by its type line, which is what the
    # match relies on. PUBLIC, because powermon_log.h reads
    # CONFIG_POWERMON_DEBUG in the app's sources.
    file(READ Kconfig powermon_kconfig)
    string(REGEX MATCHALL "config POWERMON_[A-Z0-9_]+[ \t\r\n]+bool"
           powermon_options "${powermon_kconfig}")
    list(TRANSFORM powermon_options
         REPLACE "config (POWERMON_[A-Z0-9_]+)[ \t\r\n]+bool" "CONFIG_\\1")
    set(powermon_chips_on "")
    foreach(option IN LISTS powermon_options)
        if(${option})
            target_compile_definitions(powermon PUBLIC ${option}=1)
            if(NOT option STREQUAL "CONFIG_POWERMON_DEBUG")
                list(APPEND powermon_chips_on ${option})
            endif()
        endif()
    endforeach()
    if(NOT powermon_chips_on)
        message(WARNING "powermon: no chip driver is selected; "
                        "set(CONFIG_POWERMON_<CHIP> ON) before add_subdirectory()")
    endif()

    # The HAL is the app's. MCU_HAL_TARGET names a target carrying its include
    # dirs and defines (USE_HAL_DRIVER, the device, the hal_conf.h directory)
    # -- stm32cubemx in a CubeMX-generated project; MCU_DEVICE_HEADER is the
    # family header the public headers include as CMSIS_device_header, which
    # CubeMX does not define. Both are shared by every mcu-* library, so an
    # app sets them once. Left unset, the app provides them its own way.
    #
    # Only the target's include dirs and defines are copied, the target is
    # never linked: CubeMX up to 6.13 also hangs main.c, the HAL and the
    # startup file on stm32cubemx as INTERFACE sources, which linking would
    # compile into libpowermon.a. A misspelt name fails at generate time.
    if(MCU_HAL_TARGET)
        target_include_directories(powermon PRIVATE
            $<TARGET_PROPERTY:${MCU_HAL_TARGET},INTERFACE_INCLUDE_DIRECTORIES>)
        target_compile_definitions(powermon PRIVATE
            $<TARGET_PROPERTY:${MCU_HAL_TARGET},INTERFACE_COMPILE_DEFINITIONS>)
    endif()
    if(MCU_DEVICE_HEADER)
        target_compile_definitions(powermon PUBLIC
            "CMSIS_device_header=\"${MCU_DEVICE_HEADER}\"")
    endif()
    return()
endif()

# ESP-IDF: the chip files are listed one by one, and PRIV_INCLUDE_DIRS "src"
# lets a chip folder reach powermon_priv.h by plain name.
#
# The core is always built, even with CONFIG_POWERMON off; the INA2xx driver
# only when one of its models is on, and inside it each model only under its
# own option. Kconfig values are available here during the build phase, and
# only SRCS depends on them -- never REQUIRES, which IDF resolves before
# sdkconfig. That is also why POWERMON off still builds the core: an empty
# SRCS would need a second, differently-shaped register call, and an
# unreferenced core costs nothing once --gc-sections has run.
set(srcs "src/powermon.c" "src/powermon_log.c" "src/powermon_port_esp.c")

if(CONFIG_POWERMON AND NOT (CONFIG_POWERMON_INA219 OR CONFIG_POWERMON_INA226 OR
                            CONFIG_POWERMON_INA228 OR CONFIG_POWERMON_INA237))
    message(WARNING "powermon: CONFIG_POWERMON is on but no chip driver is "
                    "selected; set CONFIG_POWERMON_<CHIP>=y "
                    "(esp-forge: POWERMON_<CHIP> = y)")
endif()

if(CONFIG_POWERMON_INA219 OR CONFIG_POWERMON_INA226 OR
   CONFIG_POWERMON_INA228 OR CONFIG_POWERMON_INA237)
    list(APPEND srcs "src/ina2xx/ina2xx.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES driver
                       PRIV_REQUIRES esp_timer)
