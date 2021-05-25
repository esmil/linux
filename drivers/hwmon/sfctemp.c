/*
 * Copyright (C) 2021 Samin Guo <samin.guo@starfivetech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/* TempSensor reset. The RSTN can be de-asserted once the analog core has
 * powered up. Trst(min 100ns)
 * 0:reset  1:de-assert */
#define SFCTEMP_RSTN     BIT(0)

/* TempSensor analog core power down. The analog core will be powered up
 * Tpu(min 50us) after PD is de-asserted. RSTN should be held low until the
 * analog core is powered up.
 * 0:power up  1:power down */
#define SFCTEMP_PD       BIT(1)

/* TempSensor start conversion enable.
 * 0:disable  1:enable */
#define SFCTEMP_RUN      BIT(2)

/* TempSensor calibration mode enable.
 * 0:disable  1:enable */
#define SFCTEMP_CAL      BIT(4)

/* TempSensor signature enable. Generate a toggle value outputting on DOUT for
 * test purpose.
 * 0:disable  1:enable */
#define SFCTEMP_SGN      BIT(5)

/* TempSensor test access control.
 * 0000:normal 0001:Test1  0010:Test2  0011:Test3
 * 0100:Test4  1000:Test8  1001:Test9 */
#define SFCTEMP_TM_Pos   12
#define SFCTEMP_TM_Msk   GENMASK(15, 12)

/* TempSensor conversion value output.
 * Temp(c)=Y*DOUT/4094 - K */
#define SFCTEMP_DOUT_Pos 16
#define SFCTEMP_DOUT_Msk GENMASK(27, 16)

/* TempSensor digital test output. */
#define SFCTEMP_DIGO     BIT(31)

/* DOUT to Celcius conversion constants */
#define SFCTEMP_Y1000 237500L
#define SFCTEMP_Z       4094L
#define SFCTEMP_K1000  81100L

struct sfctemp {
	void __iomem *regs;
	u32 dout;
	bool enabled;
};

static irqreturn_t sfctemp_isr(int irq, void *data)
{
	struct sfctemp *sfctemp = data;
	u32 reg = readl(sfctemp->regs);

	sfctemp->dout = (reg & SFCTEMP_DOUT_Msk) >> SFCTEMP_DOUT_Pos;
	return IRQ_HANDLED;
}

static void sfctemp_power_up(struct sfctemp *sfctemp)
{
	writel(SFCTEMP_PD, sfctemp->regs);
	udelay(1);

	writel(0, sfctemp->regs);
	/* wait t_pu(50us) + t_rst(100ns) */
	usleep_range(60, 200);

	writel(SFCTEMP_RSTN, sfctemp->regs);
	/* wait t_su(500ps) */
	udelay(1);

	writel(SFCTEMP_RSTN | SFCTEMP_RUN, sfctemp->regs);
	/* wait 1st sample (8192 temp_sense clk: ~2MHz) */
	msleep(10);
}

static void sfctemp_power_down(struct sfctemp *sfctemp)
{
	u32 reg = readl(sfctemp->regs);

	reg &= ~SFCTEMP_RUN;
	writel(reg, sfctemp->regs);
	udelay(1);

	reg = (reg | SFCTEMP_PD) & ~SFCTEMP_RSTN;
	writel(reg, sfctemp->regs);
	udelay(1);
}

static int sfctemp_enable(struct sfctemp *sfctemp)
{
	if (sfctemp->enabled)
		return 0;

	sfctemp_power_up(sfctemp);
	sfctemp->enabled = true;
	return 0;
}

static int sfctemp_disable(struct sfctemp *sfctemp)
{
	if (!sfctemp->enabled)
		return 0;

	sfctemp_power_down(sfctemp);
	sfctemp->enabled = false;
	return 0;
}

static umode_t sfctemp_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			return 0644;
		case hwmon_temp_input:
			return 0444;
		}
		return 0;
	default:
		return 0;
	}
}

static int sfctemp_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct sfctemp *sfctemp = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			*val = sfctemp->enabled;
			return 0;
		case hwmon_temp_input:
			if (!sfctemp->enabled)
				return -ENODATA;
			/* calculate temperature in milli Celcius */
			*val = (SFCTEMP_Y1000 * (long)sfctemp->dout) / SFCTEMP_Z
				- SFCTEMP_K1000;
			return 0;
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static int sfctemp_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct sfctemp *sfctemp = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_enable:
			if (val == 0)
				return sfctemp_disable(sfctemp);
			if (val == 1)
				return sfctemp_enable(sfctemp);
			break;
		}
		return -EINVAL;
	default:
		return -EINVAL;
	}
}

static const struct hwmon_channel_info *sfctemp_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_ENABLE | HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops sfctemp_hwmon_ops = {
	.is_visible = sfctemp_is_visible,
	.read = sfctemp_read,
	.write = sfctemp_write,
};

static const struct hwmon_chip_info sfctemp_chip_info = {
	.ops = &sfctemp_hwmon_ops,
	.info = sfctemp_info,
};

static int sfctemp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *hwmon_dev;
	struct resource *mem;
	struct sfctemp *sfctemp;
	int ret;

	sfctemp = devm_kzalloc(dev, sizeof(*sfctemp), GFP_KERNEL);
	if (!sfctemp)
		return -ENOMEM;

	dev_set_drvdata(dev, sfctemp);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sfctemp->regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(sfctemp->regs))
		return PTR_ERR(sfctemp->regs);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ret, sfctemp_isr,
			       IRQF_SHARED, pdev->name, sfctemp);
	if (ret) {
		dev_err(dev, "request irq failed: %d\n", ret);
		return ret;
	}

	ret = sfctemp_enable(sfctemp);
	if (ret)
		return ret;

	hwmon_dev = hwmon_device_register_with_info(dev, pdev->name, sfctemp,
						    &sfctemp_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "%s: sensor '%s'\n", dev_name(hwmon_dev), pdev->name);
	return 0;
}

static int sfctemp_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sfctemp *sfctemp = dev_get_drvdata(dev);

	hwmon_device_unregister(dev);
	return sfctemp_disable(sfctemp);
}

static const struct of_device_id sfctemp_of_match[] = {
	{ .compatible = "sfc,tempsensor" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, sfctemp_of_match);

static struct platform_driver sfctemp_driver = {
	.driver = {
		.name = "sfctemp",
		.of_match_table = of_match_ptr(sfctemp_of_match),
	},
	.probe  = sfctemp_probe,
	.remove = sfctemp_remove,
};
module_platform_driver(sfctemp_driver);

MODULE_AUTHOR("Samin Guo");
MODULE_DESCRIPTION("Starfive JH7100 temperature sensor driver");
MODULE_LICENSE("GPL");
