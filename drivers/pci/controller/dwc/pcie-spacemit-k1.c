// SPDX-License-Identifier: GPL-2.0
/*
 * SpacemiT K1 PCIe host driver
 *
 * Copyright (C) 2025 by RISCstar Solutions Corporation.  All rights reserved.
 * Copyright (c) 2023, spacemit Corporation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/types.h>

#include "pcie-designware.h"
#include "../../pci.h"

#define PCI_VENDOR_ID_SPACEMIT		0x201f
#define PCI_DEVICE_ID_SPACEMIT_K1	0x0001
#define PCI_DEVICE_ID_SPACEMIT_K3	0x0002

/* Offsets and field definitions for link management registers */
#define K1_PHY_AHB_IRQ_EN			0x0000
#define PCIE_INTERRUPT_EN		BIT(0)

#define K1_PHY_AHB_LINK_STS			0x0004
#define SMLH_LINK_UP			BIT(1)
#define RDLH_LINK_UP			BIT(12)

#define INTR_STATUS				0x0010

#define INTR_ENABLE				0x0014
#define MSI_CTRL_INT			BIT(11)
#define RDLH_LINK_UP_INT		BIT(20)

/* Some controls require APMU regmap access */
#define SYSCON_APMU			"spacemit,apmu"

/* Offsets and field definitions for APMU registers */
#define PCIE_CLK_RESET_CONTROL			0x0000
#define LTSSM_EN			BIT(6)
#define PCIE_AUX_PWR_DET		BIT(9)
#define PCIE_RC_PERST			BIT(12)	/* 1: assert PERST# */
#define APP_HOLD_PHY_RST		BIT(30)
#define DEVICE_TYPE_RC			BIT(31)	/* 0: endpoint; 1: RC */

#define PCIE_CONTROL_LOGIC			0x0004
#define PCIE_SOFT_RESET			BIT(0)

#ifdef CONFIG_SOC_SPACEMIT_K3
#define PCIE_PERSTN_OE			BIT(24)
#define PCIE_PERSTN_OUT			BIT(25)
#define PCIE_IGNORE_PERSTN		BIT(31)

#define SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX	0x000c
#define LEG_EP_INTERRUPTS (BIT(6) | BIT(7) | BIT(8) | BIT(9))

#define SPACEMIT_PHY_AHB_IRQENABLE_SET_MSI	0x0014
/* MSI defined as BIT(11) in existing INTR_ENABLE, reusing */

#define ADDR_INTR_ENABLE1		0x001C
#define MSI_INT			BIT(0)
#define MSIX_INT			GENMASK(8, 1)

/* Coherency control DBI register (same as K3 PCIe driver) */
#define COHERENCY_CONTROL_3_OFF		0x8E8

/* PMU / APB registers for Lane Muxing */
#define PMUA_PCIE_SUBSYS_MGMT		0x1d8

/* Port A Modes */
#define PORTA_MODE_MASK			(BIT(4) | BIT(3))
#define PORTA_MODE_X8			(0)			/* [4:3] = 00b */
#define PORTA_MODE_X4			(BIT(4))		/* [4:3] = 10b */
#define PORTA_MODE_X2_PORTB_X2		(BIT(4) | BIT(3))	/* [4:3] = 11b */

/* Port C Modes */
#define PORTC_LANE_MASK			(BIT(4) | GENMASK(2, 1))
#define PORTC_MODE_X2			(0)			/* [2:1] = 00b */
#define PORTC_MODE_X1_PHY2		(BIT(1))		/* [2:1] = 01b */
#define PORTC_MODE_X1_PHY3		(BIT(2))		/* [2:1] = 10b */

/* Port D Modes */
#define PORTD_LANE_MASK			(BIT(4) | BIT(0))
#define PORTD_MODE_PCIE			(0)
#define PORTD_MODE_USB			BIT(0)

#define MAX_PHYS 6
#endif

struct k1_pcie {
	struct dw_pcie pci;
#ifdef CONFIG_SOC_SPACEMIT_K3
	struct phy		*phys[MAX_PHYS];	/* multiple PHYs (from 'phys' property) */
	int			phy_count;		/* number of valid entries in phys[] */
	int			num_lanes;
	struct gpio_desc *detect_gpiod;
	int			port_id;
#else
	struct phy *phy;
#endif
	void __iomem *link;
	struct regmap *pmu;	/* Errors ignored; MMIO-backed regmap */
	u32 pmu_off;
};

