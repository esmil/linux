// SPDX-License-Identifier: GPL-2.0-only
/*
 * phy-k3-usb3.c - SpacemiT K3 USB3.0 PHY & Type-C Orientation Switch Driver
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

#include <linux/usb/typec.h>
#include <linux/usb/typec_mux.h>

#define MAX_NUM_PHY 2

#define PLL_TIMEOUT 500000 /* For PHY PLL lock (usec) */
#define PU_CAL_TIMEOUT 2000000
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
#define PU_CAL_DONE BIT(8)
#define R_CAL_OVRD_STABLE_EN BIT(31)
#define R_CAL_OVRD_STABLE_VAL BIT(30)
#define R_CAL_OVRD_NTRIM_EN BIT(29)
#define R_CAL_OVRD_PTRIM_EN BIT(28)
#define R_CAL_OVRD_TRIM_EN (R_CAL_OVRD_NTRIM_EN | R_CAL_OVRD_PTRIM_EN)
#define R_CAL_OVRD_NTRIM_MASK GENMASK(27, 24)
#define R_CAL_OVRD_NTRIM_VAL(val) FIELD_PREP(R_CAL_OVRD_NTRIM_MASK, val)
#define NTRIM_DEFAULT 0x6
#define R_CAL_OVRD_PTRIM_MASK GENMASK(23, 20)
#define R_CAL_OVRD_PTRIM_VAL(val) FIELD_PREP(R_CAL_OVRD_PTRIM_MASK, val)
#define PTRIM_DEFAULT 0xa
#define R_CAL_OVRD_TRIM_MASK (R_CAL_OVRD_NTRIM_MASK | R_CAL_OVRD_PTRIM_MASK)

/* PHY Registers */
#define PHY_VERSION 0x0

#define PHY_RESET_CFG 0x04
#define EN_SAMPLE_DATA_AFTER_LOCK BIT(6)

#define PHY_CLK_CFG 0x08
#define PLL_READY BIT(0)
#define CFG_RXCLK_EN BIT(3)
#define CFG_TXCLK_EN BIT(4)
#define CFG_PCLK_EN BIT(5)
#define CFG_PIPE_PCLK_EN BIT(6)
#define CFG_REFCLK_FREQ GENMASK(10, 7)
#define REFCLK_24M 0x2
#define CFG_SW_INIT_DONE BIT(11)
#define CFG_PU_SSC_OUT BIT(23)

#define PCIE_PHY_OVERRIDE 0x18
#define OVRD_MPU_U3 BIT(17)
#define CFG_MPU_U3 BIT(16)

#define PHY_MODE_CFG 0x0C
#define CFG_LFPS_RX_FILTER_EN BIT(11)
#define CFG_LFPS_TPERIOD GENMASK(9, 8)
#define LFPS_TPERIOD_USB 0x3

#define PHY_PU_SEL 0x40
#define OVRD_STATUS BIT(10)
#define CFG_STATUS BIT(9)

#define PHY_PU_CK_REG 0x54
#define PU_REFCLK_100 BIT(25)

#define PHY_PLL_REG1 0x58
#define FREF_SEL GENMASK(15, 13)
#define FREF_24M 0x1
#define SSC_DEP_SEL GENMASK(27, 24)
#define SSC_5000PPM 0xa
#define SSC_MODE GENMASK(29, 28)
#define SSC_CENTER_SPREAD 0x0
#define SSC_UP_SPREAD 0x1
#define SSC_DOWN_SPREAD 0x2
#define SSC_DOWN_SPREAD1 0x3

#define PHY_PLL_REG2 0x5c
#define SEL_REF100 BIT(21)

