// SPDX-License-Identifier: GPL-2.0
/*
 * SpacemiT PCIe Endpoint controller driver
 *
 * Copyright (c) 2025, spacemit Corporation.
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include "pcie-designware.h"

/* Offsets and field definitions for PHY AHB registers */
#define SPACEMIT_PHY_AHB_IRQ_EN		0x0000
#define PCIE_INTERRUPT_EN		BIT(0)

#define	SPACEMIT_PHY_AHB_LINK_STS	0x0004
#define SOFT_RESET	BIT(0)
#define	SMLH_LINK_UP	BIT(1)
#define	RDLH_LINK_UP	BIT(12)
#define	PCIE_CLIENT_DEBUG_LTSSM_MASK	GENMASK(11, 6)
#define	PCIE_CLIENT_DEBUG_LTSSM_L1	(BIT(10) | BIT(8))
#define	PCIE_CLIENT_DEBUG_LTSSM_L2	(BIT(10) | BIT(8) | BIT(6))

#define SPACEMIT_PHY_AHB_IRQSTATUS_INTX		0x0008
#define SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX	0x000c
#define LEG_EP_INTERRUPTS (BIT(6) | BIT(7) | BIT(8) | BIT(9))

#define INTR_STATUS			0x0010

#define INTR_ENABLE			0x0014
#define EDMA_INT			GENMASK(15, 0)
#define RDLH_LINK_UP_INT		BIT(20)

#define ADDR_INTR_STATUS1		0x0018
#define ADDR_INTR_ENABLE1		0x001C
#define MSI_INT				BIT(0)
#define MSIX_INT			GENMASK(8, 1)

/* Some controls require APMU regmap access */
#define SYSCON_APMU			"spacemit,apmu"

/* Offsets and field definitions for APMU registers */
#define PCIE_CLK_RESET_CONTROL		0x0000
#define LTSSM_EN			BIT(6)
#define PCIE_PERSTN_IN			BIT(7)
#define PCIE_AUX_PWR_DET		BIT(9)
#define PCIE_RC_PERST			BIT(12)	/* 1: assert PERST# */
#define APP_HOLD_PHY_RST		BIT(30)
#define DEVICE_TYPE_EP			BIT(31)	/* 0: RC; 1: endpoint */

#define PCIE_CONTROL_LOGIC		0x0004
#define PCIE_SOFT_RESET			BIT(0)
#define PCIE_WAKEUP_MASK		GENMASK(3, 1)
#define PCIE_EP_PERSTN_MASK		BIT(2)
#define PCIE_WAKEUP_INT_CLR		GENMASK(6, 4)
#define PCIE_WAKEUP_INT_STATUS		GENMASK(13, 11)
#define PCIE_WAKEUP_INT_EN		BIT(15)
#define PCIE_EP_PERSTN_OFFSET		BIT(1)

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

#define to_spacemit_pcie(x)	dev_get_drvdata((x)->dev)

#define MAX_PHYS 6

enum spacemit_pcie_ep_status {
	SPACEMIT_PCIE_EP_DISABLED,
	SPACEMIT_PCIE_EP_ENABLED,
};

struct spacemit_pcie_ep {
	struct dw_pcie pci;

	void __iomem *link;		/* DT link */
	struct regmap *pmu;	/* Errors ignored; MMIO-backed regmap */
	u32 pmu_off;

	struct gpio_desc *reset;

	struct phy *phys[MAX_PHYS];	/* multiple PHYs (from 'phys' property) */
	int phy_count;		/* number of valid entries in phys[] */
	int num_lanes;
	struct gpio_desc *detect_gpiod;
	int port_id;

	struct clk_bulk_data *clks;
	int num_clks;
	struct reset_control_bulk_data rsts[DW_PCIE_NUM_APP_RSTS];

	enum spacemit_pcie_ep_status ep_status;
	int global_irq;
	int perst_irq;
};