#ifdef CONFIG_SOC_SPACEMIT_K3

#if IS_ENABLED(CONFIG_PHY_SPACEMIT_K3_PCIE)
bool spacemit_k3_pcie_phy_is_busy(struct phy *phy);
#else
static inline bool spacemit_k3_pcie_phy_is_busy(struct phy *phy)
{
	return false;
}
#endif

static int spacemit_pcie_check_phy_busy(struct k1_pcie *pcie)
{
	int i;

	for (i = 0; i < pcie->phy_count; i++) {
		if (spacemit_k3_pcie_phy_is_busy(pcie->phys[i])) {
			dev_err(pcie->pci.dev, "PHY %d is busy\n", i);
			return -EBUSY;
		}
	}

	return 0;
}

static int spacemit_pcie_config_lane_mux(struct k1_pcie *pcie)
{
	u32 mask = 0, val = 0;
	int ret;

	ret = spacemit_pcie_check_phy_busy(pcie);
	if (ret)
		return ret;

	if (!pcie->pmu) {
		dev_warn(pcie->pci.dev, "PMU regmap not found, lane mux skipped\n");
		return 0;
	}

	switch (pcie->port_id) {
	case 0: /* Port A */
		mask = PORTA_MODE_MASK;
		if (pcie->num_lanes == 8)
			val = PORTA_MODE_X8;
		else if (pcie->num_lanes == 4)
			val = PORTA_MODE_X4;
		else
			val = PORTA_MODE_X2_PORTB_X2;
		break;
	case 1: /* Port B */
		mask = PORTA_MODE_MASK;
		val = PORTA_MODE_X2_PORTB_X2;
		break;
	case 2: /* Port C */
		mask = PORTC_LANE_MASK;
		if (pcie->num_lanes >= 2) {
			val = PORTC_MODE_X2; /* 00b: x2 (PHY2 + PHY3) */
		} else {
			int phy_id = -1;
			struct device *dev = pcie->pci.dev;
			struct device_node *np = dev->of_node;
			struct device_node *phy_np;

			if (pcie->phy_count > 0 && pcie->phys[0]) {
				phy_np = of_parse_phandle(np, "phys", 0);
				if (phy_np) {
					if (!of_property_read_u32(phy_np, "spacemit,phy-id",
								  &phy_id))
						of_node_put(phy_np);
				}
			}

			if (phy_id == 3)
				val = PORTC_MODE_X1_PHY3; /* 10b: Use PHY3 */
			else
				val = PORTC_MODE_X1_PHY2; /* 01b: Use PHY2 */
		}
		val |= PORTA_MODE_X4;
		break;
	case 3: /* Port D */
		mask = PORTD_LANE_MASK;
		val = PORTD_MODE_PCIE;
		val |= PORTA_MODE_X4;
		break;
	case 4: /* Port E */
		return 0;
	default:
		return 0;
	}

	return regmap_update_bits(pcie->pmu, PMUA_PCIE_SUBSYS_MGMT, mask, val);
}

static void spacemit_pcie_eq_preset(struct k1_pcie *pcie)
{
	struct dw_pcie *pci = &pcie->pci;
	u32 val;

	val = dw_pcie_readl_dbi(pci, GEN3_EQ_CONTROL_OFF);
	val &= ~(0xffff << 8);
	val |= ((0x1 << 4) << 8);
	dw_pcie_writel_dbi(pci, GEN3_EQ_CONTROL_OFF, val);
}

static int spacemit_pcie_msi_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	u32 val;

	dw_pcie_dbi_ro_wr_en(pci);

	val = dw_pcie_readl_dbi(pci, COHERENCY_CONTROL_3_OFF);
	val |= (0xf << 11);
	dw_pcie_writel_dbi(pci, COHERENCY_CONTROL_3_OFF, val);

	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}
#endif

#define to_k1_pcie(dw_pcie) \
		platform_get_drvdata(to_platform_device((dw_pcie)->dev))

static void k1_pcie_toggle_soft_reset(struct k1_pcie *k1)
{
	u32 offset;
	u32 val;

	/*
	 * Write, then read back to guarantee it has reached the device
	 * before we start the delay.
	 */
	offset = k1->pmu_off + PCIE_CONTROL_LOGIC;
	regmap_set_bits(k1->pmu, offset, PCIE_SOFT_RESET);
	regmap_read(k1->pmu, offset, &val);

	mdelay(2);

	regmap_clear_bits(k1->pmu, offset, PCIE_SOFT_RESET);
}