/* PHY RX Register Definitions */
#define PHY_RX_REG_A 0x60
#define RX_REG3_MASK GENMASK(31, 24)
#define RX_REG3_RDEG1(n) FIELD_PREP(GENMASK(31, 30), (n))
#define RX_REG3_RDEG1_DEFAULT 0x3
#define RX_REG3_ADJ_BIAS(n) FIELD_PREP(GENMASK(29, 28), (n))
#define RX_REG3_ADJ_BIAS_DEFAULT 0x1
#define RX_REG3_SEL_CBOOST_CODE BIT(27)
#define RX_REG3_I_LOAD_REG(n) FIELD_PREP(GENMASK(26, 24), (n))
#define RX_REG3_I_LOAD_REG_DEFAULT 0x7
#define RX_REG2_MASK GENMASK(23, 16)
#define RX_REG2_PSEL(n) FIELD_PREP(GENMASK(23, 21), (n))
#define RX_REG2_PSEL_DEFAULT 0x4
#define RX_REG2_FORCE_CSEL BIT(20)
#define RX_REG2_CSEL(n) FIELD_PREP(GENMASK(19, 16), (n))
#define RX_REG2_CSEL_DEFAULT 0x8
#define RX_REG1_MASK GENMASK(15, 8)
#define RX_REG1_RC_CALI_REG(n) FIELD_PREP(GENMASK(15, 12), (n))
#define RX_REG1_RC_CALI_REG_DEFAULT 0x7
#define RX_REG1_RTERM_REG(n) FIELD_PREP(GENMASK(11, 8), (n))
#define RX_REG1_RTERM_REG_DEFAULT 0x8
#define RX_REG0_MASK GENMASK(7, 0)
#define RX_REG0_RLOAD BIT(4)

#define PHY_RX_REG_B 0x64
#define RX_REG6_MASK GENMASK(23, 16)
#define RX_REG6_BYPASS_ADPT BIT(22)
#define RX_REG6_ADAPT_GAIN(n) FIELD_PREP(GENMASK(21, 20), (n))
#define RX_REG6_ADAPT_GAIN_DEFAULT 0x2
#define RX_REG6_H1_REG(n) FIELD_PREP(GENMASK(19, 16), (n))
#define RX_REG6_H1_REG_DEFAULT 0x8
#define RX_REG5_MASK GENMASK(15, 8)
#define RX_REG5_RCELL_BIAS(n) FIELD_PREP(GENMASK(15, 12), (n))
#define RX_REG5_RCELL_BIAS_DEFAULT 0x8
#define RX_REG5_RCELL_VCM(n) FIELD_PREP(GENMASK(11, 8), (n))
#define RX_REG5_RCELL_VCM_DEFAULT 0x8
#define RX_REG4_MASK GENMASK(7, 0)
#define RX_REG4_MANUAL_CFG BIT(7)
#define RX_REG4_RTERM_SEL BIT(5)
#define RX_REG4_ENVOS BIT(4)
#define RX_REG4_RDEG2(n) FIELD_PREP(GENMASK(2, 1), (n))
#define RX_REG4_RDEG2_DEFAULT 0x2

