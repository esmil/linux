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
#include <linux/gpio/consumer.h>
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
#define PME_TURN_OFF			BIT(5)

#define K1_PHY_AHB_LINK_STS			0x0004
#define SMLH_LINK_UP			BIT(1)
#define RDLH_LINK_UP			BIT(12)
#define PCIE_CLIENT_DEBUG_LTSSM_MASK	GENMASK(11, 6)
#define PCIE_CLIENT_DEBUG_LTSSM_L1	(BIT(10) | BIT(8))
#define PCIE_CLIENT_DEBUG_LTSSM_L2	(BIT(10) | BIT(8) | BIT(6))

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
#define PCIE_WAKEUP_MASK		GENMASK(3, 1)
#define PCIE_RC_WAKEN_MASK		BIT(3)
#define PCIE_WAKEUP_INT_CLR		GENMASK(6, 4)
#define PCIE_WAKEUP_INT_STATUS		GENMASK(13, 11)
#define PCIE_WAKEUP_EN			BIT(14)
#define PCIE_WAKEUP_INT_EN		BIT(15)
#define PCIE_RC_WAKEN_OFFSET		BIT(2)
#define PCIE_PERSTN_OE			BIT(24)
#define PCIE_PERSTN_OUT			BIT(25)
#define PCIE_IGNORE_PERSTN		BIT(31)

#define SPACEMIT_PHY_AHB_IRQSTATUS_INTX		0x0008
#define SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX	0x000c
#define LEG_EP_INTERRUPTS (BIT(6) | BIT(7) | BIT(8) | BIT(9))

#define SPACEMIT_PHY_AHB_IRQENABLE_SET_MSI	0x0014
/* MSI defined as BIT(11) in existing INTR_ENABLE, reusing */

#define ADDR_INTR_STATUS1		0x0018
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

/*
 * PCIe Port A, B and C IOMMU bypass
 *   [5] Port A
 *   [6] Port B
 *   [7] Port C
 */
#define PCIE_IOMMU_BYPASS(port_id)	BIT(5 + (port_id))

#define MAX_PHYS 6
#endif

#define PCIE_LINK_IS_L2(x) \
	(((x) & PCIE_CLIENT_DEBUG_LTSSM_MASK) == PCIE_CLIENT_DEBUG_LTSSM_L2)