/* Enable app clocks, deassert resets */
static int k1_pcie_enable_resources(struct k1_pcie *k1)
{
	struct dw_pcie *pci = &k1->pci;
	int ret;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(pci->app_clks), pci->app_clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(ARRAY_SIZE(pci->app_rsts),
					  pci->app_rsts);
	if (ret)
		goto err_disable_clks;

	return 0;

err_disable_clks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(pci->app_clks), pci->app_clks);

	return ret;
}

/* Assert resets, disable app clocks */
static void k1_pcie_disable_resources(struct k1_pcie *k1)
{
	struct dw_pcie *pci = &k1->pci;

	reset_control_bulk_assert(ARRAY_SIZE(pci->app_rsts), pci->app_rsts);
	clk_bulk_disable_unprepare(ARRAY_SIZE(pci->app_clks), pci->app_clks);
}

/* Disable ASPM L1 to avoid errors reported on some NVMe drives */
static void k1_pcie_disable_aspm_l1(struct k1_pcie *k1)
{
	struct dw_pcie *pci = &k1->pci;
	u8 offset;
	u32 val;

	offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);
	offset += PCI_EXP_LNKCAP;

	/* Turn off ASPM L1 for the link */
	dw_pcie_dbi_ro_wr_en(pci);
	val = dw_pcie_readl_dbi(pci, offset);
	val &= ~PCI_EXP_LNKCAP_ASPM_L1;
	dw_pcie_writel_dbi(pci, offset, val);
	dw_pcie_dbi_ro_wr_dis(pci);
}

#ifdef CONFIG_SOC_SPACEMIT_K3
static int spacemit_pcie_enable_phy(struct k1_pcie *pcie)
{
	int i, ret;

	for (i = 0; i < pcie->phy_count; i++) {
		ret = phy_init(pcie->phys[i]);
		if (ret)
			goto err_phy;
	}

	return 0;

err_phy:
	while (--i >= 0)
		phy_exit(pcie->phys[i]);

	return ret;
}

static void spacemit_pcie_disable_phy(struct k1_pcie *pcie)
{
	int i;

	for (i = 0; i < pcie->phy_count; i++)
		phy_exit(pcie->phys[i]);
}
#endif

static irqreturn_t spacemit_pcie_irq_thread(int irq, void *data)
{
	struct k1_pcie *k1 = data;
	struct dw_pcie_rp *pp = &k1->pci.pp;
	struct device *dev = k1->pci.dev;
	u32 status;

	status = readl_relaxed(k1->link + INTR_STATUS);
	writel_relaxed(status, k1->link + INTR_STATUS);

	if (FIELD_GET(RDLH_LINK_UP_INT, status)) {
		msleep(PCIE_RESET_CONFIG_WAIT_MS);
		dev_dbg(dev, "Received Link up event. Starting enumeration!\n");
		/* Rescan the bus to enumerate endpoint devices */
		pci_lock_rescan_remove();
		pci_rescan_bus(pp->bridge->bus);
		pci_unlock_rescan_remove();
	} else {
		dev_WARN_ONCE(dev, 1, "Received unknown event. INT_STATUS: 0x%08x\n",
			      status);
	}

	return IRQ_HANDLED;
}

static int k1_pcie_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 reset_ctrl;
	int ret;
	u32 val;

	k1_pcie_toggle_soft_reset(k1);

	ret = k1_pcie_enable_resources(k1);
	if (ret)
		return ret;

	/*
	 * Start by asserting fundamental reset (drive PERST# low).  The
	 * PCI CEM spec says that PERST# should be deasserted at least
	 * 100ms after the power becomes stable, so we'll insert that
	 * delay first.  Write, then read it back to guarantee the write
	 * reaches the device before we start the delay.
	 */
	reset_ctrl = k1->pmu_off + PCIE_CLK_RESET_CONTROL;
#ifdef CONFIG_SOC_SPACEMIT_K3
	/* K3: Set IGNORE_PERSTN and drive PERSTN_OE high (assert reset) */
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CONTROL_LOGIC,
			   PCIE_IGNORE_PERSTN | PCIE_PERSTN_OE | PCIE_PERSTN_OUT,
			   PCIE_IGNORE_PERSTN | PCIE_PERSTN_OE | PCIE_PERSTN_OUT);
	usleep_range(1000, 2000);
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CONTROL_LOGIC, PCIE_PERSTN_OUT, 0);
#else
	regmap_set_bits(k1->pmu, reset_ctrl, PCIE_RC_PERST);
	regmap_read(k1->pmu, reset_ctrl, &val);
