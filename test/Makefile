CC ?= cc
# gnu11, not c11: the powermon_* log macros use `, ##__VA_ARGS__`.
CFLAGS += -std=gnu11 -Wall -Wextra -Werror -pedantic
# src/ for the common private header, and every chip folder, because the
# tests include a chip's private headers by plain name.
CPPFLAGS += -I../include -I../src $(addprefix -I,$(wildcard ../src/*/))

# Two suites, one per platform: the same driver through
# src/powermon_port_esp.c against idf_stubs/ (esp/), and through
# src/powermon_port_stm.c against hal_stubs/ (stm/). The maths test touches
# no bus or platform API, and was the same in both original repos apart from
# the INA237 cases, so it runs once.

# What ESP-IDF passes: ESP_PLATFORM, and every chip on.
ESP_CPPFLAGS := -Iidf_stubs -DESP_PLATFORM \
		-DCONFIG_POWERMON_INA219=1 -DCONFIG_POWERMON_INA226=1 \
		-DCONFIG_POWERMON_INA228=1 -DCONFIG_POWERMON_INA237=1
# What stm-forge passes: the family header the code includes, the HAL modules
# an app's hal_conf would enable, and every chip option on
# (POWERMON_<CHIP> = y).
STM_CPPFLAGS := -Ihal_stubs \
		-DCMSIS_device_header='"stm32_hal_stub.h"' \
		-DHAL_I2C_MODULE_ENABLED -DHAL_SPI_MODULE_ENABLED \
		-DHAL_UART_MODULE_ENABLED \
		-DCONFIG_POWERMON_INA219=1 -DCONFIG_POWERMON_INA226=1 \
		-DCONFIG_POWERMON_INA228=1 -DCONFIG_POWERMON_INA237=1 \
		-DCONFIG_POWERMON_DEBUG=1

# The core and the stubs every binary of a suite links.
CORE     := ../src/powermon.c ../src/powermon_log.c
ESP_CORE := $(CORE) ../src/powermon_port_esp.c idf_stubs/idf_stubs.c
STM_CORE := $(CORE) ../src/powermon_port_stm.c hal_stubs/hal_stubs.c

MATH_TESTS := test_ina2xx_math
# esp/test_ina2xx runs both APIs; on the HAL the handle API keeps
# stm-ina2xx-sensor's own test, and the powermon API has test_ina2xx_dev.
# Plus one port test each, for the SPI and UART ops no chip uses yet.
ESP_TESTS  := $(addprefix esp/,test_core test_ina2xx test_port_esp)
STM_TESTS  := $(addprefix stm/,test_core test_ina2xx test_ina2xx_dev \
		test_port_stm)
TESTS      := $(MATH_TESTS) $(ESP_TESTS) $(STM_TESTS)

.PHONY: all clean test

all: $(TESTS)

test_ina2xx_math: test_ina2xx_math.c

esp/test_core: esp/test_core.c $(ESP_CORE)
esp/test_ina2xx: esp/test_ina2xx.c ../src/ina2xx/ina2xx.c $(ESP_CORE)
esp/test_port_esp: esp/test_port_esp.c $(ESP_CORE)

stm/test_core: stm/test_core.c $(STM_CORE)
stm/test_ina2xx: stm/test_ina2xx.c ../src/ina2xx/ina2xx.c $(STM_CORE)
stm/test_ina2xx_dev: stm/test_ina2xx_dev.c ../src/ina2xx/ina2xx.c \
		     $(STM_CORE)
stm/test_port_stm: stm/test_port_stm.c $(STM_CORE)

$(ESP_TESTS): CPPFLAGS += $(ESP_CPPFLAGS)
$(STM_TESTS): CPPFLAGS += $(STM_CPPFLAGS)

# Lets the driver tests count calloc() and free() calls, to prove a refused
# probe allocates nothing and a failed one leaks nothing. GNU ld; only the
# driver's own references are wrapped.
esp/test_ina2xx stm/test_ina2xx stm/test_ina2xx_dev: \
	LDFLAGS += -Wl,--wrap=calloc -Wl,--wrap=free
# Every printf() in a binary that links the HAL stubs also lands in
# stub_log_last, so a test can check what was logged.
$(STM_TESTS): LDFLAGS += -Wl,--wrap=printf

$(TESTS):
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) -o $@ -lm

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "./$$t"; ./$$t; done

clean:
	rm -f $(TESTS)

# Headers are prerequisites so that editing one rebuilds. Without this, `make
# test` reruns a stale binary and reports green for code it never compiled.
# Recipes use $(filter %.c,$^) so the headers never reach the compiler.
HDRS  := $(wildcard ../include/*.h ../src/*.h ../src/*/*.h)
STUBS := $(wildcard idf_stubs/*.h idf_stubs/*/*.h hal_stubs/*.h)
$(TESTS): $(HDRS) $(STUBS) ina2xx_dev_cases.h
