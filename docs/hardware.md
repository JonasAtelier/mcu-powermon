# Hardware wiring

[Back to README](../README.md)

Paths mentioned in this guide are relative to the repository root.

## Pins

On ESP-IDF any GPIO works; the example in `examples/esp/` puts SDA on
GPIO 21 and SCL on GPIO 22, set at the top of `main.c`. On STM32, pins are
the ones the example in `examples/stm/` uses, on an F4: I2C1 on PB8 (SCL)
and PB9 (SDA), AF4. Any pair your chip routes to an I2C peripheral works;
check the alternate-function table in its datasheet. The bus init — pins,
clocks, speed — is the app's; each example shows one. The driver uses no
interrupt or alert line.

In the table below, the SDA and SCL rows give the STM32 pin; on ESP-IDF they
are GPIO 21 and GPIO 22, or any GPIO you choose.

### INA219, INA226, INA228, INA237 — `examples/*/ina2xx`

| pin | goes to | note |
|---|---|---|
| VS | 3.3 V | the part's own supply, not the rail being measured. 3–5.5 V on the INA219, 2.7–5.5 V on the others |
| GND | GND | shared ground, always |
| SDA | PB9 | I2C1_SDA, AF4, open drain |
| SCL | PB8 | I2C1_SCL, AF4, open drain |
| A0 | GND | with A1 grounded too this is 0x40, which is what `addr = 0` means |
| A1 | GND | each address pin takes GND, VS, SDA or SCL, so sixteen of these fit on one bus |
| IN+ | the supply side of the shunt | `shunt_ohms` is the resistor between IN+ and IN− |
| IN− | the load side of the shunt | on the INA219 the bus voltage is measured from here to ground |
| VBUS | the rail whose voltage you want | INA226, INA228 and INA237 only (0–85 V on the INA228 and INA237); the INA219 has no such pin |
| ALERT | nothing | INA226, INA228 and INA237 only, and not used by this driver; leave it open |

Names are from the four datasheets, kept locally in `docs/` (gitignored, so
not in the repository). The Adafruit INA219 breakout in the
[README](../README.md) photo calls IN+ and IN− `Vin+` and `Vin−`, with a
0.1 Ω shunt already fitted between them — which is where the examples'
`SHUNT_OHMS 0.1f` comes from. The examples only turn on the
microcontroller's weak internal pull-ups; most breakouts carry their own,
and a bare chip needs external ones on SDA and SCL.

Current is positive when it flows from IN+ to IN− through the shunt, so wire
IN+ to the supply side for a positive load current.