#endif
	mdelay(PCIE_T_PVPERL_MS);

	/*
	 * Put the controller in root complex mode, and indicate that
	 * Vaux (3.3v) is present.
	 */
#ifdef CONFIG_SOC_SPACEMIT_K3
	regmap_set_bits(k1->pmu, reset_ctrl, PCIE_AUX_PWR_DET);

	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CONTROL_LOGIC,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE);
	regmap_update_bits(k1->pmu, reset_ctrl, APP_HOLD_PHY_RST, 0);

	ret = spacemit_pcie_config_lane_mux(k1);
	if (ret)
		return ret;

	ret = spacemit_pcie_enable_phy(k1);
	if (ret)
		return ret;

	spacemit_pcie_eq_preset(k1);
#else
	regmap_set_bits(k1->pmu, reset_ctrl, DEVICE_TYPE_RC | PCIE_AUX_PWR_DET);

	ret = phy_init(k1->phy);
	if (ret) {
		k1_pcie_disable_resources(k1);

		return ret;
	}
#endif

	/* Set the PCI vendor and device ID */
	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, PCI_VENDOR_ID_SPACEMIT);
#ifdef CONFIG_SOC_SPACEMIT_K3
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, PCI_DEVICE_ID_SPACEMIT_K3);
#else
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, PCI_DEVICE_ID_SPACEMIT_K1);
#endif
	dw_pcie_dbi_ro_wr_dis(pci);

	/* Deassert fundamental reset (drive PERST# high) */
#ifndef CONFIG_SOC_SPACEMIT_K3
	regmap_clear_bits(k1->pmu, reset_ctrl, PCIE_RC_PERST);
#endif

	/* Finally, as a workaround, disable ASPM L1 */
	k1_pcie_disable_aspm_l1(k1);

	return 0;
}

static void k1_pcie_deinit(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct k1_pcie *k1 = to_k1_pcie(pci);

	/* Assert fundamental reset (drive PERST# low) */
	regmap_set_bits(k1->pmu, k1->pmu_off + PCIE_CLK_RESET_CONTROL,
			PCIE_RC_PERST);

#ifdef CONFIG_SOC_SPACEMIT_K3
	spacemit_pcie_disable_phy(k1);
#else
	phy_exit(k1->phy);
#endif

	k1_pcie_disable_resources(k1);
}

static const struct dw_pcie_host_ops k1_pcie_host_ops = {
	.init		= k1_pcie_init,
	.deinit		= k1_pcie_deinit,
#ifdef CONFIG_SOC_SPACEMIT_K3
	.msi_init	= spacemit_pcie_msi_host_init,
#endif
};

static bool k1_pcie_link_up(struct dw_pcie *pci)
{
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 val;

	val = readl_relaxed(k1->link + K1_PHY_AHB_LINK_STS);

	return (val & RDLH_LINK_UP) && (val & SMLH_LINK_UP);
}

static int k1_pcie_start_link(struct dw_pcie *pci)
{
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 val;

	/* Stop holding the PHY in reset, and enable link training */
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CLK_RESET_CONTROL,
			   APP_HOLD_PHY_RST | LTSSM_EN, LTSSM_EN);

	/* Enable the MSI interrupt */
	writel_relaxed(MSI_CTRL_INT, k1->link + INTR_ENABLE);

	/* Top-level interrupt enable */
	val = readl_relaxed(k1->link + K1_PHY_AHB_IRQ_EN);
	val |= PCIE_INTERRUPT_EN;
	writel_relaxed(val, k1->link + K1_PHY_AHB_IRQ_EN);

	/* Link Up Interrupt Enable */
	val = readl_relaxed(k1->link + INTR_ENABLE);
	val |= RDLH_LINK_UP_INT;
	writel_relaxed(val, k1->link + INTR_ENABLE);