static void spacemit_pcie_ep_clear_irq_status(struct spacemit_pcie_ep *pcie_ep)
{
	u32 status0;
	u32 status1;
	u32 status2;
	u32 logic_ctrl = pcie_ep->pmu_off + PCIE_CONTROL_LOGIC;
	u32 logic_val, wakeup_status;

	regmap_read(pcie_ep->pmu, logic_ctrl, &logic_val);
	wakeup_status = FIELD_GET(PCIE_WAKEUP_INT_STATUS, logic_val);
	regmap_update_bits(pcie_ep->pmu, logic_ctrl, PCIE_WAKEUP_INT_CLR,
			   FIELD_PREP(PCIE_WAKEUP_INT_CLR, wakeup_status));

	status0 = readl_relaxed(pcie_ep->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	status1 = readl_relaxed(pcie_ep->link + INTR_STATUS);
	status2 = readl_relaxed(pcie_ep->link + ADDR_INTR_STATUS1);

	writel_relaxed(status0, pcie_ep->link + SPACEMIT_PHY_AHB_IRQSTATUS_INTX);
	writel_relaxed(status1, pcie_ep->link + INTR_STATUS);
	writel_relaxed(status2, pcie_ep->link + ADDR_INTR_STATUS1);
}

static bool spacemit_pcie_link_up(struct dw_pcie *pci)
{
	struct spacemit_pcie_ep *pcie_ep = to_spacemit_pcie(pci);
	u32 reg = readl_relaxed(pcie_ep->link + SPACEMIT_PHY_AHB_LINK_STS);

	return (reg & RDLH_LINK_UP) && (reg & SMLH_LINK_UP);
}

static int spacemit_pcie_start_link(struct dw_pcie *pci)
{
	struct spacemit_pcie_ep *pcie_ep = to_spacemit_pcie(pci);
	u32 logic_ctrl = pcie_ep->pmu_off + PCIE_CONTROL_LOGIC;

	/* enable ep perstn int */
	regmap_set_bits(pcie_ep->pmu, logic_ctrl, PCIE_EP_PERSTN_MASK);
	return 0;
}

static void spacemit_pcie_stop_link(struct dw_pcie *pci)
{
	struct spacemit_pcie_ep *pcie_ep = to_spacemit_pcie(pci);
	u32 logic_ctrl = pcie_ep->pmu_off + PCIE_CONTROL_LOGIC;

	/* disable ep perstn int */
	regmap_update_bits(pcie_ep->pmu, logic_ctrl, PCIE_EP_PERSTN_MASK, 0);
}

static int spacemit_pcie_config_lane_mux(struct spacemit_pcie_ep *pcie_ep)
{
	u32 mask = 0, val = 0;

	if (!pcie_ep->pmu) {
		dev_warn(pcie_ep->pci.dev, "PMU regmap not found, lane mux skipped\n");
		return 0;
	}

	switch (pcie_ep->port_id) {
	case 0: /* Port A */
		mask = PORTA_MODE_MASK;
		if (pcie_ep->num_lanes == 8)
			val = PORTA_MODE_X8;
		else if (pcie_ep->num_lanes == 4)
			val = PORTA_MODE_X4;
		else
			val = PORTA_MODE_X2_PORTB_X2;
		break;

	default:
		dev_warn(pcie_ep->pci.dev, "Unsupported port_id %d, lane mux skipped\n", pcie_ep->port_id);
		return -1;
	}

	return regmap_update_bits(pcie_ep->pmu, PMUA_PCIE_SUBSYS_MGMT, mask, val);
}

static int spacemit_pcie_enable_phy(struct spacemit_pcie_ep *pcie_ep)
{
	int i, ret;

	for (i = 0; i < pcie_ep->phy_count; i++) {
		ret = phy_init(pcie_ep->phys[i]);
		if (ret)
			goto err_phy;

		ret = phy_power_on(pcie_ep->phys[i]);
		if (ret) {
			phy_exit(pcie_ep->phys[i]);
			goto err_phy;
		}
	}

	return 0;

err_phy:
	while (--i >= 0) {
		phy_power_off(pcie_ep->phys[i]);
		phy_exit(pcie_ep->phys[i]);
	}

	return ret;
}

static void spacemit_pcie_disable_phy(struct spacemit_pcie_ep *pcie_ep)
{
	int i;

	for (i = 0; i < pcie_ep->phy_count; i++) {
		phy_power_off(pcie_ep->phys[i]);
		phy_exit(pcie_ep->phys[i]);
	}
}

static int spacemit_pcie_enable_resources(struct spacemit_pcie_ep *pcie_ep)
{
	u32 reset_ctrl = pcie_ep->pmu_off + PCIE_CLK_RESET_CONTROL;
	int ret;

	ret = clk_bulk_prepare_enable(pcie_ep->num_clks, pcie_ep->clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(ARRAY_SIZE(pcie_ep->rsts), pcie_ep->rsts);
	if (ret)
		goto err_disable_clk;

	regmap_update_bits(pcie_ep->pmu, reset_ctrl, APP_HOLD_PHY_RST, 0);

	ret = spacemit_pcie_config_lane_mux(pcie_ep);
	if (ret)
		goto err_assert_reset;

	ret = spacemit_pcie_enable_phy(pcie_ep);
	if (ret)
		goto err_assert_reset;

	return 0;

err_assert_reset:
	reset_control_bulk_assert(ARRAY_SIZE(pcie_ep->rsts), pcie_ep->rsts);

err_disable_clk:
	clk_bulk_disable_unprepare(pcie_ep->num_clks, pcie_ep->clks);

	return ret;
}

static void spacemit_pcie_disable_resources(struct spacemit_pcie_ep *pcie_ep)
{
	spacemit_pcie_disable_phy(pcie_ep);
	reset_control_bulk_assert(ARRAY_SIZE(pcie_ep->rsts), pcie_ep->rsts);
	clk_bulk_disable_unprepare(pcie_ep->num_clks, pcie_ep->clks);
}

static int spacemit_pcie_perst_deassert(struct dw_pcie *pci)
{
	struct spacemit_pcie_ep *pcie_ep = to_spacemit_pcie(pci);
	struct device *dev = pci->dev;
	u32 reset_ctrl = pcie_ep->pmu_off + PCIE_CLK_RESET_CONTROL;
	u32 val;
	int ret;

	if (pcie_ep->ep_status == SPACEMIT_PCIE_EP_ENABLED)
		return 0;

	ret = spacemit_pcie_enable_resources(pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to enable resources: %d\n", ret);
		return ret;
	}

	/* Perform cleanup that requires refclk */
	pci_epc_deinit_notify(pci->ep.epc);
	dw_pcie_ep_cleanup(&pci->ep);

	/* Top-level interrupt enable */
	val = readl_relaxed(pcie_ep->link + SPACEMIT_PHY_AHB_IRQ_EN);
	val |= PCIE_INTERRUPT_EN;
	writel_relaxed(val, pcie_ep->link + SPACEMIT_PHY_AHB_IRQ_EN);

	/* edma Interrupt Enable */
	val = readl_relaxed(pcie_ep->link + INTR_ENABLE);
	val |= EDMA_INT;
	writel_relaxed(val, pcie_ep->link + INTR_ENABLE);

	/* Configure PCIe to endpoint mode */
	regmap_set_bits(pcie_ep->pmu, reset_ctrl, DEVICE_TYPE_EP);

	ret = dw_pcie_ep_init_registers(&pcie_ep->pci.ep);
	if (ret) {
		dev_err(dev, "Failed to complete initialization: %d\n", ret);
		goto err_disable_resources;
	}

	pci_epc_init_notify(pcie_ep->pci.ep.epc);

	/* Enable LTSSM */
	regmap_set_bits(pcie_ep->pmu, reset_ctrl, LTSSM_EN);

	pcie_ep->ep_status = SPACEMIT_PCIE_EP_ENABLED;

	return 0;

err_disable_resources:
	spacemit_pcie_disable_resources(pcie_ep);

	return ret;
}

static void spacemit_pcie_perst_assert(struct dw_pcie *pci)
{
	struct spacemit_pcie_ep *pcie_ep = to_spacemit_pcie(pci);
	struct dw_pcie_ep *ep = &pci->ep;
	u32 reset_ctrl = pcie_ep->pmu_off + PCIE_CLK_RESET_CONTROL;

	if (pcie_ep->ep_status == SPACEMIT_PCIE_EP_DISABLED)
		return;

	regmap_update_bits(pcie_ep->pmu, reset_ctrl, LTSSM_EN, 0);
	pci_epc_deinit_notify(ep->epc);
	spacemit_pcie_disable_resources(pcie_ep);
	pcie_ep->ep_status = SPACEMIT_PCIE_EP_DISABLED;
}

/* Common DWC controller ops */
static const struct dw_pcie_ops pci_ops = {
	.link_up = spacemit_pcie_link_up,
	.start_link = spacemit_pcie_start_link,
	.stop_link = spacemit_pcie_stop_link,
};

static void spacemit_pcie_ep_init(struct dw_pcie_ep *ep)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);
	enum pci_barno bar;

	for (bar = BAR_0; bar <= BAR_5; bar++)
		dw_pcie_ep_reset_bar(pci, bar);
}

