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

#define PLL_TIMEOUT 500000 /* For PHY PLL lock (usec) */
#define POLL_DELAY 500 /* Time between polls (usec) */

/* Selecting the combo PHY operating mode requires APMU regmap access */
#define SYSCON_APMU "spacemit,syscon-apmu"

/*
 * The PCIE/USB Subsystem on SpacemiT K3 have 3 single lane PIPE3 PHYs
 * (PHY2/3/4) shared by PCIE PortC/D and USB3 PortB/C/D.
 *
 * PMUA_PCIE_SUBSYS_MGMT[4:0]
 *
 *   bit4 = 0 : PCIe A X8 mode, all 8 lanes dedicated to PCIe Port A
 *          1 : PHY lanes shared between PCIe or USB according to [3:0]
 *
 * All PHY matrix combinations according to [4:0]:
 *
 *   0x0X : PCIe-A X8
 *   0x10 : PCIe-C x2 (PHY2+PHY3) + PCIe-D x1 (PHY4)
 *   0x11 : PCIe-C x2 (PHY2+PHY3) + USB-D (PHY4)
 *   0x12 : PCIe-C x1 (PHY2)      + USB-C (PHY3)
 *   0x13 : PCIe-C x1 (PHY2)      + USB-C (PHY3) + USB-D (PHY4)
 *   0x14 : PCIe-C x1 (PHY3)      + USB-B (PHY2)
 *   0x15 : PCIe-C x1 (PHY3)      + USB-B (PHY2) + USB-D (PHY4)
 *   0x16 : USB-B (PHY2) + USB-C (PHY3) + PCIe D x1 (PHY4)
 *   0x17 : USB-B (PHY2) + USB-C (PHY3) + USB-D (PHY4)
 *
 * So any USB Port B/C/D operation requires PCIe A X8 mode to be disabled.
 */
#define PMUA_PCIE_SUBSYS_MGMT 0x1d8
#define PU_MATRIX_CONF_X8_DISABLE BIT(4)
#define PU_MATRIX_CONF_USB_MASK GENMASK(2, 0)

#define PMUA_TYPEC_CTRL 0x110
#define TYPEC_ORIENT_FLIP BIT(2)
#define TYPEC_ORIENT_OVRD_EN BIT(3)
#define TYPEC_ORIENT_OVRD BIT(4)

/* PHY rcal init requires APB_SPARE regmap access */
#define SYSCON_APB_SPARE "spacemit,syscon-apb-spare"

#define APB_SPARE_PU_CAL 0x178
#define PU_CAL BIT(17)

#define APB_SPARE_RCAL_HSIO 0x17c
#define R_CAL_OVRD_NTRIM_EN BIT(29)
#define R_CAL_OVRD_NTRIM_MASK GENMASK(27, 24)
#define R_CAL_OVRD_NTRIM_VAL(val) FIELD_PREP(R_CAL_OVRD_NTRIM_MASK, val)
#define NTRIM_DEFAULT 0x6
#define R_CAL_OVRD_PTRIM_EN BIT(28)
#define R_CAL_OVRD_PTRIM_MASK GENMASK(23, 20)
#define R_CAL_OVRD_PTRIM_VAL(val) FIELD_PREP(R_CAL_OVRD_PTRIM_MASK, val)
#define PTRIM_DEFAULT 0xa

/* PHY Registers */
#define PHY_VERSION 0x0

#define PHY_PU_SEL 0x40
#define OVRD_STATUS BIT(10)
#define CFG_STATUS BIT(9)

#define PHY_RESET_CFG 0x04
#define EN_SAMPLE_DATA_AFTER_LOCK BIT(6)
#define SOFT_RST_AHB BIT(2)
#define SOFT_RST_PCS BIT(1)
#define CFG_RXBUF_RST BIT(0)

