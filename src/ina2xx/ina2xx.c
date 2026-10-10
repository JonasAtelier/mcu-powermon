/*
 * The INA2xx family on the powermon bus layer. One driver, because the four
 * parts are one register family: the INA219 and INA226 share a layout, the
 * INA237 sits at the INA228's addresses, and what differs -- widths, LSBs,
 * calibration constants -- is per model in ina2xx_math.h. Each model's code
 * is built only under its own CONFIG_POWERMON_INA2xx.
 */
#include "powermon_priv.h"

#if CONFIG_POWERMON_INA219 || CONFIG_POWERMON_INA226 || \
    CONFIG_POWERMON_INA228 || CONFIG_POWERMON_INA237

#include <math.h>
#include <stdlib.h>

#include "ina2xx.h"
#include "ina2xx_math.h"
#include "ina2xx_regs.h"

#define I2C_TIMEOUT_MS 100
/* Long enough for the first conversion to land, so the first read is not 0. */
#define FIRST_CONVERSION_MS 10

struct ina2xx_state {
    struct powermon_bus bus;
    ina2xx_model_t      model;
    float               current_lsb;
};

/*
 * The handle of ina2xx_init(): the device and its state in one allocation,
 * as the handle always was one.
 */
struct ina2xx_t {
    struct powermon_dev pm;
    struct ina2xx_state st;
};

static int ina2xx_read_sample(struct powermon_dev *pm, struct powermon_sample *s);

static const struct powermon_ops ina2xx_ops = { .read = ina2xx_read_sample };

/* Register pointer, then the 16-bit value MSB first. */
static int reg_write(struct ina2xx_state *st, uint8_t reg, uint16_t val)
{
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    return st->bus.ops->transfer(&st->bus, reg, buf, sizeof buf, NULL, 0);
}

/* Reads `len` bytes big-endian into a right-aligned integer. */
static int reg_read(struct ina2xx_state *st, uint8_t reg, size_t len, uint64_t *out)
{
    uint8_t buf[5];

    if (len == 0 || len > sizeof buf) return -EINVAL;

    /* Register pointer, repeated start, then the bytes. */
    int err = powermon_reg_read(&st->bus, reg, buf, len);
    if (err) return err;

    uint64_t v = 0;
    for (size_t i = 0; i < len; i++) v = (v << 8) | buf[i];
    *out = v;
    return 0;
}

/* Largest table entry <= want, as its 3-bit register code. Tables are ascending. */
static uint8_t avg_code(const uint16_t *tbl, uint16_t want)
{
    uint8_t code = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (tbl[i] <= want) code = i;
    }
    return code;
}

