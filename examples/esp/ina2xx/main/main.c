/* Logs a sample once a second through the powermon API. Wire SDA/SCL to the
 * pins below and adjust MODEL, SHUNT_OHMS and MAX_CURRENT_A to match your
 * board; MODEL's option must be on (sdkconfig.defaults turns on the INA226). */
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ina2xx.h"
#include "powermon.h"
#include "powermon_log.h"

#define SDA_GPIO      21
#define SCL_GPIO      22
#define MODEL         INA226
#define SHUNT_OHMS    0.1f
#define MAX_CURRENT_A 3.2f

static const char *TAG = "ina2xx_example";

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = SDA_GPIO,
        .scl_io_num                   = SCL_GPIO,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    const ina2xx_config_t cfg = {
        .model         = MODEL,
        .shunt_ohms    = SHUNT_OHMS,
        .max_current_a = MAX_CURRENT_A,
        .avg_samples   = 16,
    };
    static struct powermon_dev pm; /* zeroed */
    int err = ina2xx_probe(&pm, bus, &cfg);
    if (err) {
        ESP_LOGE(TAG, "probe failed: %d", err);
        return;
    }

    for (;;) {
        struct powermon_sample s;

        err = powermon_read(&pm, &s);
        if (err)
            powermon_err(&pm, "read failed: %d", err);
        else
            powermon_log_sample(&pm, &s);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