#define PHY_CLK_CFG 0x08
#define PLL_READY BIT(0)
#define CFG_TXCLK_INV BIT(2)
#define CFG_RXCLK_EN BIT(3)
#define CFG_TXCLK_EN BIT(4)
#define CFG_PCLK_EN BIT(5)
#define CFG_PIPE_PCLK_EN BIT(6)
#define CFG_REFCLK_FREQ GENMASK(10, 7)
#define REFCLK_24M 0x2
#define CFG_SW_INIT_DONE BIT(11)

#define PHY_MODE_CFG 0x0C
#define CDET_CFG_LOCK_NUM GENMASK(27, 24)
#define CDET_DOUBLE_LOCK BIT(13)
#define CFG_LFPS_TPERIOD GENMASK(9, 8)
#define LFPS_TPERIOD_USB 0x3
#define CDET_STRONG_LOCK BIT(3)
#define PCIE_INT_EN BIT(0)

#define PHY_PU_CK_REG 0x54
#define PU_REFCLK_100 BIT(25)
#define REFCLK_RX_GAIN GENMASK(3, 1)
#define REFCLK_EN_RTERM BIT(0)

#define PHY_PLL_REG1 0x58
#define REF_100_WSSC BIT(12)
#define FREF_SEL GENMASK(15, 13)
#define FREF_24M 0x1
#define SSC_DEP_SEL GENMASK(27, 24)
#define SSC_5000PPM 0xa
#define SSC_MODE GENMASK(29, 28)
#define SSC_CENTER_SPREAD 0x0
#define SSC_UP_SPREAD 0x1
#define SSC_DOWN_SPREAD 0x2
#define SSC_DOWN_SPREAD1 0x3 // TODO: Weird description: 0x2/0x3 are both down

#define PHY_PLL_REG2 0x5c
#define EN_FASTLK BIT(31)
#define SEL_REF100 BIT(21)
#define EN_CK100 BIT(20)

#define PHY_RX_REG2 0x64
#define RX_EN_REG_OVRD BIT(31)
#define RX_BYPASS_ADPT BIT(22)
#define RX_RTERM_SEL BIT(5)

#define PHY_ADPT_CFG0 0x140
#define AFE_ADPT_RST_OVRD_EN BIT(1)
#define AFE_ADPT_RST_OVRD_VAL BIT(4)

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

static void k3_usb3phy_combo_set_usb(struct k3_usb3phy *k3_phy, bool usb)
{
	u32 combo_mode_mask = BIT(k3_phy->combo_sel_bit);
	u32 combo_mode_val = usb << k3_phy->combo_sel_bit;

	combo_mode_mask |= PU_MATRIX_CONF_X8_DISABLE;
	combo_mode_val |= usb ? PU_MATRIX_CONF_X8_DISABLE : 0;

	if (k3_phy->is_combo &&
	    !regmap_test_bits(k3_phy->pmu, PMUA_PCIE_SUBSYS_MGMT,
			      combo_mode_val) == usb) {
		regmap_update_bits(k3_phy->pmu, PMUA_PCIE_SUBSYS_MGMT,
				   combo_mode_mask, combo_mode_val);
		dev_info(k3_phy->dev, "Update Combo Mode %d to %s Mode\n",
			 combo_mode_val, usb ? "USB" : "PCIE");
	}
}

