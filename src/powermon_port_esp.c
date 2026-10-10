/*
 * powermon_priv.h on ESP-IDF. The CMakeLists lists this file; stm-forge
 * compiles every .c in src/, so outside ESP-IDF it is empty.
 */
#ifdef ESP_PLATFORM

#include <string.h>

#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "powermon.h"
#include "powermon_priv.h"

/* 400 kHz when a config says 0: fast mode, the most the chips here take. */
#define DEFAULT_SCL_HZ		400000

/*
 * esp_err_t -> negative errno, at the bus boundary and nowhere else.
 * i2c_master_probe() reports a missing ACK as ESP_ERR_NOT_FOUND, which is
 * -ENXIO (nothing at that address), not -ENODEV (something answered but it
 * is the wrong chip). A NACK in the middle of a transfer comes back from IDF
 * 5.5 as ESP_ERR_INVALID_STATE, which lands on -EIO.
 */
static int powermon_errno(esp_err_t err)
{
	switch (err) {
	case ESP_OK:
		return 0;
	case ESP_ERR_INVALID_ARG:
		return -EINVAL;
	case ESP_ERR_TIMEOUT:
		return -ETIMEDOUT;
	case ESP_ERR_NOT_FOUND:
		return -ENXIO;
	case ESP_ERR_INVALID_SIZE:
		return -EMSGSIZE;
	case ESP_ERR_NO_MEM:
		return -ENOMEM;
	default:
		return -EIO;
	}
}

int64_t powermon_time_us(void)
{
	return esp_timer_get_time();
}

/*
 * Block for at least ms milliseconds. Two things make a plain
 * vTaskDelay(pdMS_TO_TICKS(ms)) wait less than asked:
 *
 *   - pdMS_TO_TICKS() truncates. At IDF's default 100 Hz tick, 2 ms is 0
 *     ticks and 15 ms is 1, so round up instead.
 *   - vTaskDelay(n) counts tick interrupts, and the first one may be about
 *     to fire, so n ticks can be as little as n - 1 full periods. Add one.
 *
 * So 2 ms at 100 Hz asks for 2 ticks and waits 10-20 ms: long, never short.
 */
static TickType_t powermon_ticks(uint32_t ms)
{
	return (TickType_t)(((uint64_t)ms * configTICK_RATE_HZ + 999) / 1000) +
	       1;
}

void powermon_sleep_ms(uint32_t ms)
{
	vTaskDelay(powermon_ticks(ms));
}

/* ------------------------------------------------------------------ I2C -- */

int powermon_i2c_probe(powermon_i2c_bus handle, uint8_t address,
		       uint32_t timeout_ms)
{
	return powermon_errno(i2c_master_probe(handle, address,
					       (int)timeout_ms));
}

static int powermon_i2c_transfer(struct powermon_bus *bus, uint8_t cmd,
				 const uint8_t *tx, size_t tx_len, uint8_t *rx,
				 size_t rx_len)
{
	uint8_t out[1 + POWERMON_BUS_MAX_LEN];

	if ((tx_len && rx_len) || tx_len > POWERMON_BUS_MAX_LEN)
		return -EINVAL;
	out[0] = cmd;
	if (rx_len)
		return powermon_errno(i2c_master_transmit_receive(
			bus->i2c, out, 1, rx, rx_len, (int)bus->timeout_ms));
	if (tx_len)
		memcpy(out + 1, tx, tx_len);
	return powermon_errno(i2c_master_transmit(bus->i2c, out, 1 + tx_len,
						  (int)bus->timeout_ms));
}

static void powermon_i2c_detach(struct powermon_bus *bus)
{
	i2c_master_bus_rm_device(bus->i2c);
}

static const struct powermon_bus_ops powermon_i2c_ops = {
	.kind = POWERMON_BUS_I2C,
	.transfer = powermon_i2c_transfer,
	.detach = powermon_i2c_detach,
};

int powermon_i2c_attach(struct powermon_bus *bus, powermon_i2c_bus handle,
			uint8_t address, uint32_t scl_hz, uint32_t timeout_ms)
{
	i2c_device_config_t devcfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = address,
		.scl_speed_hz = scl_hz ? scl_hz : DEFAULT_SCL_HZ,
	};

	int ret;

	ret = powermon_errno(i2c_master_bus_add_device(handle, &devcfg,
						       &bus->i2c));
	if (ret)
		return ret;
	/* Set only now: a bus with ops is one that has something to detach. */
	bus->ops = &powermon_i2c_ops;
	bus->timeout_ms = timeout_ms;
	return 0;
}

