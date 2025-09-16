// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for CTF2301 system-level thermal management solution chip
 * Datasheet: https://www.sensylink.com/upload/1/net.sensylink.portal/1689557281035.pdf
 *
 * Copyright (C) 2025 Troy Mitchell <troy.mitchell@linux.dev>
 */

#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

#define PWM_PARENT_CLOCK			360000

#define CTF2301_LOCAL_TEMP_MSB			0x00
#define CTF2301_RMT_TEMP_MSB			0x01
#define CTF2301_ALERT_STATUS			0x02
#define CTF2301_GLOBAL_CFG			0x03
#define CTF2301_RMT_TEMP_LSB			0x10
#define CTF2301_LOCAL_TEMP_LSB			0x15
#define	CTF2301_ENHANCED_CFG			0x45
#define CTF2301_TACH_COUNT_LSB			0x46
#define CTF2301_TACH_COUNT_MSB			0x47
#define CTF2301_PWM_AND_TACH_CFG		0x4a
#define CTF2301_PWM_VALUE			0x4c
#define CTF2301_PWM_FREQ			0x4d
#define CTF2301_RMT_DIODE_TEMP_FILTER		0xbf

/* remote diode fault alarm */
#define ALERT_STATUS_RDFA			BIT(2)

/* alert interrupts enable  */
#define GLOBAL_CFG_ALERT_MASK			BIT(7)
/* tach input enable  */
#define GLOBAL_CFG_TACH_SEL			BIT(2)

/* enables signed format for high and t_crit setpoints */
#define ENHANGCED_CFG_USF			BIT(3)

/* PWM Programming enable */
#define PWM_AND_TACH_CFG_PWPGM			BIT(5)

#define PWM_DEFAULT_FREQ_CODE			0x17


struct ctf2301 {
	struct i2c_client *client;

	struct regmap *regmap;

	unsigned int pwm_freq_code;
	bool temp_signed;
};

