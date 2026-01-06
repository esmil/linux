// SPDX-License-Identifier: GPL-2.0
/*
 * Derived from code from SpacemiT.
 *	Copyright (c) 2023, SPACEMIT Co., Ltd
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/linear_range.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>

#define MOD_NAME	"spacemit,regulator,mpq8655"

enum mpq8655_regulator_id {
	MPQ8655_BUCK1,
};

static const struct regulator_ops mpq8655_regulator_ops = {
	.list_voltage		= regulator_list_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_voltage_time_sel   = regulator_set_voltage_time_sel,
};

static const struct linear_range mpq8655_buck_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, 0, 0x1f4, 2000),
};

/* These define the voltage selector field for buck regulators */
#define BUCK_MASK		GENMASK(11, 0)

#define MPQ8655_ID(_TYPE, _n)	MPQ8655_ ## _TYPE ## _n
#define MPQ8655_ENABLE_REG(_off, _n)	((_off) + ((_n) - 1))

#define MPQ8655_REG_DESC(_TYPE, _type, _n, _s, _off, _mask, _nv, _ranges)	\
	{								\
		.name			= #_type #_n,			\
		.supply_name		= _s,				\
		.of_match		= of_match_ptr(#_type #_n),	\
		.regulators_node	= of_match_ptr("regulators"),	\
		.id			= MPQ8655_ID(_TYPE, _n),		\
		.n_voltages		= _nv,				\
		.ops			= &mpq8655_regulator_ops,	\
		.owner			= THIS_MODULE,			\
		.linear_ranges		= _ranges,			\
		.n_linear_ranges	= ARRAY_SIZE(_ranges),		\
		.vsel_reg		= MPQ8655_ENABLE_REG(_off, _n),	\
		.vsel_mask		= _mask,			\
	}

#define MPQ8655_BUCK_DESC(_n) \
	MPQ8655_REG_DESC(BUCK, edcdc, _n, "vcc", 0x21, BUCK_MASK, 501, mpq8655_buck_ranges)


static const struct regulator_desc mpq8655_regulator_desc[] = {
	MPQ8655_BUCK_DESC(1),
};

static int mpq8655_regulator_probe(struct platform_device *pdev)
{
	struct regulator_config config = { };
	struct device *dev = &pdev->dev;
	u32 i;

	/*
	 * The parent device (PMIC) owns the regmap.  Since we don't
	 * provide one in the config structure, that one will be used.
	 */
	config.dev = dev->parent;

	for (i = 0; i < ARRAY_SIZE(mpq8655_regulator_desc); i++) {
		const struct regulator_desc *desc = &mpq8655_regulator_desc[i];
		struct regulator_dev *rdev;

		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev),
					     "error registering regulator %s\n",
					     desc->name);
	}

	return 0;
}

static struct platform_driver mpq8655_regulator_driver = {
	.probe = mpq8655_regulator_probe,
	.driver = {
		.name = MOD_NAME,
	},
};

module_platform_driver(mpq8655_regulator_driver);

MODULE_DESCRIPTION("SpacemiT MPQ8655 regulator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MOD_NAME);