#ifdef CONFIG_SOC_SPACEMIT_K3
	/* Enable INTx */
	val = readl_relaxed(k1->link + SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX);
	val |= LEG_EP_INTERRUPTS;
	writel_relaxed(val, k1->link + SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX);

	/* Enable MSI/MSIX specific to K3 */
	val = readl_relaxed(k1->link + ADDR_INTR_ENABLE1);
	val |= (MSI_INT | MSIX_INT);
	writel_relaxed(val, k1->link + ADDR_INTR_ENABLE1);
#endif

	return 0;
}

static void k1_pcie_stop_link(struct dw_pcie *pci)
{
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 val;

	/* Disable interrupts */
	val = readl_relaxed(k1->link + K1_PHY_AHB_IRQ_EN);
	val &= ~PCIE_INTERRUPT_EN;
	writel_relaxed(val, k1->link + K1_PHY_AHB_IRQ_EN);

	writel_relaxed(0, k1->link + INTR_ENABLE);

	/* Disable the link and hold the PHY in reset */
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CLK_RESET_CONTROL,
			   APP_HOLD_PHY_RST | LTSSM_EN, APP_HOLD_PHY_RST);
}

static const struct dw_pcie_ops k1_pcie_ops = {
	.link_up	= k1_pcie_link_up,
	.start_link	= k1_pcie_start_link,
	.stop_link	= k1_pcie_stop_link,
};

static int k1_pcie_parse_port(struct k1_pcie *k1)
{
#ifdef CONFIG_SOC_SPACEMIT_K3
	struct device *dev = k1->pci.dev;
	struct device_node *np = dev->of_node;
	u32 val;
	int ret;

	if (!of_property_read_u32(np, "spacemit,pcie-port", &val))
		k1->port_id = val;
	else
		k1->port_id = 0;

	if (!of_property_read_u32(np, "num-lanes", &val) &&
	    val >= 1 && val <= 8) {
		k1->num_lanes = val;
	} else {
		dev_warn(dev,
			 "num-lanes property not provided or invalid, setting num-lanes to 1\n");
		k1->num_lanes = 1;
	}

	ret = of_count_phandle_with_args(np, "phys", "#phy-cells");
	if (ret > 0) {
		k1->phy_count = ret;
		if (k1->phy_count > MAX_PHYS) {
			dev_warn(dev, "Too many PHYs (%d), limiting to %d\n",
				 k1->phy_count, MAX_PHYS);
			k1->phy_count = MAX_PHYS;
		}

		k1->detect_gpiod = devm_gpiod_get_optional(dev, "spacemit,device-detect", GPIOD_IN);
		if (IS_ERR(k1->detect_gpiod))
			return dev_err_probe(dev, PTR_ERR(k1->detect_gpiod),
					"failed to get detect gpio\n");

		if (k1->port_id == 0) {
			if (k1->detect_gpiod && gpiod_get_value(k1->detect_gpiod)) {
				dev_info(dev, "Port B device detected, degrading Port A to x2 mode\n");
				k1->phy_count = 1;
				k1->num_lanes = 2;
			}
		}

		for (int i = 0; i < k1->phy_count; i++) {
			k1->phys[i] = devm_of_phy_get_by_index(dev, np, i);
			if (IS_ERR(k1->phys[i]))
				return dev_err_probe(dev, PTR_ERR(k1->phys[i]),
						     "failed to get PCIe PHY %d\n", i);
		}
	}
#else
	struct device *dev = k1->pci.dev;
	struct device_node *root_port;
	struct phy *phy;

	/* We assume only one root port */
	root_port = of_get_next_available_child(dev_of_node(dev), NULL);
	if (!root_port)
		return -EINVAL;

	phy = devm_of_phy_get(dev, root_port, NULL);

	of_node_put(root_port);

	if (IS_ERR(phy))
		return PTR_ERR(phy);

	k1->phy = phy;
#endif
	return 0;
}

