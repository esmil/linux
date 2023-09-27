// SPDX-License-Identifier: GPL-2.0
/*
 * dwc3-thead.c - T-HEAD platform specific glue layer
 *
 * Inspired by dwc3-of-simple.c
 *
 * Copyright (C) 2021 Alibaba Group Holding Limited.
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define USB_SSP_EN		0x34
#define  REF_SSP_EN		BIT(0)
#define USB_SYS			0x3c
#define  COMMONONN		BIT(0)

#define USB3_DRD_SWRST		0x14
#define  USB3_DRD_PRST		BIT(0)
#define  USB3_DRD_PHYRST	BIT(1)
#define  USB3_DRD_VCCRST	BIT(2)
#define  USB3_DRD_RSTMASK	(USB3_DRD_PRST | USB3_DRD_PHYRST | USB3_DRD_VCCRST)

static int dwc3_thead_optimize_power(struct device *dev, void __iomem *base)
{
	struct regmap *misc_sysreg;
	u32 val;

	misc_sysreg = syscon_regmap_lookup_by_phandle(dev->of_node, "thead,misc-sysreg");
	if (IS_ERR(misc_sysreg))
		return dev_err_probe(dev, PTR_ERR(misc_sysreg),
		                     "error getting misc-sysreg\n");

	/* config usb top within USB ctrl & PHY reset */
	regmap_update_bits(misc_sysreg, USB3_DRD_SWRST,
			   USB3_DRD_RSTMASK, USB3_DRD_PRST);

	/*
	 * dwc reg also need to be configed to save power
	 * 1. set USB_SYS[COMMONONN]
	 * 2. set DWC3_GCTL[SOFITPSYNC](done by core.c)
	 * 3. set GUSB3PIPECTL[SUSPENDEN] (done by core.c)
	 */
	val = readl(base + USB_SYS);
	val |= COMMONONN;
	writel(val, base + USB_SYS);
	val = readl(base + USB_SSP_EN);
	val |= REF_SSP_EN;
	writel(val, base + USB_SSP_EN);

	regmap_update_bits(misc_sysreg, USB3_DRD_SWRST,
			   USB3_DRD_RSTMASK, USB3_DRD_RSTMASK);

	return 0;
}

static int dwc3_thead_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = devm_regulator_get_enable_optional(dev, "vbus");
	if (ret < 0 && ret != -ENODEV)
		return ret;

	ret = dwc3_thead_optimize_power(dev, base);
	if (ret)
		return ret;

	return devm_of_platform_populate(dev);
}

static const struct of_device_id dwc3_thead_of_match[] = {
	{ .compatible = "thead,th1520-usb" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, dwc3_thead_of_match);

static struct platform_driver dwc3_thead_driver = {
	.probe		= dwc3_thead_probe,
	.driver		= {
		.name	= "dwc3-thead",
		.of_match_table	= dwc3_thead_of_match,
	},
};
module_platform_driver(dwc3_thead_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare DWC3 T-HEAD Glue Driver");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