static int ctf2301_read_temp(struct device *dev, u32 attr, int channel, long *val)
{
	int regval[2], raw, err, flag = 1, shift = 4, scale = 625;
	struct ctf2301 *ctf2301 = dev_get_drvdata(dev);
	unsigned int reg_msb = CTF2301_LOCAL_TEMP_MSB,
		     reg_lsb = CTF2301_LOCAL_TEMP_LSB;

	switch (attr) {
	case hwmon_temp_input:
		if (channel != 0 && channel != 1)
			return -EOPNOTSUPP;

		if (channel == 1) {
			err = regmap_read(ctf2301->regmap, CTF2301_ALERT_STATUS, regval);
			if (err)
				return err;

			if (regval[0] & ALERT_STATUS_RDFA)
				return -ENODEV;

			shift = 5;
			scale = 1250;
			reg_msb = CTF2301_RMT_TEMP_MSB;
			reg_lsb = CTF2301_RMT_TEMP_LSB;
		}

		err = regmap_read(ctf2301->regmap, reg_msb, regval);
		if (err)
			return err;

		err = regmap_read(ctf2301->regmap, reg_lsb, regval + 1);
		if (err)
			return err;

		raw = (s16)((regval[0] << 8) | regval[1]);

		raw >>= shift;

		*val = raw * scale * flag;

		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ctf2301_read_fan(struct device *dev, u32 attr, long *val)
{
	struct ctf2301 *ctf2301 = dev_get_drvdata(dev);
	int regval[2], err, speed;

	switch (attr) {
	case hwmon_fan_input:
		err = regmap_read(ctf2301->regmap, CTF2301_TACH_COUNT_MSB, regval);
		if (err)
			return err;

		err = regmap_read(ctf2301->regmap, CTF2301_TACH_COUNT_LSB, regval + 1);
		if (err)
			return err;

		speed = (regval[0] << 8) | regval[1];

		*val = speed == 0 ? 0 : (unsigned int)(1 * (5400000 / speed));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ctf2301_write_pwm(struct device *dev, u32 attr, long val)
{
	struct ctf2301 *ctf2301 = dev_get_drvdata(dev);
	int err, map_val;

	switch (attr) {
	case hwmon_pwm_input:
		map_val = (val * ctf2301->pwm_freq_code * 2) / 255;

		err = regmap_write(ctf2301->regmap, CTF2301_PWM_VALUE, map_val);
		if (err)
			return err;
		break;
	case hwmon_pwm_freq:
		ctf2301->pwm_freq_code = DIV_ROUND_UP(PWM_PARENT_CLOCK, val) / 2;

		err = regmap_write(ctf2301->regmap, CTF2301_PWM_FREQ, ctf2301->pwm_freq_code);
		if (err)
			return err;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ctf2301_read_pwm(struct device *dev, u32 attr, long *val)
{
	struct ctf2301 *ctf2301 = dev_get_drvdata(dev);
	int err;
	unsigned int reg_val;
	unsigned int freq_code;

	switch (attr) {
	case hwmon_pwm_input:
		err = regmap_read(ctf2301->regmap, CTF2301_PWM_VALUE, &reg_val);
		if (err)
			return err;

		freq_code = ctf2301->pwm_freq_code;

		if (freq_code == 0)
			freq_code = 1;

		*val = (reg_val * 255) / (freq_code * 2);

		if (*val > 255)
			*val = 255;
		break;

	case hwmon_pwm_freq:
		err = regmap_read(ctf2301->regmap, CTF2301_PWM_FREQ, &reg_val);
		if (err)
			return err;

		if (reg_val == 0)
			reg_val = 1;

		*val = PWM_PARENT_CLOCK / (2 * reg_val);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t ctf2301_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		default:
			return 0;
		}
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_freq:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int ctf2301_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return ctf2301_read_temp(dev, attr, channel, val);
	case hwmon_fan:
		return ctf2301_read_fan(dev, attr, val);
	case hwmon_pwm:
		return ctf2301_read_pwm(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ctf2301_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return ctf2301_write_pwm(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static const struct hwmon_channel_info * const ctf2301_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_FREQ),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops ctf2301_hwmon_ops = {
	.is_visible = ctf2301_is_visible,
	.read = ctf2301_read,
	.write = ctf2301_write
};

static const struct hwmon_chip_info ctf2301_chip_info = {
	.ops = &ctf2301_hwmon_ops,
	.info = ctf2301_info,
};

static const struct regmap_config ctf2301_regmap_config = {
	.max_register = CTF2301_RMT_DIODE_TEMP_FILTER,
	.reg_bits = 8,
	.val_bits = 8,
};

static void ctf2301_parse_dt(struct device_node *np, struct ctf2301 *ctf2301)
{
	int ret;
	u32 pwm_freq;

	ret = of_property_read_u32(np, "sensylink,pwm-freq", &pwm_freq);
	if (ret)
		ctf2301->pwm_freq_code = PWM_DEFAULT_FREQ_CODE;
	else
		ctf2301->pwm_freq_code = DIV_ROUND_UP(PWM_PARENT_CLOCK, pwm_freq) / 2;

	ctf2301->temp_signed = of_property_read_bool(np, "sensylink,temp-signed");
}

static int ctf2301_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct ctf2301 *ctf2301;
	int err;

	ctf2301 = devm_kzalloc(dev, sizeof(*ctf2301), GFP_KERNEL);
	if (!ctf2301)
		return -ENOMEM;

	ctf2301->client = client;

	ctf2301_parse_dt(client->dev.of_node, ctf2301);

	ctf2301->regmap = devm_regmap_init_i2c(client, &ctf2301_regmap_config);
	if (IS_ERR(ctf2301->regmap))
		return dev_err_probe(dev, PTR_ERR(ctf2301->regmap),
				     "failed to allocate register map");

	err = regmap_write(ctf2301->regmap, CTF2301_GLOBAL_CFG,
			   GLOBAL_CFG_ALERT_MASK | GLOBAL_CFG_TACH_SEL);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to write CTF2301_GLOBAL_CFG register");

	err = regmap_write(ctf2301->regmap, CTF2301_ENHANCED_CFG, ENHANGCED_CFG_USF);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to write CTF2301_ENHANCED_CFG");

	err = regmap_write(ctf2301->regmap, CTF2301_PWM_AND_TACH_CFG, PWM_AND_TACH_CFG_PWPGM);
	if (err)
		return dev_err_probe(dev, err,
				     "failed to write CTF2301_PWM_AND_TACH_CFG");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, ctf2301,
							 &ctf2301_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return dev_err_probe(dev, PTR_ERR(hwmon_dev),
				     "failed to register hwmon device");

	return 0;
}

static const struct of_device_id ctf2301_of_match[] = {
	{ .compatible = "sensylink,ctf2301", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ctf2301_of_match);

static struct i2c_driver ctf2301_driver = {
	.driver = {
		.name	= "ctf2301",
		.of_match_table = of_match_ptr(ctf2301_of_match),
	},
	.probe		= ctf2301_probe,
};
module_i2c_driver(ctf2301_driver);

MODULE_AUTHOR("Troy Mitchell <troy.mitchell@linux.dev>");
MODULE_DESCRIPTION("ctf2301 driver");
MODULE_LICENSE("GPL");