static irqreturn_t spacemit_pcie_ep_global_irq_thread(int irq, void *data)
{
	struct spacemit_pcie_ep *pcie_ep = data;
	struct dw_pcie *pci = &pcie_ep->pci;
	struct device *dev = pci->dev;
	u32 reset_ctrl = pcie_ep->pmu_off + PCIE_CLK_RESET_CONTROL;
	u32 logic_ctrl = pcie_ep->pmu_off + PCIE_CONTROL_LOGIC;
	u32 reset_val, logic_val, mask, status;

	/* get int status */
	regmap_read(pcie_ep->pmu, logic_ctrl, &logic_val);
	mask = FIELD_GET(PCIE_WAKEUP_MASK, logic_val);
	status = FIELD_GET(PCIE_WAKEUP_INT_STATUS, logic_val);

	/* ep perstn int */
	if (FIELD_GET(PCIE_EP_PERSTN_OFFSET, (mask & status))) {
		regmap_read(pcie_ep->pmu, reset_ctrl, &reset_val);
		if (reset_val & PCIE_PERSTN_IN) {
			dev_dbg(dev, "PERST de-asserted by host.Starting link training!\n");
			spacemit_pcie_perst_deassert(pci);
		} else {
			dev_dbg(dev, "PERST asserted by host.Shutting down the PCIe link!\n");
			spacemit_pcie_perst_assert(pci);
		}
	}

	/* clear int status */
	regmap_update_bits(pcie_ep->pmu, logic_ctrl, PCIE_WAKEUP_INT_CLR,
			   FIELD_PREP(PCIE_WAKEUP_INT_CLR, status));

	return IRQ_HANDLED;
}

