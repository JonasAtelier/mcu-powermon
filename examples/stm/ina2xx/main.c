/*
 * Logs a sample once a second through the powermon API. Adjust MODEL,
 * SHUNT_OHMS and MAX_CURRENT_A to match your board.
 *
 * A snippet, not a project: the clock tree, the printf() retarget and the bus
 * are the app's. Pins are examples -- I2C1 on PB8 (SCL) and PB9 (SDA), AF4 --
 * any pair your chip routes to an I2C peripheral works. Written for the F4,
 * whose I2C takes Init.ClockSpeed; an F7 sets Init.Timing instead, as CubeMX
 * computes it. Needs HAL_I2C_MODULE_ENABLED and POWERMON_INA226 = y (or
 * MODEL's own option) under PKG_powermon = y; printing the floats needs
 * -u _printf_float.
 *
 * Wiring: see docs/hardware.md.
 */
#include <stdio.h>
#include <string.h>

#include CMSIS_device_header

#include "ina2xx.h"
#include "powermon.h"
#include "powermon_log.h"

#define MODEL         INA226     /* INA219, INA226, INA228 or INA237 */
#define SHUNT_OHMS    0.1f
#define MAX_CURRENT_A 3.2f

static I2C_HandleTypeDef i2c1;
static struct powermon_dev pm; /* zeroed */

/* The app's half: pins, peripheral clock and bus speed. */
static void i2c1_init(void)
{
    GPIO_InitTypeDef pins = {
        .Pin       = GPIO_PIN_8 | GPIO_PIN_9,
        .Mode      = GPIO_MODE_AF_OD,
        .Pull      = GPIO_PULLUP, /* weak; fit real 4.7k pull-ups too */
        .Speed     = GPIO_SPEED_FREQ_HIGH,
        .Alternate = GPIO_AF4_I2C1,
    };

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    HAL_GPIO_Init(GPIOB, &pins);

    i2c1.Instance             = I2C1;
    i2c1.Init.ClockSpeed      = 400000;
    i2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    i2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    HAL_I2C_Init(&i2c1);
}

int main(void)
{
    HAL_Init();
    /* SystemClock_Config() and the UART behind printf() go here. */
    i2c1_init();

    const ina2xx_config_t cfg = {
        .model         = MODEL,
        .shunt_ohms    = SHUNT_OHMS,
        .max_current_a = MAX_CURRENT_A,
        .avg_samples   = 16,
    };
    int err = ina2xx_probe(&pm, &i2c1, &cfg);
    if (err) {
        printf("ina2xx: probe failed: %s\n", strerror(-err));
        for (;;)
            ;
    }

    for (;;) {
        struct powermon_sample s;

        err = powermon_read(&pm, &s);
        if (err)
            powermon_err(&pm, "read failed: %s", strerror(-err));
        else
            powermon_log_sample(&pm, &s);
        HAL_Delay(1000);
    }
}