#define PHY_RXEQ_TIME 0xb4
#define RXEQ_TIME_OVRD_AMP_SOC BIT(24)
#define RXEQ_TIME_CFG_AMP_SOC(n) FIELD_PREP(GENMASK(23, 22), (n))
#define AMP_SOC_650M 0x0
#define AMP_SOC_800M 0x1
#define AMP_SOC_870M 0x2
#define AMP_SOC_900M 0x3
#define OVRD_POST_C_SOC BIT(21)
#define CFG_POST_C_SOC(n) FIELD_PREP(GENMASK(20, 19), (n))
#define OVRD_PRE_C_SOC BIT(18)
#define CFG_PRE_C_SOC(n) FIELD_PREP(GENMASK(17, 16), (n))
#define CFG_RXEQ_TIMEOUT(n) FIELD_PREP(GENMASK(15, 0), (n))

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

	/* For USB only */
	bool nop;
	bool orientation_flip;
	struct typec_switch_dev *sw;
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

	regmap_update_bits(regm, PCIE_PHY_OVERRIDE,
			   OVRD_MPU_U3 | CFG_MPU_U3, 0);

	ret = regmap_read(regm, PHY_CLK_CFG, &reg);
	if (ret)
		return ret;

	if (reg & CFG_SW_INIT_DONE) {
		dev_dbg(&phy->dev, "PHY already initialized, skip init\n");
		return 0;
	}

	ret = regmap_read(regm, PHY_VERSION, &version);
	if (ret)
		return ret;

	regmap_update_bits(apb_spare, APB_SPARE_PU_CAL, PU_CAL,
			   PU_CAL);

	ret = regmap_read_poll_timeout(apb_spare, APB_SPARE_RCAL_HSIO,
				       reg, (reg & PU_CAL_DONE), POLL_DELAY,
				       PU_CAL_TIMEOUT);
	if (ret) {
		dev_warn(k3_phy->dev, "PU PHY RCAL timeout, trim override\n");

		regmap_update_bits(apb_spare, APB_SPARE_RCAL_HSIO,
				   R_CAL_OVRD_TRIM_EN | R_CAL_OVRD_STABLE_VAL |
				   R_CAL_OVRD_TRIM_MASK,
				   R_CAL_OVRD_TRIM_EN | R_CAL_OVRD_STABLE_VAL |
				   R_CAL_OVRD_NTRIM_VAL(NTRIM_DEFAULT) |
				   R_CAL_OVRD_PTRIM_VAL(PTRIM_DEFAULT) );
		regmap_set_bits(apb_spare, APB_SPARE_RCAL_HSIO, R_CAL_OVRD_STABLE_EN);
	}

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

	/* Override driver amplitude value to 900m */
	regmap_set_bits(regm, PHY_RXEQ_TIME,
			RXEQ_TIME_OVRD_AMP_SOC | RXEQ_TIME_CFG_AMP_SOC(AMP_SOC_900M));

	/* Configure RX parameters */
	regmap_update_bits(regm, PHY_RX_REG_A, RX_REG0_MASK, RX_REG0_RLOAD);
	regmap_update_bits(regm, PHY_RX_REG_A, RX_REG1_MASK,
			   RX_REG1_RC_CALI_REG(RX_REG1_RC_CALI_REG_DEFAULT) |
			   RX_REG1_RTERM_REG(RX_REG1_RTERM_REG_DEFAULT));
	regmap_update_bits(regm, PHY_RX_REG_A, RX_REG2_MASK,
			   RX_REG2_PSEL(RX_REG2_PSEL_DEFAULT) | RX_REG2_FORCE_CSEL |
			   RX_REG2_CSEL(RX_REG2_CSEL_DEFAULT));
	regmap_update_bits(regm, PHY_RX_REG_A, RX_REG3_MASK,
			   RX_REG3_RDEG1(RX_REG3_RDEG1_DEFAULT) |
			   RX_REG3_ADJ_BIAS(RX_REG3_ADJ_BIAS_DEFAULT) |
			   RX_REG3_SEL_CBOOST_CODE |
			   RX_REG3_I_LOAD_REG(RX_REG3_I_LOAD_REG_DEFAULT));
	regmap_update_bits(regm, PHY_RX_REG_B, RX_REG4_MASK,
			   RX_REG4_MANUAL_CFG | RX_REG4_RTERM_SEL | RX_REG4_ENVOS |
			   RX_REG4_RDEG2(RX_REG4_RDEG2_DEFAULT));
	regmap_update_bits(regm, PHY_RX_REG_B, RX_REG5_MASK,
			   RX_REG5_RCELL_BIAS(RX_REG5_RCELL_BIAS_DEFAULT) |
			   RX_REG5_RCELL_VCM(RX_REG5_RCELL_VCM_DEFAULT));
	regmap_update_bits(regm, PHY_RX_REG_B, RX_REG6_MASK,
			   RX_REG6_ADAPT_GAIN(RX_REG6_ADAPT_GAIN_DEFAULT) |
			   RX_REG6_H1_REG(RX_REG6_H1_REG_DEFAULT));
	dev_info(&phy->dev, "PUPHY Rx Reg Configured\n");

	/*
	 * Inform PHY that all PLL-related configuration is done.
	 * PLL will not start locking until CFG_SW_INIT_DONE is set.
	 */
	regmap_write(regm, PHY_CLK_CFG,
		     CFG_SW_INIT_DONE |
			     CFG_PU_SSC_OUT |
			     FIELD_PREP(CFG_REFCLK_FREQ, REFCLK_24M) |
			     CFG_RXCLK_EN | CFG_PCLK_EN |
			     CFG_PIPE_PCLK_EN | CFG_TXCLK_EN);

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

static int k3_usb3phy_power_on(struct phy *phy)
{
	struct k3_usb3phy *k3_phy = phy_get_drvdata(phy);

	if (k3_phy->nop)
		return 0;

	/* RX Filter requires soc 24M, which is disabled during system sleep */
	for (int i = 0; i < MAX_NUM_PHY && k3_phy->regmap_bases[i]; i++)
		regmap_set_bits(k3_phy->regmap_bases[i], PHY_MODE_CFG,
				CFG_LFPS_RX_FILTER_EN);

	return 0;
}

