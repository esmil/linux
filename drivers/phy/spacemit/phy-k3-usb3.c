// SPDX-License-Identifier: GPL-2.0-only
/*
 * phy-k3-usb3.c - SpacemiT K3 Type-C Orientation Switch Driver
 *
 * Copyright (c) 2025 SpacemiT Technology Co. Ltd
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>

#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/usb.h>
#include <linux/phy/phy.h>

#define MAX_NUM_PHY 2

#define PCIE_PHY_PU_SEL 0x40
#define PUPHY_OVRD_STATUS (1 << 10)
#define PUPHY_CFG_PHY_STATUS (1 << 9)

struct k3_usb3phy {
	struct device *dev;
	struct phy *phy;
	/* dual phy for orentation switch */
	struct regmap *regmap_bases[MAX_NUM_PHY];

	bool nop;
};

static void k3_usb3phy_update_status(struct regmap *regm)
{
	int ret;

	ret = regmap_update_bits(regm, PCIE_PHY_PU_SEL,
		PUPHY_CFG_PHY_STATUS | PUPHY_OVRD_STATUS,
		PUPHY_OVRD_STATUS);
	if (ret != 0) {
		pr_err("regmap update PCIE_PHY_PU_SEL failed, ret=%d\n", ret);
		return;
	}
	udelay(200);
}

static int k3_usb3phy_init(struct phy *phy)
{
	return 0;
}

static int k3_usb3phy_exit(struct phy *phy)
{
	return 0;
}

static int k3_usb3phy_set_speed(struct phy *phy, int speed)
{
	struct k3_usb3phy *k3_phy = phy_get_drvdata(phy);

	switch (speed) {
	case USB_SPEED_HIGH:
		k3_usb3phy_update_status(k3_phy->regmap_bases[0]);
		k3_phy->nop = true;
	default:
		break;
	}
	return 0;
}

static const struct phy_ops k3_usb3phy_ops = {
	.init = k3_usb3phy_init,
	.exit = k3_usb3phy_exit,
	.set_speed = k3_usb3phy_set_speed,
	.owner = THIS_MODULE,
};

static int k3_usb3phy_probe(struct platform_device *pdev)
{
	struct phy *(*xlate)(struct device *dev,
			     const struct of_phandle_args *args);
	struct device *dev = &pdev->dev;
	struct k3_usb3phy *k3_phy;
	struct phy_provider *provider;
	void __iomem *base;
	int num_phy;

	xlate = of_device_get_match_data(dev);

	k3_phy = devm_kzalloc(dev, sizeof(*k3_phy), GFP_KERNEL);
	if (!k3_phy)
		return -ENOMEM;

	k3_phy->dev = dev;

	/* dual phy for orientation switch */
	for (num_phy = 0; num_phy < MAX_NUM_PHY; num_phy++)
		if (!platform_get_resource(pdev, IORESOURCE_MEM, num_phy))
			break;

	for (unsigned int i = 0; i < num_phy; ++i) {
		static struct regmap_config phy_regmap_config = {
			.reg_bits = 32,
			.val_bits = 32,
			.reg_stride = 4,
			.max_register = 0x200,
		};

		base = devm_platform_ioremap_resource(pdev, i);
		if (IS_ERR(base))
			return dev_err_probe(dev, PTR_ERR(base),
					     "error mapping registers\n");

		phy_regmap_config.name = devm_kasprintf(dev, GFP_KERNEL,
							"%s-%d", dev_name(dev), i);
		k3_phy->regmap_bases[i] =
			devm_regmap_init_mmio(dev, base, &phy_regmap_config);
		if (IS_ERR(k3_phy->regmap_bases))
			return dev_err_probe(dev, PTR_ERR(k3_phy->regmap_bases),
					     "Failed to init regmap\n");
	}

	k3_phy->phy = devm_phy_create(dev, NULL, &k3_usb3phy_ops);
	if (IS_ERR(k3_phy->phy))
		return dev_err_probe(dev, PTR_ERR(k3_phy->phy),
				     "Failed to create phy\n");
	phy_set_drvdata(k3_phy->phy, k3_phy);

	provider = devm_of_phy_provider_register(dev, xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider),
				     "error registering provider\n");

	return 0;
}

static const struct of_device_id k3_usb3phy_of_match[] = {
	{ .compatible = "spacemit,k3-usb3-phy", of_phy_simple_xlate },
	{},
};
MODULE_DEVICE_TABLE(of, k3_usb3phy_of_match);

static struct platform_driver k3_usb3phy_driver = {
	 .probe = k3_usb3phy_probe,
	 .driver = {
		 .name = "spacemit,k3-usb3-phy",
		 .of_match_table = k3_usb3phy_of_match,
	 },
};
module_platform_driver(k3_usb3phy_driver);

MODULE_DESCRIPTION("SpacemiT K3 USB3 PHY Driver");
MODULE_LICENSE("GPL");
