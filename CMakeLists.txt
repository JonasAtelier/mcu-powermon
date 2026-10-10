# ESP-IDF only. stm-forge never reads this file: it compiles every .c under
# src/, chip folders included, puts src/ on the include path of this
# library's sources and adds include/ -- which is why each port file is empty
# on the other platform. Here the chip files are listed one by one, and
# PRIV_INCLUDE_DIRS "src" lets a chip folder reach powermon_priv.h by plain
# name.
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