static int k3_usb3phy_power_off(struct phy *phy)
{
	struct k3_usb3phy *k3_phy = phy_get_drvdata(phy);

	if (k3_phy->nop)
		return 0;

	for (int i = 0; i < MAX_NUM_PHY && k3_phy->regmap_bases[i]; i++)
		regmap_clear_bits(k3_phy->regmap_bases[i], PHY_MODE_CFG,
				  CFG_LFPS_RX_FILTER_EN);

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
	struct k3_usb3phy *k3_phy = phy_get_drvdata(phy);
	int ret;

	if (k3_phy->nop)
		return 0;

	/* Completely shutdown the phy */
	for (int i = 0; i < MAX_NUM_PHY; i++) {
		if (!k3_phy->regmap_bases[i])
			continue;

		ret = regmap_update_bits(k3_phy->regmap_bases[i], PHY_CLK_CFG,
					 CFG_SW_INIT_DONE, 0);
		if (ret)
			return ret;

		ret = regmap_update_bits(k3_phy->regmap_bases[i], PCIE_PHY_OVERRIDE,
					 OVRD_MPU_U3 | CFG_MPU_U3, OVRD_MPU_U3);
		if (ret)
			return ret;
	}

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
	.power_on = k3_usb3phy_power_on,
	.power_off = k3_usb3phy_power_off,
	.set_speed = k3_usb3phy_set_speed,
	.owner = THIS_MODULE,
};

#if IS_ENABLED(CONFIG_TYPEC)
static int k3_usb3phy_switch_set(struct typec_switch_dev *sw,
				 enum typec_orientation orientation)
{
	struct k3_usb3phy *k3_phy = typec_switch_get_drvdata(sw);
	bool val;

	val = orientation == TYPEC_ORIENTATION_REVERSE;
	if (k3_phy->orientation_flip)
		val = !val;

	if (regmap_test_bits(k3_phy->pmu, PMUA_TYPEC_CTRL,
			     TYPEC_ORIENT_OVRD | TYPEC_ORIENT_OVRD_EN) != val)
		regmap_update_bits(
			k3_phy->pmu, PMUA_TYPEC_CTRL,
			TYPEC_ORIENT_OVRD | TYPEC_ORIENT_OVRD_EN,
			val ? TYPEC_ORIENT_OVRD | TYPEC_ORIENT_OVRD_EN : 0);

	dev_dbg(k3_phy->dev, "Override orientation with %d\n", val);
	return 0;
}

static void k3_usb3_typec_unregister(void *data)
{
	struct k3_usb3phy *k3_phy = data;

	typec_switch_unregister(k3_phy->sw);
}

static int k3_usb3_typec_register(struct k3_usb3phy *k3_phy)
{
	struct typec_switch_desc sw_desc = {};
	struct device *dev = k3_phy->dev;

	sw_desc.drvdata = k3_phy;
	sw_desc.name = dev_name(dev);
	sw_desc.fwnode = dev_fwnode(dev);
	sw_desc.set = k3_usb3phy_switch_set;
	k3_phy->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(k3_phy->sw)) {
		dev_err(dev, "Unable to register typec switch: %pe\n",
			k3_phy->sw);
		return PTR_ERR(k3_phy->sw);
	}

	dev_info(dev, "Using orientation-switch mode, flip: %d\n",
		 k3_phy->orientation_flip);

	return devm_add_action_or_reset(dev, k3_usb3_typec_unregister, k3_phy);
}
#else
static int k3_usb3_typec_register(struct k3_usb3phy *k3_phy)
{
	return 0;
}
#endif

static int k3_usb3phy_switch_init(struct k3_usb3phy *k3_phy)
{
	struct device *dev = k3_phy->dev;
	bool flip, override;
	u32 val = 0;

	if (!device_is_compatible(dev, "spacemit,k3-typec-switch"))
		return 0;

	flip = device_property_read_bool(dev, "orientation-flip");
	override = device_property_read_bool(dev, "orientation-switch");
	if (flip && !override)
		regmap_update_bits(k3_phy->pmu, PMUA_TYPEC_CTRL,
				   TYPEC_ORIENT_FLIP, val);

	if (!override)
		return 0;

	k3_phy->orientation_flip = flip;
	return k3_usb3_typec_register(k3_phy);
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

	return k3_usb3phy_switch_init(k3_phy);
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