struct k1_pcie {
	struct dw_pcie pci;
#ifdef CONFIG_SOC_SPACEMIT_K3
	struct phy		*phys[MAX_PHYS];	/* multiple PHYs (from 'phys' property) */
	int			phy_count;		/* number of valid entries in phys[] */
	int			num_lanes;
	int			port_id;
	bool			link_up;
	int 			wakeup_irq;
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

/*
 * Read a GPIO from this port's DT node (non-devm, released immediately).
 * Returns: 1 = high, 0 = low, -ENOENT = no such property, negative = error.
 */
static int k1_pcie_read_gpio_property(struct device *dev, const char *prop)
{
	struct gpio_desc *gpiod;
	int ret;

	gpiod = gpiod_get_optional(dev, prop, GPIOD_IN);
	if (IS_ERR(gpiod)) {
		ret = PTR_ERR(gpiod);
		/* GPIO controller not ready, or peer holds the shared GPIO */
		if (ret == -EBUSY)
			return -EPROBE_DEFER;
		return ret;
	}

	if (!gpiod)
		return -ENOENT;

	ret = gpiod_get_value(gpiod);
	gpiod_put(gpiod);
	return ret;
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
	val &= ~GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC;
	val |= FIELD_PREP(GEN3_EQ_CONTROL_OFF_PSET_REQ_VEC, (0x1 << 7));
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

static void k1_pcie_clear_irq_status(struct k1_pcie *k1)
{
	u32 status0;
	u32 status1;
	u32 status2;
	u32 logic_ctrl = k1->pmu_off + PCIE_CONTROL_LOGIC;
	u32 logic_val, wakeup_status;

	regmap_read(k1->pmu, logic_ctrl, &logic_val);
	wakeup_status = FIELD_GET(PCIE_WAKEUP_INT_STATUS, logic_val);
	regmap_update_bits(k1->pmu, logic_ctrl, PCIE_WAKEUP_INT_CLR,
			   FIELD_PREP(PCIE_WAKEUP_INT_CLR, wakeup_status));

	status0 = readl_relaxed(k1->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	status1 = readl_relaxed(k1->link + INTR_STATUS);
	status2 = readl_relaxed(k1->link + ADDR_INTR_STATUS1);

	writel_relaxed(status0, k1->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	writel_relaxed(status1, k1->link + INTR_STATUS);
	writel_relaxed(status2, k1->link + ADDR_INTR_STATUS1);
}

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
	u32 status0;
	u32 status1;
	u32 status2;

	status0 = readl_relaxed(k1->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	status1 = readl_relaxed(k1->link + INTR_STATUS);
	status2 = readl_relaxed(k1->link + ADDR_INTR_STATUS1);

	writel_relaxed(status0, k1->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	writel_relaxed(status1, k1->link + INTR_STATUS);
	writel_relaxed(status2, k1->link + ADDR_INTR_STATUS1);

	if (FIELD_GET(RDLH_LINK_UP_INT, status1)) {
		msleep(PCIE_RESET_CONFIG_WAIT_MS);
		dev_dbg(dev, "Received Link up event. Starting enumeration!\n");
		/* Rescan the bus to enumerate endpoint devices */
		pci_lock_rescan_remove();
		pci_rescan_bus(pp->bridge->bus);
		pci_unlock_rescan_remove();
	} else if (!status0 && !status1 && !status2)
		dev_WARN_ONCE(dev, 1,
			      "Received unknown event. status0=0x%08x status1=0x%08x status2=0x%08x\n",
			      status0, status1, status2);

	return IRQ_HANDLED;
}

static irqreturn_t spacemit_pcie_wakeup_irq_thread(int irq, void *data)
{
	struct k1_pcie *k1 = data;
	struct dw_pcie *pci = &k1->pci;
	struct device *dev = pci->dev;
	u32 logic_ctrl = k1->pmu_off + PCIE_CONTROL_LOGIC;
	u32 logic_val, mask, status;

	/* get int status */
	regmap_read(k1->pmu, logic_ctrl, &logic_val);
	mask = FIELD_GET(PCIE_WAKEUP_MASK, logic_val);
	status = FIELD_GET(PCIE_WAKEUP_INT_STATUS, logic_val);

	/* rc wakeup int */
	if (FIELD_GET(PCIE_RC_WAKEN_OFFSET, (mask & status))) {
		dev_info(dev, "pcie wakeup interrupt received from RC\n");
	}

	/* clear int status */
	regmap_update_bits(k1->pmu, logic_ctrl, PCIE_WAKEUP_INT_CLR,
			   FIELD_PREP(PCIE_WAKEUP_INT_CLR, status));
	return IRQ_HANDLED;
}

static int k1_pcie_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 reset_ctrl = k1->pmu_off + PCIE_CLK_RESET_CONTROL;
	int ret;
#ifndef CONFIG_SOC_SPACEMIT_K3
	u32 val;
#endif

	regmap_update_bits(k1->pmu, reset_ctrl, LTSSM_EN, 0);
	k1_pcie_toggle_soft_reset(k1);

	ret = k1_pcie_enable_resources(k1);
	if (ret)
		return ret;

#ifdef CONFIG_SOC_SPACEMIT_K3
	regmap_set_bits(k1->pmu, reset_ctrl, PCIE_AUX_PWR_DET);
	regmap_update_bits(k1->pmu, reset_ctrl, APP_HOLD_PHY_RST, 0);

	ret = spacemit_pcie_config_lane_mux(k1);
	if (ret) {
		k1_pcie_disable_resources(k1);
		return ret;
	}

	ret = spacemit_pcie_enable_phy(k1);
	if (ret) {
		k1_pcie_disable_resources(k1);
		return ret;
	}
#else
	regmap_set_bits(k1->pmu, reset_ctrl, DEVICE_TYPE_RC | PCIE_AUX_PWR_DET);

	ret = phy_init(k1->phy);
	if (ret) {
		k1_pcie_disable_resources(k1);

		return ret;
	}
#endif

	/*
	 * Start by asserting fundamental reset (drive PERST# low).  The
	 * PCI CEM spec says that PERST# should be deasserted at least
	 * 100ms after the power becomes stable, so we'll insert that
	 * delay first.  Write, then read it back to guarantee the write
	 * reaches the device before we start the delay.
	 */
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
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CONTROL_LOGIC,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE);
	spacemit_pcie_eq_preset(k1);
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

static void spacemit_pcie_pme_turn_off(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct k1_pcie *k1 = to_k1_pcie(pci);
	u32 val;

	if (!dw_pcie_link_up(pci))
		return;

	val = readl_relaxed(k1->link + K1_PHY_AHB_IRQ_EN);
	val |= PME_TURN_OFF;
	writel_relaxed(val, k1->link + K1_PHY_AHB_IRQ_EN);
	udelay(1);
	val = readl_relaxed(k1->link + K1_PHY_AHB_IRQ_EN);
	val &= ~PME_TURN_OFF;
	writel_relaxed(val, k1->link + K1_PHY_AHB_IRQ_EN);
}

static void spacemit_pcie_wait_for_l2(struct k1_pcie *k1)
{
	u32 val;
	int ret;

	spacemit_pcie_pme_turn_off(&k1->pci.pp);
	ret = readl_poll_timeout(k1->link + K1_PHY_AHB_LINK_STS,
				 val, PCIE_LINK_IS_L2(val), PCIE_PME_TO_L2_TIMEOUT_US/10,
				 PCIE_PME_TO_L2_TIMEOUT_US);
	if (ret) {
		/* Only log message when LTSSM isn't in DETECT or POLL */
		dev_warn(k1->pci.dev, "Timeout waiting for L2 entry! LTSSM: 0x%x\n", val);
	}
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

		/* Bifurcation GPIO: if asserted, yield lanes to the peer port */
		ret = k1_pcie_read_gpio_property(dev, "spacemit,bifurcation");
		if (ret < 0 && ret != -ENOENT)
			return dev_err_probe(dev, ret, "Failed to read bifurcation GPIO\n");
		if (ret > 0) {
			if (k1->port_id == 0) {
				dev_info(dev, "Bifurcation GPIO asserted, degrading to x2 mode\n");
				k1->phy_count = 1;
				k1->num_lanes = 2;
			}
		}

		/* Device-detect GPIO: skip this port if no device is present */
		ret = k1_pcie_read_gpio_property(dev, "spacemit,device-detect");
		if (ret < 0 && ret != -ENOENT)
			return dev_err_probe(dev, ret, "Failed to read device-detect GPIO\n");
		/* ret: 1 = present, 0 = absent, -ENOENT = no GPIO (assume present) */
		if (ret == 0) {
			dev_info(dev, "Device not detected, skipping initialization\n");
			return -ENODEV;
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

/*
 * Determine whether the IOMMU device node specified by "iommu-map" is available
 * ("status" property is "okay" or "ok").
 */
static bool pcie_iommu_is_available(const struct device_node *np)
{
	const char *map_name = "iommu-map";
	const __be32 *map = NULL;
	int map_len;
	u32 phandle;
	struct device_node *phandle_node;

	/*
	 * "iommu-map" property is an arbitrary number of tuples of
	 * <requester_id_base, iommu_phandle, iommu_device_id_base, length>.
	 *
	 * Each tuple has 4 cells.
	 */

	map = of_get_property(np, map_name, &map_len);
	if (!map)
		return false;

	if (!map_len || map_len % (4 * sizeof(*map))) {
		pr_err("%pOF: Error: Bad %s length: %d\n", np, map_name, map_len);
		return false;
	}

	/* Here we only check the first tuple */
	phandle = be32_to_cpup(map + 1);
	phandle_node = of_find_node_by_phandle(phandle);
	if (!phandle_node)
		return false;

	return of_device_is_available(phandle_node);
}

/*
 * Setup IOMMU bypass.
 * It must be run after that pmu and port_id in struct k1_pcie are initialized.
 */
static void pcie_iommu_bypass_setup(struct k1_pcie *k1)
{
	struct device *dev = k1->pci.dev;
	struct device_node *np = dev->of_node;

	/* Only PCIe A, B and C are behind IOMMU */
	if (k1->port_id > 2)
		return;

	if (pcie_iommu_is_available(np)) {
		dev_info(dev, "iommu not bypassed\n");
		regmap_clear_bits(k1->pmu, PMUA_PCIE_SUBSYS_MGMT,
				  PCIE_IOMMU_BYPASS(k1->port_id));
	} else {
		dev_info(dev, "iommu bypassed\n");
		regmap_set_bits(k1->pmu, PMUA_PCIE_SUBSYS_MGMT,
				PCIE_IOMMU_BYPASS(k1->port_id));
	}
}

static int k1_pcie_enable_wakeup_irq(struct k1_pcie *k1)
{
	u32 logic_ctrl = k1->pmu_off + PCIE_CONTROL_LOGIC;

	/* enable rc wakeup int */
	if (k1->wakeup_irq > 0) {
		regmap_update_bits(k1->pmu, logic_ctrl,
				   PCIE_WAKEUP_EN | PCIE_WAKEUP_INT_EN | PCIE_RC_WAKEN_MASK,
				   PCIE_WAKEUP_EN | PCIE_WAKEUP_INT_EN | PCIE_RC_WAKEN_MASK);
		enable_irq(k1->wakeup_irq);
	}

	return 0;
}

static int k1_pcie_disable_wakeup_irq(struct k1_pcie *k1)
{
	u32 logic_ctrl = k1->pmu_off + PCIE_CONTROL_LOGIC;

	if (k1->wakeup_irq > 0) {
		disable_irq(k1->wakeup_irq);
		regmap_update_bits(k1->pmu, logic_ctrl,
				   PCIE_WAKEUP_EN | PCIE_WAKEUP_INT_EN | PCIE_RC_WAKEN_MASK, 0);
	}

	return 0;
}

static int k1_pcie_suspend_noirq(struct device *dev)
{
	struct k1_pcie *k1 = dev_get_drvdata(dev);
	struct dw_pcie *pci = &k1->pci;
	u8 offset = dw_pcie_find_capability(pci, PCI_CAP_ID_EXP);

	/*
	 * If L1SS is supported, then do not put the link into L2 as some
	 * devices such as NVMe expect low resume latency.
	 */
	if (dw_pcie_readw_dbi(pci, offset + PCI_EXP_LNKCTL) & PCI_EXP_LNKCTL_ASPM_L1) {
		dev_info(pci->dev, "L1 ASPM supported, skip L2 entry on suspend\n");
		return 0;
	}

	k1->link_up = k1_pcie_link_up(pci);
	/* Put the link into L2 to save power */
	if (k1->link_up) {
		spacemit_pcie_wait_for_l2(k1);
	}

	udelay(1);

	dw_pcie_stop_link(pci);

#ifdef CONFIG_SOC_SPACEMIT_K3
	spacemit_pcie_disable_phy(k1);
#else
	phy_exit(k1->phy);
#endif

	clk_bulk_disable_unprepare(ARRAY_SIZE(pci->app_clks), pci->app_clks);

	k1_pcie_enable_wakeup_irq(k1);

	pci->suspended = true;

	return 0;
}

static int k1_pcie_resume_noirq(struct device *dev)
{
	struct k1_pcie *k1 = dev_get_drvdata(dev);
	struct dw_pcie *pci = &k1->pci;
	u32 reset_ctrl = k1->pmu_off + PCIE_CLK_RESET_CONTROL;
	int ret;

	if (!pci->suspended)
		return 0;

	k1_pcie_disable_wakeup_irq(k1);

	pci->suspended = false;

	k1_pcie_toggle_soft_reset(k1);
	ret = clk_bulk_prepare_enable(ARRAY_SIZE(pci->app_clks), pci->app_clks);
	if (ret)
		return ret;

#ifdef CONFIG_SOC_SPACEMIT_K3
	regmap_set_bits(k1->pmu, reset_ctrl, PCIE_AUX_PWR_DET);
	regmap_update_bits(k1->pmu, reset_ctrl, APP_HOLD_PHY_RST, 0);

	ret = spacemit_pcie_config_lane_mux(k1);
	if (ret)
		goto err_disable_clks;

	ret = spacemit_pcie_enable_phy(k1);
	if (ret)
		goto err_disable_clks;
#else
	regmap_set_bits(k1->pmu, reset_ctrl, DEVICE_TYPE_RC | PCIE_AUX_PWR_DET);

	ret = phy_init(k1->phy);
	if (ret)
		goto err_disable_clks;
#endif

	regmap_update_bits(k1->pmu, reset_ctrl, LTSSM_EN, 0);
	/*
	 * Start by asserting fundamental reset (drive PERST# low).  The
	 * PCI CEM spec says that PERST# should be deasserted at least
	 * 100ms after the power becomes stable, so we'll insert that
	 * delay first.  Write, then read it back to guarantee the write
	 * reaches the device before we start the delay.
	 */
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
	regmap_update_bits(k1->pmu, k1->pmu_off + PCIE_CONTROL_LOGIC,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE,
			   PCIE_PERSTN_OUT | PCIE_PERSTN_OE);
	spacemit_pcie_eq_preset(k1);
#endif

	/* Deassert fundamental reset (drive PERST# high) */
#ifndef CONFIG_SOC_SPACEMIT_K3
	regmap_clear_bits(k1->pmu, reset_ctrl, PCIE_RC_PERST);
#endif

	/* Finally, as a workaround, disable ASPM L1 */
	k1_pcie_disable_aspm_l1(k1);

	spacemit_pcie_msi_host_init(&k1->pci.pp);
	dw_pcie_setup_rc(&pci->pp);

	ret = dw_pcie_start_link(pci);
	if (ret)
		goto err_phy_exit;

	if (k1->link_up)
		dw_pcie_wait_for_link(pci);

	return 0;

err_phy_exit:
#ifdef CONFIG_SOC_SPACEMIT_K3
	spacemit_pcie_disable_phy(k1);
#else
	phy_exit(k1->phy);
#endif

err_disable_clks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(pci->app_clks), pci->app_clks);

	return ret;
}

static const struct dev_pm_ops k1_pcie_pm_ops = {
	NOIRQ_SYSTEM_SLEEP_PM_OPS(k1_pcie_suspend_noirq,
				  k1_pcie_resume_noirq)
};

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
		if (ret != -ENODEV)
			dev_err_probe(dev, ret, "failed to parse port\n");
		goto err_pm_runtime_put;
	}

	pcie_iommu_bypass_setup(k1);

	irq = platform_get_irq_byname_optional(pdev, "pcie_irq");
	if (irq > 0)
		pp->use_linkup_irq = true;

	k1->wakeup_irq = platform_get_irq_byname_optional(pdev, "wakeup");

	k1_pcie_clear_irq_status(k1);

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

	name = devm_kasprintf(dev, GFP_KERNEL, "spacemit_pcie_rc_wakeup%d",
			      pci_domain_nr(pp->bridge->bus));
	if (!name) {
		ret = -ENOMEM;
		goto err_disable_pcie_irq;
	}

	if (k1->wakeup_irq > 0) {
		irq_set_status_flags(k1->wakeup_irq, IRQ_NOAUTOEN);
		ret = devm_request_threaded_irq(&pdev->dev, k1->wakeup_irq, NULL,
						spacemit_pcie_wakeup_irq_thread,
						IRQF_ONESHOT | IRQF_NO_SUSPEND,
						name, k1);
		if (ret) {
			dev_err_probe(&pdev->dev, ret,
				      "Failed to request wakeup IRQ\n");
			goto err_disable_pcie_irq;
		}

		device_init_wakeup(&pdev->dev, true);
	}

#ifdef CONFIG_SOC_SPACEMIT_K3
	if (dw_pcie_link_up(&k1->pci))
		dev_info(dev, "spacemit-pcie: link is up after host_init\n");
	else
		dev_info(dev, "spacemit-pcie: link is down after host_init\n");
#endif
	return 0;

err_disable_pcie_irq:
	if (irq > 0)
		disable_irq(irq);

err_host_deinit:
	dw_pcie_host_deinit(pp);

err_pm_runtime_put:
	pm_runtime_disable(dev);

	return ret;
}

static void k1_pcie_remove(struct platform_device *pdev)
{
	struct k1_pcie *k1 = platform_get_drvdata(pdev);

	if (k1->wakeup_irq > 0) {
		device_init_wakeup(&pdev->dev, false);
	}

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
		.pm			= &k1_pcie_pm_ops,
		.probe_type		= PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_platform_driver(k1_pcie_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SpacemiT K1 PCIe host driver");
