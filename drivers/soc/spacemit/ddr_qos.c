// SPDX-License-Identifier: GPL-2.0
/*
 * spacemit-k3 set master ddr qos driver
 *
 * Copyright (C) 2025 Spacemit
 *
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#define DDR_DEVICE_NUM		14
#define QOS_CTRL1		0x118
#define QOS_CTRL2		0x11c
#define QOS_CTRL3		0x120
#define QOS_CTRL4		0x124
#define GPU_CONFIG_CTRL		0x100
#define MASTER0_SHIFT		0
#define MASTER1_SHIFT		8
#define MASTER2_SHIFT		16
#define MASTER3_SHIFT		24
#define MASTER_RD_SHIFT		4
#define LCD1_SAT_EN		BIT(24)
#define LCD2_SAT_EN		BIT(25)

struct dev_qos_info {
	char *name;
	unsigned int qos;
};

struct spacemit_ddr_qos_info {
	void __iomem *base;
	unsigned int qos[DDR_DEVICE_NUM];
};

static char *name[DDR_DEVICE_NUM] = {"CPU", "BCM", "DMA", "USB3",
				     "SD", "USBOTG", "UFS", "AUDSYS",
				     "PCIE", "ESPI", "LCD0", "LCD1",
				     "VPU", "GPU"};

/* add module params api later */

static void parse_dtb_qos_set(struct device *dev, struct spacemit_ddr_qos_info *info)
{
	int i;
	u32 val;

	for (i = 0; i < DDR_DEVICE_NUM; i++) {
		if (of_property_read_u32(dev->of_node, name[i], &val) == 0)
			info->qos[i] = val;
		else
			info->qos[i] = -1;
	}

	/* write qos reg directly, set reg value by info->qos later */
	/* set dpu qos max */
	val = readl(info->base + QOS_CTRL1);
	val &= ~(0xff << MASTER0_SHIFT);
	val |= (0xf | (0xf << MASTER_RD_SHIFT)) << MASTER0_SHIFT;
	writel(val, info->base + QOS_CTRL1);

	val = readl(info->base + QOS_CTRL4);
	val &= ~(0xff << MASTER0_SHIFT);
	val |= (0x1 | (0x1 << MASTER_RD_SHIFT)) << MASTER0_SHIFT;
	val |= (0xf | (0xf << MASTER_RD_SHIFT)) << MASTER3_SHIFT;
	writel(val, info->base + QOS_CTRL4);

	val = readl(info->base + GPU_CONFIG_CTRL);
	val |= (0xf | (0xf << MASTER_RD_SHIFT)) << MASTER1_SHIFT;
	val |= (0xf | (0xf << MASTER_RD_SHIFT)) << MASTER2_SHIFT;
	writel(val, info->base + GPU_CONFIG_CTRL);
}

static const struct of_device_id spacemit_ddr_qos_match[] = {
	{ .compatible = "spacemit,ddr-qos" },
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_ddr_qos_match);

static int spacemit_ddr_qos_probe(struct platform_device *pdev)
{
	struct spacemit_ddr_qos_info *info;

	info = devm_kzalloc(&pdev->dev, sizeof(struct spacemit_ddr_qos_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	parse_dtb_qos_set(&pdev->dev, info);

	return 0;
}

static void spacemit_ddr_qos_remove(struct platform_device *dev)
{
	return;
}

static struct platform_driver spacemit_ddr_qos_driver = {
	.driver = {
		.name = "spacemit_ddr_qos",
		.of_match_table = of_match_ptr(spacemit_ddr_qos_match),
	},
	.probe = spacemit_ddr_qos_probe,
	.remove = spacemit_ddr_qos_remove,
};

module_platform_driver(spacemit_ddr_qos_driver);
MODULE_DESCRIPTION("k3 ddr qos config");
MODULE_AUTHOR("Spacemit");
MODULE_LICENSE("GPL v2");