static int spacemit_pcie_ep_enable_irq_resources(struct platform_device *pdev,
						 struct spacemit_pcie_ep *pcie_ep)
{
	struct device *dev = pcie_ep->pci.dev;
	u32 logic_ctrl = pcie_ep->pmu_off + PCIE_CONTROL_LOGIC;
	char *name;
	int ret;

	name = devm_kasprintf(dev, GFP_KERNEL, "spacemit_pcie_ep_global_irq%d",
			      pcie_ep->pci.ep.epc->domain_nr);
	if (!name)
		return -ENOMEM;

	pcie_ep->global_irq = platform_get_irq_byname(pdev, "global");
	if (pcie_ep->global_irq < 0)
		return pcie_ep->global_irq;

	regmap_set_bits(pcie_ep->pmu, logic_ctrl, PCIE_WAKEUP_INT_EN);
	regmap_update_bits(pcie_ep->pmu, logic_ctrl, PCIE_WAKEUP_MASK, 0);
	ret = devm_request_threaded_irq(&pdev->dev, pcie_ep->global_irq, NULL,
					spacemit_pcie_ep_global_irq_thread,
					IRQF_ONESHOT,
					name, pcie_ep);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request Global IRQ\n");
		return ret;
	}

	return 0;
}

static int spacemit_pcie_ep_raise_irq(struct dw_pcie_ep *ep, u8 func_no,
				      unsigned int type, u16 interrupt_num)
{
	struct dw_pcie *pci = to_dw_pcie_from_ep(ep);

	switch (type) {
	case PCI_IRQ_INTX:
		return dw_pcie_ep_raise_intx_irq(ep, func_no);
	case PCI_IRQ_MSI:
		return dw_pcie_ep_raise_msi_irq(ep, func_no, interrupt_num);
	case PCI_IRQ_MSIX:
		return dw_pcie_ep_raise_msix_irq(ep, func_no, interrupt_num);
	default:
		dev_err(pci->dev, "Unknown IRQ type\n");
		return -EINVAL;
	}
}