#if CONFIG_POWERMON_INA219
/* SBOS448G Table 5, ADC3:1 with ADC4 = 1. */
static const uint16_t avg_tbl_219[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
#endif
#if CONFIG_POWERMON_INA226 || CONFIG_POWERMON_INA228 || CONFIG_POWERMON_INA237
/* SBOS547C Table 7-3; SLYS021A and SBOSA20A Table 7-6. */
static const uint16_t avg_tbl_2268[8] = { 1, 4, 16, 64, 128, 256, 512, 1024 };
#endif

/* The device name, or NULL for a model that is not built. */
static const char *model_name(ina2xx_model_t model)
{
    switch (model) {
#if CONFIG_POWERMON_INA219
    case INA219: return "ina219";
#endif
#if CONFIG_POWERMON_INA226
    case INA226: return "ina226";
#endif
#if CONFIG_POWERMON_INA228
    case INA228: return "ina228";
#endif
#if CONFIG_POWERMON_INA237
    case INA237: return "ina237";
#endif
    default:     return NULL;
    }
}

/*
 * Everything that can be refused before the bus is touched. *addr is the
 * address to use.
 */
static int check_config(const ina2xx_config_t *cfg, uint8_t *addr)
{
    if (!(cfg->shunt_ohms > 0.0f) || !(cfg->max_current_a > 0.0f)) return -EINVAL;
    if (!model_name(cfg->model)) return -EINVAL;

    *addr = cfg->addr ? cfg->addr : INA2XX_ADDR_DEFAULT;
    if (*addr > 0x7F) return -EINVAL; /* not a 7-bit address */
    return 0;
}

/* Calibrate the chip behind st->bus and start continuous conversion. */
static int configure(struct ina2xx_state *st, const ina2xx_config_t *cfg)
{
    double trim = (cfg->cal_trim > 0.0f) ? cfg->cal_trim : 1.0;
    uint16_t cal;
    uint8_t avg;
    int err;

    st->model = cfg->model;

    switch (cfg->model) {
#if CONFIG_POWERMON_INA219
    case INA219:
        st->current_lsb = ina219_current_lsb(cfg->max_current_a);
        /* Refuse before writing to the chip: a shunt drop this small or this
         * large has no calibration value that fits the register (16 bits
         * here, 15 on the INA226, INA228 and INA237), and programming a
         * wrong one scales every current and power reading silently. */
        cal = ina219_cal(st->current_lsb, cfg->shunt_ohms, trim);
        if (cal == 0) return -EINVAL;
        /* BRNG=32V, PGA=/8 (+/-320mV), both ADCs in averaging mode, continuous shunt+bus. */
        avg = avg_code(avg_tbl_219, cfg->avg_samples);
        err = reg_write(st, REG16_CONFIG,
                        INA219_BRNG_32V | INA219_PG_DIV8 |
                        ((INA219_ADC_AVERAGE | avg) << INA219_BADC_SHIFT) |
                        ((INA219_ADC_AVERAGE | avg) << INA219_SADC_SHIFT) |
                        INA219_MODE_CONT);
        if (err == 0) {
            err = reg_write(st, REG16_CAL, cal);
        }
        return err;
#endif

#if CONFIG_POWERMON_INA226
    case INA226:
        st->current_lsb = ina226_current_lsb(cfg->max_current_a);
        cal = ina226_cal(st->current_lsb, cfg->shunt_ohms, trim);
        if (cal == 0) return -EINVAL;
        /* Both conversion times at the 1.1ms default, continuous shunt+bus. */
        avg = avg_code(avg_tbl_2268, cfg->avg_samples);
        err = reg_write(st, REG16_CONFIG,
                        (avg << INA226_AVG_SHIFT) | INA226_VBUSCT_1100US |
                        INA226_VSHCT_1100US | INA226_MODE_CONT);
        if (err == 0) {
            err = reg_write(st, REG16_CAL, cal);
        }
        return err;
#endif

#if CONFIG_POWERMON_INA228
    case INA228:
        st->current_lsb = ina228_current_lsb(cfg->max_current_a);
        cal = ina228_cal(st->current_lsb, cfg->shunt_ohms, trim);
        if (cal == 0) return -EINVAL;
        /* ADCRANGE=0, accumulators cleared. */
        err = reg_write(st, REG28_CONFIG, INA228_CONFIG_RSTACC);
        /* Continuous bus+shunt+temperature, all conversion times at the 1052us default. */
        avg = avg_code(avg_tbl_2268, cfg->avg_samples);
        if (err == 0) {
            err = reg_write(st, REG28_ADCCONFIG,
                            INA228_MODE_CONT_ALL | INA228_VBUSCT_1052US |
                            INA228_VSHCT_1052US | INA228_VTCT_1052US | avg);
        }
        if (err == 0) {
            err = reg_write(st, REG28_CAL, cal);
        }
        return err;
#endif

#if CONFIG_POWERMON_INA237
    case INA237:
        st->current_lsb = ina237_current_lsb(cfg->max_current_a);
        cal = ina237_cal(st->current_lsb, cfg->shunt_ohms, trim);
        if (cal == 0) return -EINVAL;
        /* ADCRANGE=0, CONVDLY=0: the reset value (Table 7-5), written in case
         * an earlier firmware changed it without a power cycle. */
        err = reg_write(st, REG28_CONFIG, 0);
        /* Continuous bus+shunt+temperature, all conversion times at the
         * 1052us default (Table 7-6). */
        avg = avg_code(avg_tbl_2268, cfg->avg_samples);
        if (err == 0) {
            err = reg_write(st, REG28_ADCCONFIG,
                            INA228_MODE_CONT_ALL | INA228_VBUSCT_1052US |
                            INA228_VSHCT_1052US | INA228_VTCT_1052US | avg);
        }
        if (err == 0) {
            err = reg_write(st, REG28_CAL, cal);
        }
        return err;
#endif

    default:
        return -EINVAL;
    }
}

/*
 * Attach st->bus at addr, configure the chip and wait for its first
 * conversion, then bind pm to st. On failure the bus is detached again and
 * pm is left as it was.
 */
static int bind(struct powermon_dev *pm, struct ina2xx_state *st,
                powermon_i2c_bus bus, const ina2xx_config_t *cfg, uint8_t addr)
{
    int err = powermon_i2c_attach(&st->bus, bus, addr, 0, I2C_TIMEOUT_MS);
    if (err) return err;

    err = configure(st, cfg);
    if (err) {
        powermon_bus_detach(&st->bus);
        return err;
    }

    /* Let the first conversion land so the caller's first read is not zero.
     * Never shorter than asked; on the HAL it busy-waits on SysTick, so never
     * call init or probe from an interrupt at or above SysTick's priority. */
    powermon_sleep_ms(FIRST_CONVERSION_MS);

    pm->name = model_name(cfg->model);
    pm->ops  = &ina2xx_ops;
    pm->priv = st;
    return 0;
}

int ina2xx_probe(struct powermon_dev *pm, powermon_i2c_bus bus,
                 const ina2xx_config_t *cfg)
{
    if (!pm || !bus || !cfg) return -EINVAL;
    if (pm->ops || pm->priv) return -EBUSY;

    uint8_t addr;
    int err = check_config(cfg, &addr);
    if (err) return err;

    /* Presence first, before anything is allocated or written. */
    err = powermon_i2c_probe(bus, addr, I2C_TIMEOUT_MS);
    if (err) return err;

    struct ina2xx_state *st = calloc(1, sizeof *st);
    if (!st) return -ENOMEM;

    err = bind(pm, st, bus, cfg, addr);
    if (err) free(st);
    return err;
}

/* pm's state, or NULL if this driver did not probe it. */
static struct ina2xx_state *state_of(struct powermon_dev *pm)
{
    return (pm && pm->ops == &ina2xx_ops) ? pm->priv : NULL;
}

void ina2xx_remove(struct powermon_dev *pm)
{
    struct ina2xx_state *st = state_of(pm);

    if (!st) return;
    powermon_bus_detach(&st->bus);
    free(st);
    pm->name = NULL;
    pm->ops  = NULL;
    pm->priv = NULL;
}

static int reset_energy(struct ina2xx_state *st)
{
#if CONFIG_POWERMON_INA228
    if (st->model == INA228)
        return reg_write(st, REG28_CONFIG, INA228_CONFIG_RSTACC); /* self-clearing */
#endif
    (void)st;
    return -ENOTSUP;
}

int ina2xx_reset_energy(struct powermon_dev *pm)
{
    struct ina2xx_state *st = state_of(pm);

    if (!st) return -EINVAL;
    return reset_energy(st);
}

/* Fills s, but for t_us; s is the caller's scratch and is written on failure too. */
static int ina2xx_read_sample(struct powermon_dev *pm, struct powermon_sample *s)
{
    struct ina2xx_state *st = pm->priv;
    int err;
    uint64_t bus, shunt, curr, power;

    s->die_temp_c = NAN;
    s->energy_j   = NAN;
    s->charge_c   = NAN;

#if CONFIG_POWERMON_INA228
    if (st->model == INA228) {
        uint64_t temp, energy, charge;

        if ((err = reg_read(st, REG28_VBUS, 3, &bus)) != 0) return err;
        if ((err = reg_read(st, REG28_VSHUNT, 3, &shunt)) != 0) return err;
        if ((err = reg_read(st, REG28_CURRENT, 3, &curr)) != 0) return err;
        if ((err = reg_read(st, REG28_POWER, 3, &power)) != 0) return err;
        if ((err = reg_read(st, REG28_DIETEMP, 2, &temp)) != 0) return err;
        if ((err = reg_read(st, REG28_ENERGY, 5, &energy)) != 0) return err;
        if ((err = reg_read(st, REG28_CHARGE, 5, &charge)) != 0) return err;

        s->bus_v      = ina228_bus_v(bus);
        s->shunt_v    = ina228_shunt_v(shunt);
        s->current_a  = ina228_current_a(curr, st->current_lsb);
        s->power_w    = ina228_power_w(power, st->current_lsb);
        s->die_temp_c = ina228_die_temp_c(temp);
        s->energy_j   = ina228_energy_j(energy, st->current_lsb);
        s->charge_c   = ina228_charge_c(charge, st->current_lsb);
        return 0;
    }
#endif

#if CONFIG_POWERMON_INA237
    if (st->model == INA237) {
        uint64_t temp;

        if ((err = reg_read(st, REG28_VBUS, 2, &bus)) != 0) return err;
        if ((err = reg_read(st, REG28_VSHUNT, 2, &shunt)) != 0) return err;
        if ((err = reg_read(st, REG28_CURRENT, 2, &curr)) != 0) return err;
        if ((err = reg_read(st, REG28_POWER, 3, &power)) != 0) return err;
        if ((err = reg_read(st, REG28_DIETEMP, 2, &temp)) != 0) return err;

        s->bus_v      = ina237_bus_v(bus);
        s->shunt_v    = ina237_shunt_v(shunt);
        s->current_a  = ina237_current_a(curr, st->current_lsb);
        s->power_w    = ina237_power_w(power, st->current_lsb);
        s->die_temp_c = ina237_die_temp_c(temp);
        return 0;
    }
#endif

#if CONFIG_POWERMON_INA219 || CONFIG_POWERMON_INA226
    if ((err = reg_read(st, REG16_BUS, 2, &bus)) != 0) return err;
    if ((err = reg_read(st, REG16_SHUNT, 2, &shunt)) != 0) return err;
    if ((err = reg_read(st, REG16_CURR, 2, &curr)) != 0) return err;
    if ((err = reg_read(st, REG16_POWER, 2, &power)) != 0) return err;

    s->current_a = (float)(int16_t)curr * st->current_lsb;

#if CONFIG_POWERMON_INA219
    if (st->model == INA219) {
        s->bus_v   = ina219_bus_v(bus);
        s->shunt_v = ina219_shunt_v(shunt);
        s->power_w = ina219_power_w(power, st->current_lsb);
        return 0;
    }
#endif
#if CONFIG_POWERMON_INA226
    s->bus_v   = ina226_bus_v(bus);
    s->shunt_v = ina226_shunt_v(shunt);
    s->power_w = ina226_power_w(power, st->current_lsb);
#endif
    return 0;
#else
    return -EINVAL; /* unreachable: probe binds only built models */
#endif
}

/* -------------------------------------- the ina2xx_init() handle API -- */

#ifdef ESP_PLATFORM
/*
 * negative errno -> esp_err_t, for the handle API's ESP-IDF callers. The bus
 * layer's own mapping is lossy in this direction: an IDF code with no errno
 * of its own came up as -EIO and goes back as ESP_FAIL.
 */
static esp_err_t esp_err_of(int err)
{
    switch (err) {
    case 0:          return ESP_OK;
    case -EINVAL:    return ESP_ERR_INVALID_ARG;
    case -ENOMEM:    return ESP_ERR_NO_MEM;
    case -ETIMEDOUT: return ESP_ERR_TIMEOUT;
    case -ENOTSUP:   return ESP_ERR_NOT_SUPPORTED;
    case -ENXIO:     return ESP_ERR_NOT_FOUND;
    case -EMSGSIZE:  return ESP_ERR_INVALID_SIZE;
    default:         return ESP_FAIL;
    }
}
#define HANDLE_RET(err) esp_err_of(err)
typedef esp_err_t handle_ret_t;
#else
#define HANDLE_RET(err) (err)
typedef int handle_ret_t;
#endif

handle_ret_t ina2xx_init(powermon_i2c_bus bus, const ina2xx_config_t *cfg, ina2xx_handle_t *out)
{
    if (!bus || !cfg || !out) return HANDLE_RET(-EINVAL);

    uint8_t addr;
    int err = check_config(cfg, &addr);
    if (err) return HANDLE_RET(err);

    /* Presence first, before anything is allocated or written. */
    err = powermon_i2c_probe(bus, addr, I2C_TIMEOUT_MS);
    if (err) return HANDLE_RET(err);

    struct ina2xx_t *h = calloc(1, sizeof *h);
    if (!h) return HANDLE_RET(-ENOMEM);

    err = bind(&h->pm, &h->st, bus, cfg, addr);
    if (err) {
        free(h);
        return HANDLE_RET(err);
    }
    *out = h;
    return HANDLE_RET(0);
}

handle_ret_t ina2xx_read(ina2xx_handle_t h, ina2xx_reading_t *out)
{
    if (!h || !out) return HANDLE_RET(-EINVAL);

    /* Read into scratch and copy out only on success, so a failed read leaves *out alone. */
    struct powermon_sample s;
    int err = ina2xx_read_sample(&h->pm, &s);
    if (err) return HANDLE_RET(err);

    out->bus_v      = s.bus_v;
    out->shunt_v    = s.shunt_v;
    out->current_a  = s.current_a;
    out->power_w    = s.power_w;
    out->die_temp_c = s.die_temp_c;
    out->energy_j   = s.energy_j;
    out->charge_c   = s.charge_c;
    return HANDLE_RET(0);
}

handle_ret_t ina2xx_reset_accumulators(ina2xx_handle_t h)
{
    if (!h) return HANDLE_RET(-EINVAL);
    return HANDLE_RET(reset_energy(&h->st));
}

/* Frees the handle. The bus is the app's and is left alone. */
handle_ret_t ina2xx_del(ina2xx_handle_t h)
{
    if (!h) return HANDLE_RET(-EINVAL);
    powermon_bus_detach(&h->st.bus);
    free(h);
    return HANDLE_RET(0);
}

#endif /* CONFIG_POWERMON_INA2xx */
