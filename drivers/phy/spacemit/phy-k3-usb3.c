// SPDX-License-Identifier: GPL-2.0-only
/*
 * phy-k3-usb3.c - SpacemiT K3 Type-C Orientation Switch Driver
 *
 * Copyright (c) 2025 SpacemiT Technology Co. Ltd
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/platform_device.h>
#include <linux/of.h>

#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <linux/usb.h>
#include <linux/phy/phy.h>

#define MAX_NUM_PHY 2
#define PHY_VERSION 0x0

#define PLL_TIMEOUT 500000 /* For PHY PLL lock (usec) */
#define POLL_DELAY 500 /* Time between polls (usec) */

/* Selecting the combo PHY operating mode requires APMU regmap access */
#define SYSCON_APMU "spacemit,syscon-apmu"

#define PMUA_PCIE_SUBSYS_MGMT 0x1d8
#define PCIE_USB_COMBO_MODE_MASK GENMASK(2, 0)

#define PMUA_TYPEC_CTRL 0x110
#define TYPEC_ORIENT_FLIP BIT(2)
#define TYPEC_ORIENT_OVRD_EN BIT(3)
#define TYPEC_ORIENT_OVRD BIT(4)

/* PHY rcal init requires APB_SPARE regmap access */
#define SYSCON_APB_SPARE "spacemit,syscon-apb-spare"

#define APB_SPARE_PU_CAL 0x178
#define PUPHY_PU_CAL BIT(17)

#define APB_SPARE_RCAL_HSIO 0x17c
#define PUPHY_RCAL_NTRIM_OVRD_EN BIT(29)
#define PUPHY_RCAL_NTRIM_MASK GENMASK(27, 24)
#define PUPHY_RCAL_NTRIM_VAL(val) (((val) << 24) & PUPHY_RCAL_NTRIM_MASK)
#define NTRIM_DEFAULT 0x6
#define PUPHY_RCAL_PTRIM_OVRD_EN BIT(28)
#define PUPHY_RCAL_PTRIM_MASK GENMASK(23, 20)
#define PUPHY_RCAL_PTRIM_VAL(val) (((val) << 20) & PUPHY_RCAL_PTRIM_MASK)
#define PTRIM_DEFAULT 0xa

/* PHY Registers */
#define PCIE_PHY_PU_SEL 0x40
#define PUPHY_OVRD_STATUS (1 << 10)
#define PUPHY_CFG_PHY_STATUS (1 << 9)

#define PHY2_RESET_CFG 0x04
#define PHY2_EN_SAMPLE_DATA_AFTER_LOCK BIT(6)
#define PHY2_SOFT_RST_AHB BIT(2)
#define PHY2_SOFT_RST_PCS BIT(1)
#define PHY2_CFG_RXBUF_RST BIT(0)

#define PHY2_CLK_CFG 0x08
#define PHY2_PLL_READY BIT(0)
#define PHY2_CFG_TXCLK_INV BIT(2)
#define PHY2_CFG_RXCLK_EN BIT(3)
#define PHY2_CFG_TXCLK_EN BIT(4)
#define PHY2_CFG_PCLK_EN BIT(5)
#define PHY2_CFG_PIPE_PCLK_EN BIT(6)
#define PHY2_CFG_REFCLK_FREQ GENMASK(10, 7)
#define PHY2_REFCLK_24M 0x2
#define PHY2_CFG_SW_INIT_DONE BIT(11)

#define PHY2_MODE_CFG 0x0C
#define PHY2_CDET_CFG_LOCK_NUM GENMASK(27, 24)
#define PHY2_CDET_DOUBLE_LOCK BIT(13)
#define PHY2_CFG_LFPS_TPERIOD GENMASK(9, 8)
#define PHY2_LFPS_TPERIOD_USB 0x3
#define PHY2_CDET_STRONG_LOCK BIT(3)
#define PHY2_PCIE_PHY_INT_EN BIT(0)

#define PHY2_PU_CK_REG 0x54
#define PHY2_PU_REFCLK_100 BIT(25)
#define PHY2_REFCLK_RX_GAIN GENMASK(3, 1)
#define PHY2_REFCLK_EN_RTERM BIT(0)