static const struct pci_epc_features spacemit_pcie_epc_features = {
	.linkup_notifier = false,
	.msi_capable = true,
	.msix_capable = true,
	.align = SZ_64K,
	.bar[BAR_2] = { .type = BAR_RESERVED, },
};

static const struct pci_epc_features *
spacemit_pcie_epc_get_features(struct dw_pcie_ep *ep)
{
	return &spacemit_pcie_epc_features;
}

static const struct dw_pcie_ep_ops pci_ep_ops = {
	.init = spacemit_pcie_ep_init,
	.raise_irq = spacemit_pcie_ep_raise_irq,
	.get_features = spacemit_pcie_epc_get_features,
};

static int spacemit_pcie_ep_get_io_resources(struct platform_device *pdev,
					     struct spacemit_pcie_ep *pcie_ep)
{
	struct device *dev = &pdev->dev;
	struct dw_pcie *pci = &pcie_ep->pci;
	struct resource *res;

	pcie_ep->pmu = syscon_regmap_lookup_by_phandle_args(dev_of_node(dev),
							    SYSCON_APMU, 1,
							    &pcie_ep->pmu_off);
	if (IS_ERR(pcie_ep->pmu))
		return dev_err_probe(dev, PTR_ERR(pcie_ep->pmu),
				     "failed to lookup PMU registers\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dbi");
	pci->dbi_base = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pci->dbi_base))
		return PTR_ERR(pci->dbi_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "link");
	pcie_ep->link = devm_pci_remap_cfg_resource(dev, res);
	if (IS_ERR(pcie_ep->link))
		return PTR_ERR(pcie_ep->link);

	return 0;
}

static int spacemit_pcie_ep_get_resources(struct platform_device *pdev,
					  struct spacemit_pcie_ep *pcie_ep)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	u32 val;
	int ret;

	ret = spacemit_pcie_ep_get_io_resources(pdev, pcie_ep);
	if (ret) {
		dev_err(dev, "Failed to get io resources %d\n", ret);
		return ret;
	}

	pcie_ep->num_clks = devm_clk_bulk_get_all(dev, &pcie_ep->clks);
	if (pcie_ep->num_clks < 0) {
		dev_err(dev, "Failed to get clocks\n");
		return pcie_ep->num_clks;
	}

	pcie_ep->rsts[0].id = "dbi";
	pcie_ep->rsts[1].id = "mstr";
	pcie_ep->rsts[2].id = "slv";
	ret = devm_reset_control_bulk_get_exclusive(dev, DW_PCIE_NUM_APP_RSTS, pcie_ep->rsts);
	if (ret) {
		dev_err(dev, "Failed to get resets\n");
		return ret;
	}

	/* k3 only port 0 support ep */
	if (!of_property_read_u32(np, "spacemit,pcie-port", &val) && val == 0) {
		pcie_ep->port_id = val;
	} else {
		dev_err(dev, "spacemit,pcie-port property not provided or invalid\n");
		return -EINVAL;
	}

	if (!of_property_read_u32(np, "num-lanes", &val) &&
	    val >= 1 && val <= 8) {
		pcie_ep->num_lanes = val;
	} else {
		dev_warn(dev,
			 "num-lanes property not provided or invalid, setting num-lanes to 1\n");
		pcie_ep->num_lanes = 1;
	}

	ret = of_count_phandle_with_args(np, "phys", "#phy-cells");
	if (ret > 0) {
		pcie_ep->phy_count = ret;
		if (pcie_ep->phy_count > MAX_PHYS) {
			dev_warn(dev, "Too many PHYs (%d), limiting to %d\n",
				 pcie_ep->phy_count, MAX_PHYS);
			pcie_ep->phy_count = MAX_PHYS;
		}

		pcie_ep->detect_gpiod = devm_gpiod_get_optional(dev, "spacemit,device-detect", GPIOD_IN);
		if (IS_ERR(pcie_ep->detect_gpiod))
			return dev_err_probe(dev, PTR_ERR(pcie_ep->detect_gpiod),
					"failed to get detect gpio\n");

		if (pcie_ep->port_id == 0) {
			if (pcie_ep->detect_gpiod && gpiod_get_value(pcie_ep->detect_gpiod)) {
				dev_info(dev, "Port B device detected, degrading Port A to x2 mode\n");
				pcie_ep->phy_count = 1;
				pcie_ep->num_lanes = 2;
			}
		}

		for (int i = 0; i < pcie_ep->phy_count; i++) {
			pcie_ep->phys[i] = devm_of_phy_get_by_index(dev, np, i);
			if (IS_ERR(pcie_ep->phys[i]))
				return dev_err_probe(dev, PTR_ERR(pcie_ep->phys[i]),
						     "failed to get PCIe PHY %d\n", i);
		}
		ret = 0;
	}

	return ret;
}