static int k1_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct k1_pcie *k1;
	struct dw_pcie_rp *pp;
	int ret, irq;
	char *name;

	k1 = devm_kzalloc(dev, sizeof(*k1), GFP_KERNEL);
	if (!k1)
		return -ENOMEM;

	k1->pmu = syscon_regmap_lookup_by_phandle_args(dev_of_node(dev),
						       SYSCON_APMU, 1,
						       &k1->pmu_off);
	if (IS_ERR(k1->pmu))
		return dev_err_probe(dev, PTR_ERR(k1->pmu),
				     "failed to lookup PMU registers\n");

	k1->link = devm_platform_ioremap_resource_byname(pdev, "link");
	if (IS_ERR(k1->link))
		return dev_err_probe(dev, PTR_ERR(k1->link),
				     "failed to map \"link\" registers\n");

	k1->pci.dev = dev;
	k1->pci.ops = &k1_pcie_ops;
	k1->pci.pp.num_vectors = MAX_MSI_IRQS;
	dw_pcie_cap_set(&k1->pci, REQ_RES);

	k1->pci.pp.ops = &k1_pcie_host_ops;
	pp = &k1->pci.pp;

	/* Hold the PHY in reset until we start the link */
	regmap_set_bits(k1->pmu, k1->pmu_off + PCIE_CLK_RESET_CONTROL,
			APP_HOLD_PHY_RST);

	ret = devm_regulator_get_enable_optional(dev, "vpcie3v3");
	if (ret) {
		if (ret != -ENODEV)
			return dev_err_probe(dev, ret,
					     "failed to get \"vpcie3v3\" supply\n");
	}

	pm_runtime_set_active(dev);
	pm_runtime_no_callbacks(dev);
	devm_pm_runtime_enable(dev);

	platform_set_drvdata(pdev, k1);

	ret = k1_pcie_parse_port(k1);
	if (ret) {
		dev_err_probe(dev, ret, "failed to parse port\n");
		goto err_pm_runtime_put;
	}

	irq = platform_get_irq_byname_optional(pdev, "pcie_irq");
	if (irq > 0)
		pp->use_linkup_irq = true;

	ret = dw_pcie_host_init(&k1->pci.pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		goto err_pm_runtime_put;
	}

	name = devm_kasprintf(dev, GFP_KERNEL, "spacemit_pcie_irq%d",
			      pci_domain_nr(pp->bridge->bus));
	if (!name) {
		ret = -ENOMEM;
		goto err_host_deinit;
	}

	if (irq > 0) {
		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						spacemit_pcie_irq_thread,
						IRQF_ONESHOT, name, k1);
		if (ret) {
			dev_err_probe(&pdev->dev, ret,
				      "Failed to request PCIe IRQ\n");
			goto err_host_deinit;
		}
	}

#ifdef CONFIG_SOC_SPACEMIT_K3
	if (dw_pcie_link_up(&k1->pci))
		dev_info(dev, "spacemit-pcie: link is up after host_init\n");
	else
		dev_info(dev, "spacemit-pcie: link is down after host_init\n");
#endif
	return 0;

err_host_deinit:
	dw_pcie_host_deinit(pp);

err_pm_runtime_put:
	pm_runtime_disable(dev);

	return ret;
}

static void k1_pcie_remove(struct platform_device *pdev)
{
	struct k1_pcie *k1 = platform_get_drvdata(pdev);

	dw_pcie_host_deinit(&k1->pci.pp);
}

static const struct of_device_id k1_pcie_of_match_table[] = {
	{ .compatible = "spacemit,k1-pcie", },
	{ }
};

static struct platform_driver k1_pcie_driver = {
	.probe	= k1_pcie_probe,
	.remove	= k1_pcie_remove,
	.driver = {
		.name			= "spacemit-k1-pcie",
		.of_match_table		= k1_pcie_of_match_table,
	#ifdef CONFIG_SOC_SPACEMIT_K3
			/*
			 * Force synchronous probing so that PCIe controllers are
			 * initialized in device-tree order (pcie0_rc, pcie1_rc, ...).
			 *
			 * On K3 some root complexes share PHYs (e.g. Port A/B share
			 * phy1). With asynchronous probing, Port B (pcie1_rc) may
			 * probe first, initialize the shared PHY and mark it busy.
			 * When Port A (pcie0_rc) probes later,
			 * spacemit_k3_pcie_phy_is_busy() reports "PHY 1 is busy"
			 * and k1_pcie_init() fails with -EBUSY, so Port A never
			 * comes up even though hardware is present.
			 *
			 * For boards like k3_deb1 we want Port A, if enabled, to
			 * have priority when sharing PHYs with other ports. Using
			 * PROBE_FORCE_SYNCHRONOUS guarantees Port A is probed
			 * before Port B and can claim the shared PHY first.
			 */
		.probe_type		= PROBE_FORCE_SYNCHRONOUS,
#else
		.probe_type		= PROBE_PREFER_ASYNCHRONOUS,
#endif
	},
};
module_platform_driver(k1_pcie_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SpacemiT K1 PCIe host driver");
