/*
 * ina2xx -- TI INA219 / INA226 / INA228 / INA237 power monitors, as a
 * powermon chip driver.
 *
 * Datasheets in docs/. All four sit on I2C and measure bus voltage, shunt
 * voltage, current and power; the INA228 and INA237 add die temperature, and
 * the INA228 energy and charge accumulators too. One driver serves the
 * family: the model in the config picks the register layout, the widths and
 * the scale factors. Each model is built only under its own option,
 * CONFIG_POWERMON_INA219 to CONFIG_POWERMON_INA237.
 *
 * Two APIs share the config and the driver:
 *
 *   - ina2xx_probe() binds a struct powermon_dev, read with powermon_read(),
 *     like every powermon chip. 0 or a negative errno.
 *   - ina2xx_init() and its handle, the API of esp-ina2xx-sensor and
 *     stm-ina2xx-sensor, unchanged. On the STM32 HAL it returns 0 or a
 *     negative errno; on ESP-IDF an esp_err_t, as it always did.
 */
#pragma once

#ifdef ESP_PLATFORM
#include "driver/i2c_master.h"
#include "esp_err.h"
#else
#include <stdint.h>

#include CMSIS_device_header
#endif

#include "powermon.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INA219,
    INA226,
    INA228,
    INA237,
} ina2xx_model_t;

typedef struct ina2xx_t *ina2xx_handle_t;

typedef struct {
    ina2xx_model_t model;
    uint8_t        addr;          /* 7-bit I2C address; 0 means 0x40 (A0=A1=GND) */
    float          shunt_ohms;    /* sense resistor, e.g. 0.1f */
    float          max_current_a; /* largest current you expect; sets the current LSB */
    uint16_t       avg_samples;   /* hardware averaging; 0 or 1 disables it.
                                   * INA219 rounds down to 1/2/4/8/16/32/64/128,
                                   * INA226, INA228 and INA237 to
                                   * 1/4/16/64/128/256/512/1024. */
    float          cal_trim;      /* multiplies the calibration register; 0 means 1.0.
                                   * Trim against a known-good meter to cancel shunt
                                   * tolerance: cal_trim = measured / reported. */
} ina2xx_config_t;

typedef struct {
    float bus_v;
    float shunt_v;
    float current_a;
    float power_w;
    float die_temp_c; /* INA228 and INA237 only, NAN elsewhere */
    float energy_j;   /* INA228 only, NAN elsewhere */
    float charge_c;   /* INA228 only, NAN elsewhere */
} ina2xx_reading_t;

/*
 * Check the chip answers, calibrate it and start continuous conversion, then
 * wait 10 ms for the first one, so the first powermon_read() is not zero.
 * bus is, on ESP-IDF, an i2c_master bus the app has already created; the
 * driver adds its device at 400 kHz. On the STM32 HAL it is an I2C handle
 * the app has already HAL_I2C_Init()ed; the bus speed is the handle's.
 * Fills pm->name ("ina219" ... "ina237"), ops and priv.
 *
 * -EBUSY if pm is already bound (see struct powermon_dev); nothing is
 * touched. -EINVAL for a NULL argument, an address past 0x7F, a shunt or
 * current that is not positive, a model that is not built, or a shunt and
 * current whose calibration does not fit the register -- all decided before
 * anything is allocated, apart from the last, which nothing is written for.
 * -ENXIO if nothing ACKs at the address, decided before anything is
 * allocated. Bus failures come back as the errno of the failed transfer.
 */
#ifdef ESP_PLATFORM
int ina2xx_probe(struct powermon_dev *pm, i2c_master_bus_handle_t bus,
                 const ina2xx_config_t *cfg);
#else
int ina2xx_probe(struct powermon_dev *pm, I2C_HandleTypeDef *bus,
                 const ina2xx_config_t *cfg);
#endif

/* Releases the device; the bus is the app's and is left alone. Safe on an
 * unprobed powermon_dev, and on one another driver probed. */
void ina2xx_remove(struct powermon_dev *pm);

/* Zero the INA228 energy and charge accumulators. -ENOTSUP on the others,
 * -EINVAL for a powermon_dev this driver did not probe. */
int ina2xx_reset_energy(struct powermon_dev *pm);

#ifdef ESP_PLATFORM
/* Attach to an already-initialised I2C master bus and start continuous conversion. */
esp_err_t ina2xx_init(i2c_master_bus_handle_t bus, const ina2xx_config_t *cfg, ina2xx_handle_t *out);

/* Read every quantity the chip supports in one go. */
esp_err_t ina2xx_read(ina2xx_handle_t dev, ina2xx_reading_t *out);

/* Zero the INA228 energy and charge accumulators. ESP_ERR_NOT_SUPPORTED on the others. */
esp_err_t ina2xx_reset_accumulators(ina2xx_handle_t dev);

esp_err_t ina2xx_del(ina2xx_handle_t dev);
#else
/*
 * Check the chip answers on an I2C bus the app has already HAL_I2C_Init()ed,
 * calibrate it and start continuous conversion. Bus speed is the handle's.
 */
int ina2xx_init(I2C_HandleTypeDef *bus, const ina2xx_config_t *cfg, ina2xx_handle_t *out);

/* Read every quantity the chip supports in one go. *out is written only on success. */
int ina2xx_read(ina2xx_handle_t dev, ina2xx_reading_t *out);

/* Zero the INA228 energy and charge accumulators. -ENOTSUP on the others. */
int ina2xx_reset_accumulators(ina2xx_handle_t dev);

int ina2xx_del(ina2xx_handle_t dev);
#endif

#ifdef __cplusplus
}
#endif
