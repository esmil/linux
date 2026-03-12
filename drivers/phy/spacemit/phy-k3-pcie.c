// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit PCIe phy driver
 *
 * Copyright (c) 2025, spacemit Corporation.
 *
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PLL_TIMEOUT_US			500000		/* For PHY PLL lock */
#define POLL_DELAY_US			500		/* Time between polls */

#define APB_SPARE_PU_CAL		0x178
#define PU_CAL				BIT(17)

#define APB_SPARE_PU_STATUS		0x17c
#define PU_CAL_DONE			BIT(8)

/* Trim override fields in APB_SPARE_PU_STATUS (0x17c) */
#define RCAL_TRIM0			GENMASK(23, 20)
#define RCAL_TRIM1			GENMASK(27, 24)
#define RCAL_TRIM2			GENMASK(30, 28)
#define RCAL_TRIM_OVRD_EN		BIT(31)

#define RCAL_TIMEOUT_US			1000000		/* ~1s, match K2 BSP */

/* Offset between lane 0 and lane 1 blocks inside a single PHY base */
#define PHY_LANE_OFFSET			0x0400

#define PCIE_PU_ADDR_CLK_CFG		0x0008
#define PLL_READY			BIT(0)
#define CFG_INTERNAL_TIMER_ADJ		GENMASK(10, 7)
#define CFG_SW_PHY_INIT_DONE		BIT(11)

#define PCIE_RC_DONE_STATUS		0x0018
#define CFG_FORCE_RCV_RETRY		BIT(10)

/* Lane RX/TX configuration (per‑lane, at lane_base) */
#define PCIE_RX_REG1			0x0050
#define PCIE_TX_REG1			0x0064

#define PHY_PLL_REG1			(0x16 << 2)		/* 0x58 */
#define PHY_PLL_REG2			(0x17 << 2)		/* 0x5c */

#define PCIE_REF_CLK_OUTPUT

/* --------------------------------------------------------------------- */

struct k3_pcie_phy {
	struct device *dev;
	struct phy *phy;
	void __iomem *base;
	struct regmap *apb_spare;
	int phy_id;
	u32 lane_count;

	bool inited;
};

bool spacemit_k3_pcie_phy_is_busy(struct phy *phy);

static inline u32 k3_phy_readl(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static inline void k3_phy_writel(void __iomem *base, u32 offset, u32 val)
{
	writel(val, base + offset);
}

static inline void k3_phy_mod_bit(void __iomem *base, u32 offset,
				  u32 mask, bool set)
{
	u32 val = readl(base + offset);

	val &= ~mask;
	if (set)
		val |= mask;

	writel(val, base + offset);
}

static inline void k3_phy_update_bits(void __iomem *base, u32 offset,
				      u32 mask, u32 val)
{
	u32 tmp = k3_phy_readl(base, offset);

	tmp &= ~mask;
	tmp |= val & mask;

	k3_phy_writel(base, offset, tmp);
}

static inline void k3_phy_rmw_seq(void __iomem *base, u32 offset,
				  u32 mask, u32 bits)
{
	u32 v;

	v = k3_phy_readl(base, offset);
	v &= ~mask;
	k3_phy_writel(base, offset, v);

	v = k3_phy_readl(base, offset);
	v |= bits & mask;
	k3_phy_writel(base, offset, v);
}

static void k3_pcie_init_lanes(struct k3_pcie_phy *k3_phy, int num_lanes)
{
	void __iomem *phy_base = k3_phy->base;
	int i;

	dev_dbg(k3_phy->dev, "Init PHY lanes: %d\n", num_lanes);

#ifndef PCIE_100M_REF_CLK
	/* select 24MHz refclock input pll_reg2[7:4]=2 */
	k3_phy_update_bits(phy_base, PHY_PLL_REG1,
			   GENMASK(15, 12), 0x2 << 12);

	k3_phy_mod_bit(phy_base, PHY_PLL_REG2, BIT(21), 0);

	for (i = 0; i < num_lanes; i++)
		k3_phy_mod_bit(phy_base + (PHY_LANE_OFFSET * i),
			       PCIE_RX_REG1, 0x3, 0);

#ifdef PCIE_REF_CLK_OUTPUT
	k3_phy_mod_bit(phy_base, PHY_PLL_REG2, BIT(20), 1);
	k3_phy_writel(phy_base, PCIE_RX_REG1, 0x00006505);
#endif
#endif

	/* pll_reg1 of lane0, disable SSC: pll_reg4[3:0] = 0 */
	k3_phy_update_bits(phy_base, PHY_PLL_REG1, GENMASK(27, 24), 0);

	for (i = 0; i < num_lanes; i++) {
		void __iomem *lane_base = phy_base + (PHY_LANE_OFFSET * i);

		/* set cfg_tx_send_dummy_data to be 1'b1 for disable dash data */
		k3_phy_mod_bit(lane_base, (0x10 << 2), BIT(13), 1);
		/* disable en_sample_data_after_cdr_locked */
		k3_phy_mod_bit(lane_base, (0x01 << 2), BIT(6), 0);
		/* dynamic lock */
		k3_phy_mod_bit(lane_base, 0xC, BIT(2), 1);
	}

	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x60, GENMASK(7, 0), 0x10 << 0);
	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x60, GENMASK(15, 8), 0x78 << 8);
	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x60, GENMASK(23, 16), 0x98 << 16);
	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x60, GENMASK(31, 24), 0xdf << 24);

	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x64, GENMASK(7, 0), 0xb4 << 0);
	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x64, GENMASK(15, 8), 0x88 << 8);
	for (i = 0; i < num_lanes; i++)
		k3_phy_rmw_seq(phy_base + (PHY_LANE_OFFSET * i),
			       0x64, GENMASK(23, 16), 0x28 << 16);

	/* Set init done */
	for (i = 0; i < num_lanes; i++) {
		void __iomem *lane_base = phy_base + (PHY_LANE_OFFSET * i);

		/* cfg_sw_phy_init_done */
		k3_phy_mod_bit(lane_base, PCIE_PU_ADDR_CLK_CFG,
			       CFG_SW_PHY_INIT_DONE, 1);
	}
}