/* ------------------------------------------------------------------ SPI -- */

/*
 * One full-duplex transaction, so CS stays low across cmd, tx and rx: clock
 * out cmd and tx followed by zeros, and keep what came back after them. The
 * buffers live on the stack, word-aligned: IDF bounces non-DMA-capable or
 * unaligned buffers through a copy of its own.
 */
static int powermon_spi_transfer(struct powermon_bus *bus, uint8_t cmd,
				 const uint8_t *tx, size_t tx_len, uint8_t *rx,
				 size_t rx_len)
{
	uint8_t out[1 + POWERMON_BUS_MAX_LEN] __attribute__((aligned(4))) = {
		0
	};
	uint8_t in[1 + POWERMON_BUS_MAX_LEN] __attribute__((aligned(4)));
	spi_transaction_t t = { 0 };
	int ret;

	if (tx_len + rx_len > POWERMON_BUS_MAX_LEN)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	t.length = (1 + tx_len + rx_len) * 8;
	t.tx_buffer = out;
	t.rx_buffer = in;
	ret = powermon_errno(spi_device_polling_transmit(bus->spi, &t));
	if (!ret && rx_len)
		memcpy(rx, in + 1 + tx_len, rx_len);
	return ret;
}

static void powermon_spi_detach(struct powermon_bus *bus)
{
	spi_bus_remove_device(bus->spi);
}

static const struct powermon_bus_ops powermon_spi_ops = {
	.kind = POWERMON_BUS_SPI,
	.transfer = powermon_spi_transfer,
	.detach = powermon_spi_detach,
};

int powermon_spi_attach(struct powermon_bus *bus, spi_host_device_t host,
			int cs_pin, uint32_t clock_hz, uint8_t mode)
{
	spi_device_interface_config_t devcfg = {
		.mode = mode,
		.clock_speed_hz = (int)clock_hz,
		.spics_io_num = cs_pin,
		.queue_size = 1,
	};

	int ret;

	ret = powermon_errno(spi_bus_add_device(host, &devcfg, &bus->spi));
	if (ret)
		return ret;
	bus->ops = &powermon_spi_ops;
	bus->timeout_ms = 0;	/* spi_master transfers take none */
	return 0;
}

/* ----------------------------------------------------------------- UART -- */

/*
 * Drop whatever is waiting -- a late reply to an earlier command would read
 * as the start of this one -- then send, then wait for the whole reply.
 * uart_read_bytes() returns early only with everything; a short count means
 * the timeout ran out.
 */
static int powermon_uart_transfer(struct powermon_bus *bus, uint8_t cmd,
				  const uint8_t *tx, size_t tx_len,
				  uint8_t *rx, size_t rx_len)
{
	uint8_t out[1 + POWERMON_BUS_MAX_LEN];
	int n, ret;

	if (tx_len > POWERMON_BUS_MAX_LEN || rx_len > INT32_MAX)
		return -EINVAL;
	out[0] = cmd;
	if (tx_len)
		memcpy(out + 1, tx, tx_len);

	ret = powermon_errno(uart_flush_input(bus->uart));
	if (ret)
		return ret;
	n = uart_write_bytes(bus->uart, out, 1 + tx_len);
	if (n != (int)(1 + tx_len))
		return -EIO;
	if (!rx_len)
		return 0;
	n = uart_read_bytes(bus->uart, rx, (uint32_t)rx_len,
			    powermon_ticks(bus->timeout_ms));
	if (n < 0)
		return -EIO;
	return (size_t)n < rx_len ? -ETIMEDOUT : 0;
}

/* The driver is the app's: nothing was added, nothing to remove. */
static void powermon_uart_detach(struct powermon_bus *bus)
{
	(void)bus;
}

static const struct powermon_bus_ops powermon_uart_ops = {
	.kind = POWERMON_BUS_UART,
	.transfer = powermon_uart_transfer,
	.detach = powermon_uart_detach,
};

int powermon_uart_attach(struct powermon_bus *bus, uart_port_t port,
			 uint32_t timeout_ms)
{
	if (!uart_is_driver_installed(port))
		return -EINVAL;
	bus->ops = &powermon_uart_ops;
	bus->timeout_ms = timeout_ms;
	bus->uart = port;
	return 0;
}

#endif /* ESP_PLATFORM */