static void k3_usb3phy_update_status(struct regmap *regm)
{
	int ret;

	ret = regmap_update_bits(regm, PHY_PU_SEL,
				 CFG_STATUS | OVRD_STATUS,
				 OVRD_STATUS);
	if (ret != 0) {
		pr_err("regmap update PHY_PU_SEL failed, ret=%d\n", ret);
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

	regmap_update_bits(apb_spare, APB_SPARE_PU_CAL, PU_CAL,
			   PU_CAL);

	regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
			   R_CAL_OVRD_NTRIM_EN | R_CAL_OVRD_PTRIM_EN,
			   R_CAL_OVRD_NTRIM_EN | R_CAL_OVRD_PTRIM_EN);

	regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
			   R_CAL_OVRD_NTRIM_MASK | R_CAL_OVRD_PTRIM_MASK,
			   R_CAL_OVRD_NTRIM_VAL(NTRIM_DEFAULT) |
				   R_CAL_OVRD_PTRIM_VAL(PTRIM_DEFAULT));

	mdelay(100);

	/* Do not wait CDR lock before sampling data */
	regmap_update_bits(regm, PHY_RESET_CFG, EN_SAMPLE_DATA_AFTER_LOCK,
			   0);

	/* Power down 100MHz refclk buffer */
	regmap_update_bits(regm, PHY_PU_CK_REG, PU_REFCLK_100, 0);

	/* Program PLL REG1 configure the SSC */
	regmap_write(regm, PHY_PLL_REG1,
		     FIELD_PREP(SSC_MODE, SSC_DOWN_SPREAD1) |
			     FIELD_PREP(SSC_DEP_SEL, SSC_5000PPM) |
			     FIELD_PREP(FREF_SEL, FREF_24M));

	/* Un-select 100MHz PLL reference */
	regmap_update_bits(regm, PHY_PLL_REG2, SEL_REF100, 0);

	/* USB LFPS period configuration */
	regmap_update_bits(regm, PHY_MODE_CFG, CFG_LFPS_TPERIOD,
			   FIELD_PREP(CFG_LFPS_TPERIOD,
				      LFPS_TPERIOD_USB));

	/* Force AFE adaptation reset */
	regmap_update_bits(
		regm, PHY_ADPT_CFG0,
		AFE_ADPT_RST_OVRD_EN | AFE_ADPT_RST_OVRD_VAL,
		AFE_ADPT_RST_OVRD_EN | AFE_ADPT_RST_OVRD_VAL);
	/*
	 * Optional but commonly required for USB bring-up:
	 * bypass RX adaptation loop
	 */
	regmap_update_bits(regm, PHY_RX_REG2, RX_BYPASS_ADPT,
			   RX_BYPASS_ADPT);

	/*
	 * Inform PHY that all PLL-related configuration is done.
	 * PLL will not start locking until CFG_SW_INIT_DONE is set.
	 */
	regmap_write(regm, PHY_CLK_CFG,
		     CFG_SW_INIT_DONE |
			     FIELD_PREP(CFG_REFCLK_FREQ, REFCLK_24M) |
			     CFG_RXCLK_EN | CFG_PCLK_EN |
			     CFG_PIPE_PCLK_EN | CFG_TXCLK_EN |
			     CFG_TXCLK_INV);

	ret = regmap_read_poll_timeout(regm, PHY_CLK_CFG, reg,
				       (reg & PLL_READY), POLL_DELAY,
				       PLL_TIMEOUT);
	if (ret) {
		dev_err(&phy->dev, "PHY PLL polling Timeout!\n");
		return -ETIMEDOUT;
	}

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

	k3_usb3phy_combo_set_usb(k3_phy, true);

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
	int num_phy, ret;

	xlate = of_device_get_match_data(dev);

	k3_phy = devm_kzalloc(dev, sizeof(*k3_phy), GFP_KERNEL);
	if (!k3_phy)
		return -ENOMEM;

	k3_phy->is_combo = device_property_read_bool(dev, "combo-usb-bit");
	if (k3_phy->is_combo) {
		ret = device_property_read_u32(dev, "combo-usb-bit", &k3_phy->combo_sel_bit);
		if (ret || !(BIT(k3_phy->combo_sel_bit) & PU_MATRIX_CONF_USB_MASK))
			return dev_err_probe(dev, ret, "Wrong combo-usb-bit configuration");
	}

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
		if (IS_ERR(k3_phy->regmap_bases[i]))
			return dev_err_probe(dev, PTR_ERR(k3_phy->regmap_bases[i]),
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
