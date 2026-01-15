#ifndef __k3_THERMAL_H__
#define __k3_THERMAL_H__

#define MAX_SENSOR_NUMBER		8
#define CALIB_VREF_DEFAULT		(0x9A)
#define BITS_D_REG_VREF_CTRL		BITS(7, 14)
#define BITS_D_REG_VREF_OFFSET		(7)

#define BITS(_start, _end) ((BIT(_end) - BIT(_start)) + BIT(_end))

#define MAX_SENSOR_NUMBER		8
#define TEMPERATURE_OFFSET		278

#define REG_TSEN_LITE_CFG		0x00
#define REG_TSEN_LITE_CFG2		0x04
#define REG_TSEN_LITE_INT_ENB		0x08
#define REG_TSEN_LITE_INT_ST		0x0C
#define REG_TSEN_LITE_INT_CLR		0x10
#define REG_TSEN_REBOOT_THRESH		0x14
#define REG_TSEN_LITE_TEMP_DATA		0x18
#define REG_TSEN_LITE_VREF_EFFUSE	0x1C
#define REG_TSEN_LITE_TEMP_THRESH	0x20
#define REG_TSEN_LITE_ECO_INTVL		0x24
#define REG_TSEN_LITE_LP_FIX_BYPASS	0x28

#define BITS_D_CK_DIV_SEL		BITS(2, 3)
#define BITS_TEMP_DATA			BITS(0, 11)
#define BITS_CK_DIV_SEL_DIV4		(0 << 2)
#define BIT_TSEN_EN			BIT(0)

#define TSEN_HIGH_THRESH_OFFSET		(0)
#define TSEN_LOW_THRESH_OFFSET		(16)

#define TEMP_RAW_DATA_DIV		(8)

struct k3_thermal_sensor_desc {
	void __iomem *base;
	int temp_offset;
	int index;
	struct k3_thermal_sensor *ks;
	struct thermal_zone_device *tzd;
};

struct k3_thermal_sensor {
	int irq;
	int temp_offset;
	void __iomem *base;
	struct clk *fclk, *bclk;
	struct reset_control *resets;
	struct device *dev;
	struct mutex lock;
	/* sensor range */
	unsigned int sr[2];
	struct k3_thermal_sensor_desc *sdesc;
	unsigned int tsen_enable_map[MAX_SENSOR_NUMBER];
};

#endif