static void k3_pcie_wait_pll_lock(struct k3_pcie_phy *k3_phy)
{
	u32 val;
	int ret;

	dev_info(k3_phy->dev, "waiting PLL lock...\n");
	ret = readl_poll_timeout(k3_phy->base + PCIE_PU_ADDR_CLK_CFG, val,
				 val & PLL_READY,
				 POLL_DELAY_US, PLL_TIMEOUT_US);
	if (ret)
		dev_err(k3_phy->dev, "PHY PLL lock timeout\n");
	else
		dev_info(k3_phy->dev, "PHY PLL Locked.\n");
}

bool spacemit_k3_pcie_phy_is_busy(struct phy *phy)
{
	struct k3_pcie_phy *k3_phy = phy_get_drvdata(phy);

	if (!k3_phy)
		return false;

	return k3_phy->inited;
}
EXPORT_SYMBOL_GPL(spacemit_k3_pcie_phy_is_busy);

static int k3_pcie_phy_init(struct phy *phy)
{
	struct k3_pcie_phy *k3_phy = phy_get_drvdata(phy);
	u32 val;
	int ret;

	if (k3_phy->inited)
		return 0;

	if (k3_phy->apb_spare) {
		regmap_update_bits(k3_phy->apb_spare, APB_SPARE_PU_CAL,
				   PU_CAL, PU_CAL);

		ret = regmap_read_poll_timeout(k3_phy->apb_spare,
					       APB_SPARE_PU_STATUS,
					       val, val & PU_CAL_DONE,
					       10000, RCAL_TIMEOUT_US);
		if (ret) {
			dev_err(k3_phy->dev,
				"PCIe RCAL timeout, trim override\n");

			regmap_read(k3_phy->apb_spare,
				    APB_SPARE_PU_STATUS, &val);
			val &= ~(RCAL_TRIM0 | RCAL_TRIM1 | RCAL_TRIM2);
			val |= FIELD_PREP(RCAL_TRIM0, 0xa) |
			       FIELD_PREP(RCAL_TRIM1, 0x6) |
			       FIELD_PREP(RCAL_TRIM2, 0x7);
			regmap_write(k3_phy->apb_spare,
				     APB_SPARE_PU_STATUS, val);

			regmap_read(k3_phy->apb_spare,
				    APB_SPARE_PU_STATUS, &val);
			val |= RCAL_TRIM_OVRD_EN;
			regmap_write(k3_phy->apb_spare,
				     APB_SPARE_PU_STATUS, val);
		}
	}

	k3_pcie_init_lanes(k3_phy, k3_phy->lane_count);
	k3_pcie_wait_pll_lock(k3_phy);

	k3_phy->inited = true;

	return 0;
}

static int k3_pcie_phy_exit(struct phy *phy)
{
	struct k3_pcie_phy *k3_phy = phy_get_drvdata(phy);

	k3_phy->inited = false;
	return 0;
}

static const struct phy_ops k3_pcie_phy_ops = {
	.init		= k3_pcie_phy_init,
	.exit		= k3_pcie_phy_exit,
	.owner		= THIS_MODULE,
};

static int k3_pcie_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct k3_pcie_phy *k3_phy;
	struct phy_provider *provider;

	k3_phy = devm_kzalloc(dev, sizeof(*k3_phy), GFP_KERNEL);
	if (!k3_phy)
		return -ENOMEM;

	k3_phy->dev = dev;

	k3_phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(k3_phy->base))
		return PTR_ERR(k3_phy->base);

	k3_phy->apb_spare = syscon_regmap_lookup_by_phandle(dev->of_node,
							    "spacemit,syscon-apb-spare");
	if (IS_ERR(k3_phy->apb_spare)) {
		dev_dbg(dev, "no APB spare syscon, skipping PU_CAL setup\n");
		k3_phy->apb_spare = NULL;
	}

	if (of_property_read_u32(dev->of_node, "spacemit,phy-id", &k3_phy->phy_id)) {
		dev_dbg(dev, "spacemit,phy-id not found, default to -1\n");
		k3_phy->phy_id = -1;
	}

	if (of_property_read_u32(dev->of_node, "num-lanes",
				 &k3_phy->lane_count) &&
	    of_property_read_u32(dev->of_node, "spacemit,lane-count",
				 &k3_phy->lane_count))
		k3_phy->lane_count = 1;

	k3_phy->phy = devm_phy_create(dev, NULL, &k3_pcie_phy_ops);
	if (IS_ERR(k3_phy->phy))
		return dev_err_probe(dev, PTR_ERR(k3_phy->phy), "failed to create phy\n");

	phy_set_drvdata(k3_phy->phy, k3_phy);

	provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id k3_pcie_phy_of_match[] = {
	{ .compatible = "spacemit,k3-pcie-phy" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, k3_pcie_phy_of_match);

static struct platform_driver k3_pcie_phy_driver = {
	.probe	= k3_pcie_phy_probe,
	.driver = {
		.name		= "spacemit,k3-pcie-phy",
		.of_match_table	= k3_pcie_phy_of_match,
	},
};
module_platform_driver(k3_pcie_phy_driver);

MODULE_DESCRIPTION("SpacemiT K3 PCIe PHY driver");
MODULE_LICENSE("GPL");