#define PHY2_PLL_REG1 0x58
#define PHY2_REF_100_WSSC BIT(12)
#define PHY2_FREF_SEL GENMASK(15, 13)
#define PHY2_FREF_24M 0x1
#define PHY2_SSC_DEP_SEL GENMASK(27, 24)
#define PHY2_SSC_5000PPM 0xA
#define PHY2_SSC_MODE GENMASK(29, 28)
#define PHY2_SSC_CENTER_SPREAD 0x0
#define PHY2_SSC_UP_SPREAD 0x1
#define PHY2_SSC_DOWN_SPREAD 0x2
#define PHY2_SSC_DOWN_SPREAD1 0x3 // TODO: Weird description: 0x2/0x3 are both down

#define PHY2_PLL_REG2 0x5C
#define PHY2_EN_FASTLK BIT(31)
#define PHY2_SEL_REF100 BIT(21)
#define PHY2_EN_CK100 BIT(20)

#define PHY2_RX_REG2 0x64
#define PHY2_RX_EN_REG_OVRD BIT(31)
#define PHY2_RX_BYPASS_ADPT BIT(22)
#define PHY2_RX_RTERM_SEL BIT(5)

#define PHY2_ADPT_CFG0 0x140
#define PHY2_AFE_ADPT_RST_OVRD_EN BIT(1)
#define PHY2_AFE_ADPT_RST_OVRD_VAL BIT(4)

struct k3_usb3phy {
	struct device *dev;
	struct phy *phy;
	/* dual phy for orentation switch */
	struct regmap *regmap_bases[MAX_NUM_PHY];

	bool is_combo;
	u32 combo_sel_bit;

	/* MMIO regmap (no errors) */
	struct regmap *pmu;
	struct regmap *apb_spare;

	bool nop;
};

static void k3_usb3phy_combo_sel(struct k3_usb3phy *k3_phy, bool usb)
{
	u32 combo_mode_mask = 1 << k3_phy->combo_sel_bit;
	u32 combo_mode_val = usb << k3_phy->combo_sel_bit;

	combo_mode_mask = 0x7;
	combo_mode_val = 0x7;

	if (k3_phy->is_combo &&
	    !regmap_test_bits(k3_phy->pmu, PMUA_PCIE_SUBSYS_MGMT,
			      k3_phy->combo_sel_bit) == usb) {
		regmap_update_bits(k3_phy->pmu, PMUA_PCIE_SUBSYS_MGMT,
				   combo_mode_mask, combo_mode_val);
		dev_info(k3_phy->dev, "Update Combo Mode %d to %s Mode\n",
			 combo_mode_val, usb ? "USB" : "PCIE");
	}
}

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

static int k3_usb3phy_init_single(struct k3_usb3phy *k3_phy,
				  struct regmap *regm)
{
	struct phy *phy = k3_phy->phy;
	struct regmap *apb_spare = k3_phy->apb_spare;
	int ret;
	u32 version, reg;

	ret = regmap_read(regm, PHY_VERSION, &version);
	if (ret)
		return ret;

	regmap_update_bits(apb_spare, APB_SPARE_PU_CAL, PUPHY_PU_CAL,
			   PUPHY_PU_CAL);

	regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
			   PUPHY_RCAL_NTRIM_OVRD_EN | PUPHY_RCAL_PTRIM_OVRD_EN,
			   PUPHY_RCAL_NTRIM_OVRD_EN | PUPHY_RCAL_PTRIM_OVRD_EN);

	regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
			   PUPHY_RCAL_NTRIM_MASK | PUPHY_RCAL_PTRIM_MASK,
			   PUPHY_RCAL_NTRIM_VAL(NTRIM_DEFAULT) |
				   PUPHY_RCAL_PTRIM_VAL(PTRIM_DEFAULT));

	mdelay(100);

	/* Do not wait CDR lock before sampling data */
	regmap_update_bits(regm, PHY2_RESET_CFG, PHY2_EN_SAMPLE_DATA_AFTER_LOCK,
			   0);

	/* Power down 100MHz refclk buffer */
	regmap_update_bits(regm, PHY2_PU_CK_REG, PHY2_PU_REFCLK_100, 0);

	/* Program PLL REG1 configure the SSC */
	regmap_write(regm, PHY2_PLL_REG1,
		     FIELD_PREP(PHY2_SSC_MODE, PHY2_SSC_DOWN_SPREAD1) |
			     FIELD_PREP(PHY2_SSC_DEP_SEL, PHY2_SSC_5000PPM) |
			     FIELD_PREP(PHY2_FREF_SEL, PHY2_FREF_24M));

	/* Un-select 100MHz PLL reference */
	regmap_update_bits(regm, PHY2_PLL_REG2, PHY2_SEL_REF100, 0);

	/* USB LFPS period configuration */
	regmap_update_bits(regm, PHY2_MODE_CFG, PHY2_CFG_LFPS_TPERIOD,
			   FIELD_PREP(PHY2_CFG_LFPS_TPERIOD,
				      PHY2_LFPS_TPERIOD_USB));

	/* Force AFE adaptation reset */
	regmap_update_bits(
		regm, PHY2_ADPT_CFG0,
		PHY2_AFE_ADPT_RST_OVRD_EN | PHY2_AFE_ADPT_RST_OVRD_VAL,
		PHY2_AFE_ADPT_RST_OVRD_EN | PHY2_AFE_ADPT_RST_OVRD_VAL);
	/*
	 * Optional but commonly required for USB bring-up:
	 * bypass RX adaptation loop
	 */
	regmap_update_bits(regm, PHY2_RX_REG2, PHY2_RX_BYPASS_ADPT,
			   PHY2_RX_BYPASS_ADPT);

	/*
	 * Inform PHY that all PLL-related configuration is done.
	 * PLL will not start locking until PHY2_CFG_SW_INIT_DONE is set.
	 */
	regmap_write(regm, PHY2_CLK_CFG,
		     PHY2_CFG_SW_INIT_DONE |
			     FIELD_PREP(PHY2_CFG_REFCLK_FREQ, PHY2_REFCLK_24M) |
			     PHY2_CFG_RXCLK_EN | PHY2_CFG_PCLK_EN |
			     PHY2_CFG_PIPE_PCLK_EN | PHY2_CFG_TXCLK_EN |
			     PHY2_CFG_TXCLK_INV);

	ret = regmap_read_poll_timeout(regm, PHY2_CLK_CFG, reg,
				       (reg & PHY2_PLL_READY), POLL_DELAY,
				       PLL_TIMEOUT);
	if (ret)
		return -ETIMEDOUT;

	dev_info(&phy->dev, "PHY version: 0x%x init as USB3 mode\n", version);

	return 0;
}

static int k3_usb3phy_init(struct phy *phy)
{
	struct k3_usb3phy *k3_phy = phy_get_drvdata(phy);

	if (k3_phy->nop) {
		dev_info(&phy->dev,
			 "maximum high-speed configuration requested\n");
		return 0;
	}

	k3_usb3phy_combo_sel(k3_phy, true);

	k3_usb3phy_init_single(k3_phy, k3_phy->regmap_bases[0]);
	if (k3_phy->regmap_bases[1])
		k3_usb3phy_init_single(k3_phy, k3_phy->regmap_bases[1]);

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

	k3_phy->is_combo = device_property_read_bool(dev, "combo-usb-bit");
	device_property_read_u32(dev, "combo-usb-bit", &k3_phy->combo_sel_bit);

	k3_phy->dev = dev;

	k3_phy->pmu =
		syscon_regmap_lookup_by_phandle(dev_of_node(dev), SYSCON_APMU);
	if (IS_ERR(k3_phy->pmu))
		return dev_err_probe(dev, PTR_ERR(k3_phy->pmu),
				     SYSCON_APMU " lookup failed");

	k3_phy->apb_spare = syscon_regmap_lookup_by_phandle(dev_of_node(dev),
							    SYSCON_APB_SPARE);
	if (IS_ERR(k3_phy->apb_spare))
		return dev_err_probe(dev, PTR_ERR(k3_phy->apb_spare),
				     SYSCON_APB_SPARE " lookup failed");

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
