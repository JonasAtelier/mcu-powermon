/*
 * Shared by the chip drivers inside this library. Not public API.
 *
 * Two layers sit under the chip drivers, the way regmap sits under a kernel
 * driver:
 *
 *   - The bus kind is chosen at run time. A chip holds a struct powermon_bus
 *     and reaches it only through bus->ops->transfer(), so a driver can sit
 *     on I2C, SPI or a UART. Every category library offers all three kinds,
 *     used or not; every powermon chip today is on I2C.
 *   - The platform is chosen at link time. powermon_port_esp.c and
 *     powermon_port_stm.c each define the ops tables, the attach functions,
 *     the clock and the wait below for their platform; the other one
 *     compiles to nothing.
 *
 * There is no receive(): every chip here answers a register read, none
 * streams, and none wants its command and its answer as two transactions.
 *
 * ESP_PLATFORM, which ESP-IDF defines for every component, and the HAL's own
 * module switches are defined or not, never 0 or 1, so they are the one place
 * #ifdef is right; this library's CONFIG_POWERMON_* options are always tested
 * with #if.
 */
#ifndef POWERMON_PRIV_H
#define POWERMON_PRIV_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

/* The most bytes after cmd: tx on I2C and UART, tx and rx together on SPI. */
#define POWERMON_BUS_MAX_LEN	31

struct powermon_bus;

enum powermon_bus_kind {
	POWERMON_BUS_I2C,
	POWERMON_BUS_SPI,
	POWERMON_BUS_UART,
};

struct powermon_bus_ops {
	enum powermon_bus_kind kind;
	/*
	 * One transaction: cmd, then tx_len bytes of tx, then rx_len bytes
	 * into rx. cmd is the register address, with any SPI read bit already
	 * set by the chip, and a command byte on a UART.
	 *
	 *   I2C:  rx_len ? cmd, repeated start, read : cmd and tx, one write;
	 *         a bare cmd is a one-byte write. Sending tx and then reading
	 *         is -EINVAL.
	 *   SPI:  CS low, cmd, tx, rx, CS high -- high again even when the
	 *         transfer failed.
	 *   UART: drop stale input, send cmd and tx, then wait for rx_len
	 *         bytes; fewer within the timeout is -ETIMEDOUT.
	 *
	 * On SPI more than POWERMON_BUS_MAX_LEN bytes after cmd is -EINVAL; on
	 * I2C and UART only tx is bounded so, and rx on the HAL by its 16-bit
	 * count. 0 or a negative errno.
	 */
	int (*transfer)(struct powermon_bus *bus, uint8_t cmd,
			const uint8_t *tx, size_t tx_len, uint8_t *rx,
			size_t rx_len);
	/* Undo the attach. The bus itself is the app's and is left alone. */
	void (*detach)(struct powermon_bus *bus);
};

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"

/* The I2C argument of every I2C probe on this platform. */
typedef i2c_master_bus_handle_t powermon_i2c_bus;

/*
 * One device on one bus, as its driver holds it. POWERMON_HAVE_BUS says the
 * platform has at least one bus kind, so the struct exists.
 */
#define POWERMON_HAVE_BUS
struct powermon_bus {
	const struct powermon_bus_ops *ops;
	uint32_t timeout_ms;
	union {
		i2c_master_dev_handle_t i2c;
		spi_device_handle_t spi;
		uart_port_t uart;
	};
};

#else /* STM32 HAL */
#include CMSIS_device_header

#if !defined(HAL_I2C_MODULE_ENABLED) && \
    (CONFIG_POWERMON_INA219 || CONFIG_POWERMON_INA226 || \
     CONFIG_POWERMON_INA228 || CONFIG_POWERMON_INA237)
#error "the powermon chip drivers need the HAL I2C module: #define HAL_I2C_MODULE_ENABLED in the app's stm32<family>xx_hal_conf.h, with stm32<family>xx_hal_i2c.c in the build"
#endif

#ifdef HAL_I2C_MODULE_ENABLED
typedef I2C_HandleTypeDef *powermon_i2c_bus;
#endif

