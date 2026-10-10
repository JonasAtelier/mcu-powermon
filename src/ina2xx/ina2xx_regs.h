/*
 * INA2xx register map: addresses and the configuration fields this driver
 * writes. Every value cites its datasheet in docs/, by table:
 *
 *   INA219  SBOS448G (Dec 2015)  docs/ina219.pdf
 *   INA226  SBOS547C (Aug 2026)  docs/ina226.pdf
 *   INA228  SLYS021A (May 2022)  docs/ina228.pdf
 *   INA237  SBOSA20A (May 2022)  docs/ina237.pdf
 *
 * The INA219 and INA226 share one layout of 16-bit registers (REG16_*). The
 * INA228 has its own (REG28_*), and the INA237's 0x00-0x08 sit at the
 * INA228's addresses (SBOSA20A Table 7-3), so it uses REG28_* too; its
 * results there are 16 bits wide, except POWER at 24.
 *
 * The scaling of each result -- LSBs, shifts, signedness -- is in
 * ina2xx_math.h, beside the code that applies it.
 */
#ifndef INA2XX_REGS_H
#define INA2XX_REGS_H

/* A0 = A1 = GND: SBOS448G Table 1, SBOS547C Table 6-2, SLYS021A and
 * SBOSA20A Table 7-2. */
#define INA2XX_ADDR_DEFAULT	0x40

/* ---- INA219 and INA226: SBOS448G Table 2, SBOS547C Table 7-1 ---- */
#define REG16_CONFIG		0x00
#define REG16_SHUNT		0x01
#define REG16_BUS		0x02
#define REG16_POWER		0x03
#define REG16_CURR		0x04
#define REG16_CAL		0x05

/* INA219 CONFIG: SBOS448G Figure 19, Tables 3-6. */
#define INA219_BRNG_32V		(1u << 13)	/* bus range 32 V */
#define INA219_PG_DIV8		(3u << 11)	/* PGA /8, +/-320 mV */
#define INA219_BADC_SHIFT	7		/* bus ADC, bits 10:7 */
#define INA219_SADC_SHIFT	3		/* shunt ADC, bits 6:3 */
#define INA219_ADC_AVERAGE	0x8u		/* ADC4 = 1, Table 5 */
#define INA219_MODE_CONT	0x7u		/* shunt and bus, continuous */

/* INA226 CONFIG: SBOS547C Table 7-2 to 7-6. */
#define INA226_CONFIG_D14	(1u << 14)	/* unnamed, POR 1, Table 7-2 */
#define INA226_AVG_SHIFT	9		/* bits 11:9, Table 7-3 */
#define INA226_VBUSCT_1100US	(4u << 6)	/* Table 7-4 */
#define INA226_VSHCT_1100US	(4u << 3)	/* Table 7-5 */
#define INA226_MODE_CONT	0x7u		/* shunt and bus, Table 7-6 */

/* ---- INA228: SLYS021A Table 7-3; the INA237 at the same addresses ---- */
#define REG28_CONFIG		0x00
#define REG28_ADCCONFIG		0x01
#define REG28_CAL		0x02
#define REG28_VSHUNT		0x04
#define REG28_VBUS		0x05
#define REG28_DIETEMP		0x06
#define REG28_CURRENT		0x07
#define REG28_POWER		0x08
#define REG28_ENERGY		0x09	/* INA228 only */
#define REG28_CHARGE		0x0A	/* INA228 only */

/*
 * CONFIG, SLYS021A Table 7-5: RSTACC clears ENERGY and CHARGE. ADCRANGE
 * (bit 4) stays 0, +/-163.84 mV, as it does on the INA237 (SBOSA20A
 * Table 7-5), where bit 14 is reserved.
 */
#define INA228_CONFIG_RSTACC	(1u << 14)

/*
 * ADC_CONFIG, SLYS021A and SBOSA20A Table 7-6: MODE Fh is continuous bus,
 * shunt and temperature; 5h is 1052 us for each conversion time; AVG in
 * bits 2:0.
 */
#define INA228_MODE_CONT_ALL	(0xFu << 12)
#define INA228_VBUSCT_1052US	(5u << 9)
#define INA228_VSHCT_1052US	(5u << 6)
#define INA228_VTCT_1052US	(5u << 3)

#endif /* INA2XX_REGS_H */
