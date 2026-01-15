// SPDX-License-Identifier: GPL-2.0-only

#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/thermal.h>
#include <linux/reset.h>
#include "thermal_hwmon.h"
#include "thermal_core.h"
#include "k3-thermal.h"

static int init_sensors(struct platform_device *pdev)
{
	int ret;
	unsigned int val;
	struct k3_thermal_sensor *s = platform_get_drvdata(pdev);

	/* read the sensor range */
	ret = of_property_read_u32_array(pdev->dev.of_node, "sensor_range", s->sr, 2);
	if (ret < 0) {
		dev_err(&pdev->dev, "get sensor range error\n");
		return ret;
	}

	if (s->sr[1] >= MAX_SENSOR_NUMBER) {
		dev_err(&pdev->dev, "un-fitable sensor range\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(pdev->dev.of_node, "tsensor_map",
			s->tsen_enable_map, s->sr[1] - s->sr[0] + 1);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get definition of tsensor_map\n");
		return -EINVAL;
	}

	/* first: disable all the interrupts */
        writel(0xffffffff, s->base + REG_TSEN_LITE_INT_CLR);
        writel(0xffffffff, s->base + REG_TSEN_LITE_INT_ENB);

        /* select clk div 26M/4 */
        val = readl(s->base + REG_TSEN_LITE_CFG);
        val &= ~BITS_D_CK_DIV_SEL;
        val |= BITS_CK_DIV_SEL_DIV4;

	/* vref calibration */
	val &= ~BITS_D_REG_VREF_CTRL;
	val |= ((CALIB_VREF_DEFAULT & 0xff) << BITS_D_REG_VREF_OFFSET);

        writel(val, s->base + REG_TSEN_LITE_CFG);

	return 0;
}

static void enable_sensors(struct platform_device *pdev)
{
	struct k3_thermal_sensor *s = platform_get_drvdata(pdev);

	writel(readl(s->base + REG_TSEN_LITE_CFG) | BIT_TSEN_EN,
			s->base + REG_TSEN_LITE_CFG);
}

static int k3_thermal_get_temp(struct thermal_zone_device *tz, int *temp)
{
	struct k3_thermal_sensor_desc *desc = (struct k3_thermal_sensor_desc *)tz->devdata;

	mutex_lock(&desc->ks->lock);
	/* select which sensor */
	writel(desc->index, desc->base + REG_TSEN_LITE_CFG2);
	msleep(1);
	*temp = readl(desc->base + REG_TSEN_LITE_TEMP_DATA);
	mutex_unlock(&desc->ks->lock);

	*temp &= BITS_TEMP_DATA;
	*temp /= TEMP_RAW_DATA_DIV;

	*temp -= desc->temp_offset;

	*temp *= 1000;

	return 0;
}

/**
 * static int k3_thermal_set_trips(struct thermal_zone_device *tz, int low, int high)
 * {
 *	int index;
 *	unsigned int temp;
 *	int over_thrsh = high;
 *	int under_thrsh = low;
 *	struct k3_thermal_sensor_desc *desc = (struct k3_thermal_sensor_desc *)tz->devdata;
 *
 *	index = desc - desc;
 *	writel(index, desc->base + REG_TSEN_LITE_CFG2);
 *
 *	over_thrsh /= 1000;
 *	over_thrsh += desc->temp_offset;
 *	over_thrsh *= TEMP_RAW_DATA_DIV;
 *
 *	temp = readl(desc->base + REG_TSEN_LITE_TEMP_THRESH);
 *	temp &= ~0xffff0000;
 *	temp |= (over_thrsh << TSEN_HIGH_THRESH_OFFSET);
 *	writel(temp, desc->base + REG_TSEN_LITE_TEMP_THRESH);
 *
 *	if (low < 0)
 *		under_thrsh = 0;
 *
 *	under_thrsh /= 1000;
 *	under_thrsh += desc->temp_offset;
 *	under_thrsh *= TEMP_RAW_DATA_DIV;
 *	temp = readl(desc->base + REG_TSEN_LITE_TEMP_THRESH);
 *	temp &= ~0xffff;
 *	temp |= (under_thrsh << TSEN_LOW_THRESH_OFFSET);
 *	writel(temp, desc->base + REG_TSEN_LITE_TEMP_THRESH);
 *
 *	return 0;
 * }
 */
static const struct thermal_zone_device_ops k3_of_thermal_ops = {
	.get_temp = k3_thermal_get_temp,
	/* .set_trips = k3_thermal_set_trips, */
};