#if defined(HAL_I2C_MODULE_ENABLED) || defined(HAL_SPI_MODULE_ENABLED) || \
    defined(HAL_UART_MODULE_ENABLED)
#define POWERMON_HAVE_BUS
struct powermon_bus {
	const struct powermon_bus_ops *ops;
	uint32_t timeout_ms;
	union {
#ifdef HAL_I2C_MODULE_ENABLED
		struct {
			I2C_HandleTypeDef *handle;
			uint8_t address;	/* 7-bit */
		} i2c;
#endif
#ifdef HAL_SPI_MODULE_ENABLED
		struct {
			SPI_HandleTypeDef *handle;
			GPIO_TypeDef *cs_port;	/* app's output, idle high */
			uint16_t cs_pin;
		} spi;
#endif
#ifdef HAL_UART_MODULE_ENABLED
		UART_HandleTypeDef *uart;
#endif
	};
};
#endif

#endif /* STM32 HAL */

#ifdef POWERMON_HAVE_BUS
/* len bytes from register reg, as the chip sends them. */
static inline int powermon_reg_read(struct powermon_bus *bus, uint8_t reg,
				    void *buf, size_t len)
{
	return bus->ops->transfer(bus, reg, NULL, 0, buf, len);
}

static inline void powermon_bus_detach(struct powermon_bus *bus)
{
	bus->ops->detach(bus);
}
#endif

#if defined(ESP_PLATFORM) || defined(HAL_I2C_MODULE_ENABLED)
/*
 * Is anything at this 7-bit address? -ENXIO when nothing ACKs -- nothing at
 * that address -- not -ENODEV, which means something answered but it is the
 * wrong chip. A NACK in the middle of a transfer is a plain -EIO on both
 * platforms, which is why every probe decides presence here, never by its
 * first transfer.
 */
int powermon_i2c_probe(powermon_i2c_bus handle, uint8_t address,
		       uint32_t timeout_ms);

/*
 * Bind bus to the I2C device at address. On ESP-IDF the driver adds the
 * device at scl_hz, 0 for 400 kHz; on the HAL the clock is the handle's and
 * scl_hz is ignored. Undo with powermon_bus_detach(). Every attach leaves
 * bus->ops NULL when it fails, so a bus with ops is one with something to
 * detach.
 */
int powermon_i2c_attach(struct powermon_bus *bus, powermon_i2c_bus handle,
			uint8_t address, uint32_t scl_hz, uint32_t timeout_ms);
#endif

#ifdef ESP_PLATFORM
/*
 * Add the SPI device on host's cs_pin, in SPI mode 0-3 at clock_hz. The
 * host is the app's, already spi_bus_initialize()d.
 */
int powermon_spi_attach(struct powermon_bus *bus, spi_host_device_t host,
			int cs_pin, uint32_t clock_hz, uint8_t mode);

/* port's driver is the app's, already installed and configured. */
int powermon_uart_attach(struct powermon_bus *bus, uart_port_t port,
			 uint32_t timeout_ms);
#else
#ifdef HAL_SPI_MODULE_ENABLED
/*
 * Bind bus to the SPI device behind cs_port/cs_pin, and deselect it, so the
 * first transfer starts on a falling edge. Mode, clock and frame are the
 * handle's; the chip select is an output the app has set up.
 */
int powermon_spi_attach(struct powermon_bus *bus, SPI_HandleTypeDef *handle,
			GPIO_TypeDef *cs_port, uint16_t cs_pin,
			uint32_t timeout_ms);
#endif
#ifdef HAL_UART_MODULE_ENABLED
/* handle is the app's, already HAL_UART_Init()ed. */
int powermon_uart_attach(struct powermon_bus *bus, UART_HandleTypeDef *handle,
			 uint32_t timeout_ms);
#endif
#endif

/* Block for at least ms milliseconds: long, never short. */
void powermon_sleep_ms(uint32_t ms);

#endif /* POWERMON_PRIV_H */
