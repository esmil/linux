// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021-2022 Samuel Holland <samuel@sholland.org>
//

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define SUN20I_POWER_REG		0x348

#define SUN20I_SYS_LDO_CTRL_REG		0x150

struct sun20i_regulator_data {
	int				(*init)(struct device *dev,
						struct regmap *regmap);
	const struct regulator_desc	*descs;
	unsigned int			ndescs;
};

static int sun20i_d1_analog_ldos_init(struct device *dev, struct regmap *regmap)
{
	u8 bg_trim;
	int ret;

	ret = nvmem_cell_read_u8(dev, "bg_trim", &bg_trim);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get bg_trim value\n");

	/* The default value corresponds to 900 mV. */
	if (!bg_trim)
		bg_trim = 0x19;

	return regmap_update_bits(regmap, SUN20I_POWER_REG,
				  GENMASK(7, 0), bg_trim);
}

static const struct regulator_ops sun20i_d1_analog_ldo_ops = {
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_desc sun20i_d1_analog_ldo_descs[] = {
	{
		.name		= "aldo",
		.supply_name	= "vdd33",
		.of_match	= "aldo",
		.ops		= &sun20i_d1_analog_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 8,
		.min_uV		= 1650000,
		.uV_step	= 50000,
		.vsel_reg	= SUN20I_POWER_REG,
		.vsel_mask	= GENMASK(14, 12),
		.enable_reg	= SUN20I_POWER_REG,
		.enable_mask	= BIT(31),
	},
	{
		.name		= "hpldo",
		.supply_name	= "hpldoin",
		.of_match	= "hpldo",
		.ops		= &sun20i_d1_analog_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 8,
		.min_uV		= 1650000,
		.uV_step	= 50000,
		.vsel_reg	= SUN20I_POWER_REG,
		.vsel_mask	= GENMASK(10, 8),
		.enable_reg	= SUN20I_POWER_REG,
		.enable_mask	= BIT(30),
	},
};

static const struct sun20i_regulator_data sun20i_d1_analog_ldos = {
	.init	= sun20i_d1_analog_ldos_init,
	.descs	= sun20i_d1_analog_ldo_descs,
	.ndescs	= ARRAY_SIZE(sun20i_d1_analog_ldo_descs),
};

/* regulator_list_voltage_linear() modified for the non-integral uV_step. */
static int sun20i_d1_system_ldo_list_voltage(struct regulator_dev *rdev,
					     unsigned int selector)
{
	const struct regulator_desc *desc = rdev->desc;
	unsigned int uV;

	if (selector >= desc->n_voltages)
		return -EINVAL;

	uV = desc->min_uV + (desc->uV_step * selector);

	/* Produce correctly-rounded absolute voltages. */
	return uV + ((selector + 1 + (desc->min_uV % 4)) / 3);
}

static const struct regulator_ops sun20i_d1_system_ldo_ops = {
	.list_voltage		= sun20i_d1_system_ldo_list_voltage,
	.map_voltage		= regulator_map_voltage_ascend,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
};

static const struct regulator_desc sun20i_d1_system_ldo_descs[] = {
	{
		.name		= "ldoa",
		.supply_name	= "ldo-in",
		.of_match	= "ldoa",
		.ops		= &sun20i_d1_system_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 32,
		.min_uV		= 1600000,
		.uV_step	= 13333, /* repeating */
		.vsel_reg	= SUN20I_SYS_LDO_CTRL_REG,
		.vsel_mask	= GENMASK(7, 0),
	},
	{
		.name		= "ldob",
		.supply_name	= "ldo-in",
		.of_match	= "ldob",
		.ops		= &sun20i_d1_system_ldo_ops,
		.type		= REGULATOR_VOLTAGE,
		.owner		= THIS_MODULE,
		.n_voltages	= 64,
		.min_uV		= 1166666,
		.uV_step	= 13333, /* repeating */
		.vsel_reg	= SUN20I_SYS_LDO_CTRL_REG,
		.vsel_mask	= GENMASK(15, 8),
	},
};

static const struct sun20i_regulator_data sun20i_d1_system_ldos = {
	.descs	= sun20i_d1_system_ldo_descs,
	.ndescs	= ARRAY_SIZE(sun20i_d1_system_ldo_descs),
};

static const struct of_device_id sun20i_regulator_of_match[] = {
	{
		.compatible = "allwinner,sun20i-d1-analog-ldos",
		.data = &sun20i_d1_analog_ldos,
	},
	{
		.compatible = "allwinner,sun20i-d1-system-ldos",
		.data = &sun20i_d1_system_ldos,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sun20i_regulator_of_match);

static struct regmap *sun20i_regulator_get_regmap(struct device *dev)
{
	struct regmap *regmap;

	/*
	 * First try the syscon interface. The system control device is not
	 * compatible with "syscon", so fall back to getting the regmap from
	 * its platform device. This is ugly, but required for devicetree
	 * backward compatibility.
	 */
	regmap = device_node_to_regmap(dev->parent->of_node);
	if (!IS_ERR(regmap))
		return regmap;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (regmap)
		return regmap;

	return ERR_PTR(-EPROBE_DEFER);
}

static int sun20i_regulator_probe(struct platform_device *pdev)
{
	const struct sun20i_regulator_data *data;
	struct device *dev = &pdev->dev;
	struct regulator_config config;
	struct regmap *regmap;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	regmap = sun20i_regulator_get_regmap(dev);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap), "Failed to get regmap\n");

	if (data->init) {
		ret = data->init(dev, regmap);
		if (ret)
			return ret;
	}

	config = (struct regulator_config) {
		.dev	= dev,
		.regmap	= regmap,
	};

	for (unsigned int i = 0; i < data->ndescs; ++i) {
		const struct regulator_desc *desc = &data->descs[i];
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev))
			return PTR_ERR(rdev);
	}

	return 0;
}

static struct platform_driver sun20i_regulator_driver = {
	.probe	= sun20i_regulator_probe,
	.driver	= {
		.name		= "sun20i-regulator",
		.of_match_table	= sun20i_regulator_of_match,
	},
};
module_platform_driver(sun20i_regulator_driver);

MODULE_AUTHOR("Samuel Holland <samuel@sholland.org>");
MODULE_DESCRIPTION("Allwinner D1 internal LDO driver");
MODULE_LICENSE("GPL");