static int spacemit_pcie_ep_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_pcie_ep *pcie_ep;
	struct dw_pcie_ep *ep;
	int ret;

	pcie_ep = devm_kzalloc(dev, sizeof(*pcie_ep), GFP_KERNEL);
	if (!pcie_ep)
		return -ENOMEM;

	pcie_ep->pci.dev = dev;
	pcie_ep->pci.ops = &pci_ops;
	ep = &pcie_ep->pci.ep;
	ep->ops = &pci_ep_ops;
	pcie_ep->pci.edma.ll_wr_cnt = 8;
	pcie_ep->pci.edma.ll_rd_cnt = 8;
	pcie_ep->pci.edma.mf = EDMA_MF_HDMA_NATIVE;

	platform_set_drvdata(pdev, pcie_ep);

	ret = spacemit_pcie_ep_get_resources(pdev, pcie_ep);
	if (ret)
		return ret;

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	ep->page_size = SZ_64K;
	ret = dw_pcie_ep_init(&pcie_ep->pci.ep);
	if (ret) {
		dev_err(dev, "Failed to initialize endpoint: %d\n", ret);
		return ret;
	}

	spacemit_pcie_ep_clear_irq_status(pcie_ep);
	ret = spacemit_pcie_ep_enable_irq_resources(pdev, pcie_ep);
	if (ret)
		goto err_ep_deinit;

	return 0;

err_ep_deinit:
	dw_pcie_ep_deinit(&pcie_ep->pci.ep);

	return ret;
}

static void spacemit_pcie_ep_remove(struct platform_device *pdev)
{
	struct spacemit_pcie_ep *pcie_ep = platform_get_drvdata(pdev);
	struct dw_pcie *pci = &pcie_ep->pci;
	struct dw_pcie_ep *ep = &pci->ep;
	u32 reset_ctrl = pcie_ep->pmu_off + PCIE_CLK_RESET_CONTROL;

	dw_pcie_stop_link(pci);

	pci_epc_deinit_notify(ep->epc);
	dw_pcie_ep_deinit(ep);

	disable_irq(pcie_ep->global_irq);

	if (pcie_ep->ep_status == SPACEMIT_PCIE_EP_DISABLED)
		return;

	regmap_update_bits(pcie_ep->pmu, reset_ctrl, LTSSM_EN, 0);
	spacemit_pcie_disable_resources(pcie_ep);
}

static const struct of_device_id spacemit_pcie_ep_match[] = {
	{ .compatible = "spacemit,k3-pcie-ep",},
	{ }
};
MODULE_DEVICE_TABLE(of, spacemit_pcie_ep_match);

static struct platform_driver spacemit_pcie_ep_driver = {
	.probe	= spacemit_pcie_ep_probe,
	.remove = spacemit_pcie_ep_remove,
	.driver	= {
		.name = "spacemit-pcie-ep",
		.of_match_table	= spacemit_pcie_ep_match,
	},
};
builtin_platform_driver(spacemit_pcie_ep_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SpacemiT PCIe endpoint controller driver");