/**
 * static irqreturn_t k3_thermal_irq_thread(int irq, void *data)
 * {
 *	struct k3_thermal_sensor_desc *desc = (struct k3_thermal_sensor_desc *)data;
 *
 *	thermal_zone_device_update(desc->tzd, THERMAL_EVENT_UNSPECIFIED);
 *
 *	return IRQ_HANDLED;
 * }
 */

static int k3_thermal_probe(struct platform_device *pdev)
{
	unsigned int value;
	int ret, i;
	struct resource *res;
	struct k3_thermal_sensor *s;
	struct device *dev = &pdev->dev;

	s = devm_kzalloc(dev, sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	s->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(s->base))
		return PTR_ERR(s->base);

	ret = of_property_read_u32(dev->of_node, "temperature_offset", &s->temp_offset);
	if (ret) {
		dev_err(dev, "Get temperature_offset failed\n");
		return -EINVAL;
	}

/**
 *	s->irq = platform_get_irq(pdev, 0);
 *	if (s->irq < 0) {
 *		dev_err(dev, "failed to get irq number\n");
 *		return -EINVAL;
 *	}
 */
	s->resets = devm_reset_control_get_optional(&pdev->dev, NULL);
	if (IS_ERR(s->resets))
		return PTR_ERR(s->resets);

        reset_control_deassert(s->resets);

	s->fclk = devm_clk_get(dev, "func");
	if (IS_ERR(s->fclk))
		return PTR_ERR(s->fclk);

	clk_prepare_enable(s->fclk);

	s->bclk = devm_clk_get(dev, "bus");
	if (IS_ERR(s->bclk))
		return PTR_ERR(s->bclk);

	clk_prepare_enable(s->bclk);

	mutex_init(&s->lock);

	s->sdesc = (struct k3_thermal_sensor_desc *)devm_kzalloc(dev,
			sizeof(struct k3_thermal_sensor_desc) * MAX_SENSOR_NUMBER,
			GFP_KERNEL);

	platform_set_drvdata(pdev, s);

	/* initialize the sensors */
	ret = init_sensors(pdev);

	/* enable the sensors & using auto mode */
	enable_sensors(pdev);

	pr_debug("test cfg d_out_sel[26] and d_en_autozero[25]\n");
	value = readl(s->base);
	value |= ((1 << 25) | (1 << 26));
	writel(value, s->base);
	value = readl(s->base);
	pr_debug("cfg_val: 0x%x\n", value);
	value &= ~((1 << 25) | (1 << 26));
	writel(value, s->base);
	value = readl(s->base);
	pr_debug("cfg_val: 0x%x\n", value);

	/* then register the thermal zone */
	for (i = s->sr[0]; i <= s->sr[1]; ++i) {
		if (s->tsen_enable_map[i] == 0)
			continue;

		s->sdesc[i].base = s->base;
		s->sdesc[i].index = i;
		s->sdesc[i].ks = s;
		s->sdesc[i].temp_offset = s->temp_offset;
		s->sdesc[i].tzd = devm_thermal_of_zone_register(dev,
				i, s->sdesc + i, &k3_of_thermal_ops);
		if (IS_ERR(s->sdesc[i].tzd)) {
			ret = PTR_ERR(s->sdesc[i].tzd);
			dev_err(dev, "faild to register sensor id %d: %d\n",
					i, ret);
			return ret;
		}

		/* register the thermal sensor to hwmon */
		if (devm_thermal_add_hwmon_sysfs(dev, s->sdesc[i].tzd))
			dev_warn(dev, "Failed to add hwmon sysfs attributes\n");
	}

	return 0;
}

static void k3_thermal_remove(struct platform_device *pdev)
{
	int i;
	struct k3_thermal_sensor *s = platform_get_drvdata(pdev);

	/* disable the clk */
	clk_disable_unprepare(s->fclk);
	clk_disable_unprepare(s->bclk);
	reset_control_assert(s->resets);

	for (i = s->sr[0]; i <= s->sr[1]; ++i)
		devm_thermal_of_zone_unregister(&pdev->dev, s->sdesc[i].tzd);

	return;
}

static const struct of_device_id of_k3_thermal_match[] = {
	{
		.compatible = "spacemit,k3-tsensor",
	},
	{ /* end */ }
};

MODULE_DEVICE_TABLE(of, of_k3_thermal_match);

static struct platform_driver k3_thermal_driver = {
	.driver = {
		.name		= "k3_thermal",
		.of_match_table = of_k3_thermal_match,
	},
	.probe	= k3_thermal_probe,
	.remove	= k3_thermal_remove,
};

module_platform_driver(k3_thermal_driver);
