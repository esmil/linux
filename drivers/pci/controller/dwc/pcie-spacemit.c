// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit PCIe rc && ep driver
 *
 * Copyright (c) 2025, spacemit Corporation.
 *
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/resource.h>
#include <linux/types.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "../../pci.h"
#include "pcie-designware.h"

#define	PCIE_VENDORID_MASK	0xffff
#define	PCIE_DEVICEID_SHIFT	16
#define	SPACEMIT_PCIE_VENDOR_ID	0x201F
#define	k1_PCIE_DEVICE_ID	0x0001
#define	K3_PCIE_DEVICE_ID	0x0002

/* PCIe controller wrapper k1x configuration registers */

#define	SPACEMIT_PHY_AHB_IRQ_EN	0x0000
#define	IRQ_EN	BIT(0)
#define	PME_TURN_OFF	BIT(5)

#define	SPACEMIT_PHY_AHB_IRQSTATUS_INTX	0x0008
#define	INTA	BIT(6)
#define	INTB	BIT(7)
#define	INTC	BIT(8)
#define	INTD	BIT(9)
#define	LEG_EP_INTERRUPTS (INTA | INTB | INTC | INTD)
#define	INTX_MASK	GENMASK(9, 6)
#define	INTX_SHIFT	6

#define	SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX	0x000c

#define	SPACEMIT_PHY_AHB_IRQSTATUS_MSI	0x0010
#define	MSI	BIT(11)
#define	PCIE_REMOTE_INTERRUPT	BIT(31)
/* DMA write channel 0~7 irq*/
#define	EDMA_INT0	BIT(0)
#define	EDMA_INT1	BIT(1)
#define	EDMA_INT2	BIT(2)
#define	EDMA_INT3	BIT(3)
#define	EDMA_INT4	BIT(4)
#define	EDMA_INT5	BIT(5)
#define	EDMA_INT6	BIT(6)
#define	EDMA_INT7	BIT(7)
/* DMA read channel 0~7 irq*/
#define	EDMA_INT8	BIT(8)
#define	EDMA_INT9	BIT(9)
#define	EDMA_INT10	BIT(10)
#define	EDMA_INT11	BIT(11)
#define	EDMA_INT12	BIT(12)
#define	EDMA_INT13	BIT(13)
#define	EDMA_INT14	BIT(14)
#define	EDMA_INT15	BIT(15)
#define	DMA_READ_INT	GENMASK(11, 8)

#define	SPACEMIT_PHY_AHB_IRQENABLE_SET_MSI	0x0014

#define	PCIECTRL_SPACEMIT_CONF_DEVICE_CMD	0x0000
#define	LTSSM_EN	BIT(6)
/* Perst input value in ep mode */
#define	PCIE_PERST_IN	BIT(7)
#define	PCIE_AUX_PWR_DET	BIT(9)
#define	APP_HOLD_PHY_RST	BIT(30)
/* BIT31 0: EP, 1: RC*/
#define	DEVICE_TYPE_RC	BIT(31)

#define	PCIE_CTRL_LOGIC	0x0004
#define	PCIE_IGNORE_PERSTN	BIT(31)
/* Perst GPIO en in RC mode 1: perst# low, 0: perst# high */
#define	PCIE_PERSTN_OE	BIT(24)

#define	SPACEMIT_PHY_AHB_LINK_STS	0x0004
#define	SMLH_LINK_UP	BIT(1)
#define	RDLH_LINK_UP	BIT(12)
#define	PCIE_CLIENT_DEBUG_LTSSM_MASK	GENMASK(11, 6)
#define	PCIE_CLIENT_DEBUG_LTSSM_L1	(BIT(10) | BIT(8))
#define	PCIE_CLIENT_DEBUG_LTSSM_L2	(BIT(10) | BIT(8) | BIT(6))

#define	ADDR_INTR_STATUS1	0x0018
#define	ADDR_INTR_ENABLE1	0x001C
#define	MSI_INT	BIT(0)
#define	MSIX_INT	GENMASK(8, 1)

#define	ADDR_MSI_RECV_CTRL	0x0080
#define	MSI_MON_EN	BIT(0)
#define	MSIX_MON_EN	GENMASK(8, 1)
#define	MSIX_AFIFO_FULL	BIT(30)
#define	MSIX_AFIFO_EMPTY	BIT(29)
#define	ADDR_MSI_RECV_ADDR0	0x0084
#define	ADDR_MSIX_MON_MASK	0x0088
#define	ADDR_MSIX_MON_BASE0	0x008c

#define	ADDR_MON_FIFO_DATA0	0x00b0
#define	ADDR_MON_FIFO_DATA1	0x00b4
#define	FIFO_EMPTY	0xFFFFFFFF
#define	FIFO_LEN	32
#define	INT_VEC_MASK	GENMASK(7, 0)

#define	EXP_CAP_ID_OFFSET	0x70

#define	PCIECTRL_SPACEMIT_CONF_INTX_ASSERT	0x0124
#define	PCIECTRL_SPACEMIT_CONF_INTX_DEASSERT	0x0128

/*RC write config  0xD28 offset register which equal with ELBI offset 0x028 addr*/
#define PCIE_ELBI_EP_DMA_IRQ_STATUS	0x028
#define	PC_TO_EP_INT	(0x3fffffff)

#define PCIE_ELBI_EP_DMA_IRQ_MASK	0x02c
#define	PC_TO_EP_INT_MASK	(0x3fffffff)

#define PCIE_ELBI_EP_MSI_REASON         0x018

#define PCIE_LINK_IS_L2(x) \
	(((x) & PCIE_CLIENT_DEBUG_LTSSM_MASK) == PCIE_CLIENT_DEBUG_LTSSM_L2)
struct spacemit_pcie {
	struct dw_pcie		*pci;
	void __iomem		*app_base;		/* DT app */
	void __iomem		*phy_ahb;		/* DT phy_ahb */
	struct phy		**phy;
	int pcie_init_before_kernel;
	int			link_gen;
	bool			link_is_up;
	struct irq_domain	*irq_domain;
	struct	clk *clk_master;
	struct	clk *clk_slave;
	struct reset_control *reset;
};

#define to_spacemit_pcie(x)	dev_get_drvdata((x)->dev)

static inline u32 spacemit_pcie_readl(struct spacemit_pcie *pcie, u32 offset)
{
	return readl(pcie->app_base + offset);
}

static inline void spacemit_pcie_writel(struct spacemit_pcie *pcie, u32 offset, u32 value)
{
	writel(value, pcie->app_base + offset);
}

static inline u32 spacemit_pcie_phy_ahb_readl(struct spacemit_pcie *pcie, u32 offset)
{
	return readl(pcie->phy_ahb + offset);
}

static inline void spacemit_pcie_phy_ahb_writel(struct spacemit_pcie *pcie, u32 offset, u32 value)
{
	writel(value, pcie->phy_ahb + offset);
}

int is_pcie_init = 1;
static int __init pcie_already_init(char *str)
{
	is_pcie_init = 1;
	return 0;
}
__setup("pcie_init", pcie_already_init);

static bool spacemit_pcie_link_up(struct dw_pcie *pci)
{
	struct spacemit_pcie *pcie = to_spacemit_pcie(pci);
	u32 reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_LINK_STS);

	return (reg & RDLH_LINK_UP) && (reg & SMLH_LINK_UP);
}

static void spacemit_pcie_stop_link(struct dw_pcie *pci)
{
	struct spacemit_pcie *pcie = to_spacemit_pcie(pci);
	u32 reg;

	reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
	reg &= ~LTSSM_EN;
	spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);
}

static int spacemit_pcie_establish_link(struct dw_pcie *pci)
{
	struct spacemit_pcie *pcie = to_spacemit_pcie(pci);
	struct device *dev = pci->dev;
	u32 reg;

	if (dw_pcie_link_up(pci)) {
		dev_err(dev, "link is already up\n");
		return 0;
	}

	reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
	reg |= LTSSM_EN;
	reg &= ~APP_HOLD_PHY_RST;
	spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);

	pr_debug("ltssm enable\n");
	return 0;
}

static void spacemit_pcie_enable_msi_interrupts(struct spacemit_pcie *pcie)
{
	u32 reg;

	reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQENABLE_SET_MSI);
	reg |= MSI;
	spacemit_pcie_phy_ahb_writel(pcie, SPACEMIT_PHY_AHB_IRQENABLE_SET_MSI, reg);

	reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX);
	reg |= LEG_EP_INTERRUPTS;
	spacemit_pcie_phy_ahb_writel(pcie, SPACEMIT_PHY_AHB_IRQENABLE_SET_INTX, reg);

	reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQ_EN);
	reg |= IRQ_EN;
	spacemit_pcie_phy_ahb_writel(pcie, SPACEMIT_PHY_AHB_IRQ_EN, reg);

	reg = spacemit_pcie_phy_ahb_readl(pcie, ADDR_INTR_ENABLE1);
	reg |= (MSI_INT | MSIX_INT);
	spacemit_pcie_phy_ahb_writel(pcie, ADDR_INTR_ENABLE1, reg);
}

static int __init spacemit_pcie_init_id(struct spacemit_pcie *pcie)
{
	struct dw_pcie *pci = pcie->pci;

	dw_pcie_dbi_ro_wr_en(pci);
	dw_pcie_writew_dbi(pci, PCI_VENDOR_ID, SPACEMIT_PCIE_VENDOR_ID);
	dw_pcie_writew_dbi(pci, PCI_DEVICE_ID, K3_PCIE_DEVICE_ID);
	dw_pcie_dbi_ro_wr_dis(pci);

	return 0;
}

static int spacemit_pcie_host_init(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct spacemit_pcie *pcie = to_spacemit_pcie(pci);
	u32 reg;

	mdelay(100);
	/* set Perst# gpio high state*/
	reg = spacemit_pcie_readl(pcie, PCIE_CTRL_LOGIC);
	reg &= ~PCIE_PERSTN_OE;
	spacemit_pcie_writel(pcie, PCIE_CTRL_LOGIC, reg);

	/* read the link status register, get the current speed */
	reg = dw_pcie_readw_dbi(pci, EXP_CAP_ID_OFFSET + PCI_EXP_LNKSTA);
	pr_debug("Link up, Gen%i\n", reg & PCI_EXP_LNKSTA_CLS);

	spacemit_pcie_init_id(pcie);
	spacemit_pcie_enable_msi_interrupts(pcie);

	return 0;
}

static int spacemit_pcie_intx_map(struct irq_domain *domain, unsigned int irq,
				  irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, domain->host_data);

	return 0;
}

static const struct irq_domain_ops intx_domain_ops = {
	.map = spacemit_pcie_intx_map,
	.xlate = pci_irqd_intx_xlate,
};

static int spacemit_pcie_init_irq_domain(struct dw_pcie_rp *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device *dev = pci->dev;
	struct spacemit_pcie *pcie = to_spacemit_pcie(pci);
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node =  of_get_next_child(node, NULL);

	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	pcie->irq_domain = irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
						 &intx_domain_ops, pp);
	if (!pcie->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		return -ENODEV;
	}

	return 0;
}

static const struct dw_pcie_host_ops spacemit_pcie_host_ops = {
	.init = spacemit_pcie_host_init,
};

static void (*spacemit_pcie_irq_callback)(int);

static void spacemit_pcie_set_irq_callback(void (*fn)(int))
{
	spacemit_pcie_irq_callback = fn;
}

/* local cpu interrupt, vendor specific*/
static irqreturn_t spacemit_pcie_irq_handler(int irq, void *arg)
{
	struct spacemit_pcie *pcie = arg;
	u32 reg_ahb;
	__maybe_unused u8 chan;

	reg_ahb = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQSTATUS_MSI);
	if (reg_ahb & DMA_READ_INT)
		pr_debug("dma read done irq reg_ahb=%x\n", reg_ahb);

	return IRQ_HANDLED;
}

static int __init spacemit_add_pcie_port(struct spacemit_pcie *pcie,
					 struct platform_device *pdev)
{
	int ret;
	struct dw_pcie *pci = pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = pci->dev;
	u32 reg;

	/* set Perst# (fundamental reset) gpio low state*/
	reg = spacemit_pcie_readl(pcie, PCIE_CTRL_LOGIC);
	reg |= PCIE_PERSTN_OE;
	spacemit_pcie_writel(pcie, PCIE_CTRL_LOGIC, reg);

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0) {
		dev_err(dev, "missing IRQ resource\n");
		return pp->irq;
	}

	ret = spacemit_pcie_init_irq_domain(pp);
	if (ret < 0)
		return ret;

	pp->ops = &spacemit_pcie_host_ops;
	pp->num_vectors = MAX_MSI_IRQS;
	ret = dw_pcie_host_init(pp);
	if (ret) {
		dev_err(dev, "failed to initialize host\n");
		return ret;
	}

	return 0;
}

static const struct dw_pcie_ops spacemit_pcie_ops = {
	.start_link = spacemit_pcie_establish_link,
	.stop_link = spacemit_pcie_stop_link,
	.link_up = spacemit_pcie_link_up,
};

static const struct of_device_id of_spacemit_pcie_match[] = {
	{
		.compatible = "spacemit,k3-pcie",
	},
	{},
};

static void spacemit_pcie_hold_phy_rst(struct spacemit_pcie *pcie)
{
	u32 reg;

	reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
	if (reg & APP_HOLD_PHY_RST) {
		dev_dbg(pcie->pci->dev, "%s: phy reset already held\n", __func__);
		return;
	}
	reg |= APP_HOLD_PHY_RST;
	spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);
}

static int spacemit_pcie_clk_enable(struct spacemit_pcie *pcie)
{
#ifdef CONFIG_SOC_SPACEMIT_K3_FPGA
	u32 reg;

	reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
	reg |= 0x7;
	spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);
	return 0;
#else
	int ret;

	pcie->clk_master = devm_clk_get(pcie->pci->dev, "pcie-master");
	if (IS_ERR(pcie->clk_master))
		return PTR_ERR(pcie->clk_master);

	pcie->clk_slave = devm_clk_get(pcie->pci->dev, "pcie-slave");
	if (IS_ERR(pcie->clk_slave))
		return PTR_ERR(pcie->clk_slave);

	ret = clk_prepare_enable(pcie->clk_master);
	if (ret)
		return ret;

	ret = clk_prepare_enable(pcie->clk_slave);
	if (ret) {
		clk_disable_unprepare(pcie->clk_master);
		return ret;
	}

	return 0;
#endif
}

static void spacemit_pcie_clk_disable(struct spacemit_pcie *pcie)
{
	clk_disable_unprepare(pcie->clk_slave);
	clk_disable_unprepare(pcie->clk_master);
}

static int __init spacemit_pcie_probe(struct platform_device *pdev)
{
	u32 reg;
	int ret;
	int irq;
	void __iomem *base;
	struct resource *res;
	struct dw_pcie *pci;
	struct spacemit_pcie *pcie;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	const struct of_device_id *match;

	match = of_match_device(of_match_ptr(of_spacemit_pcie_match), dev);
	if (!match)
		return -EINVAL;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = devm_kzalloc(dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = dev;
	pci->ops = &spacemit_pcie_ops;

	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		dev_err(dev, "missing IRQ resource: %d\n", irq);
		return irq;
	}
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "app");
	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_ahb");
	pcie->phy_ahb = devm_ioremap(dev, res->start, resource_size(res));
	if (!pcie->phy_ahb)
		return -ENOMEM;

	pcie->reset = devm_reset_control_get_optional(dev, NULL);
	if (IS_ERR(pcie->reset)) {
		dev_err(dev, "failed to get reset control\n");
		return PTR_ERR(pcie->reset);
	}

	pcie->app_base = base;
	pcie->pci = pci;
	platform_set_drvdata(pdev, pcie);

	ret = spacemit_pcie_clk_enable(pcie);
	if (ret) {
		dev_err(dev, "failed to enable PCIe clocks: %d\n", ret);
		return ret;
	}
	reset_control_assert(pcie->reset);
	spacemit_pcie_hold_phy_rst(pcie);
	reset_control_deassert(pcie->reset);

	pcie->pcie_init_before_kernel = is_pcie_init;
	if (is_pcie_init == 0) {
		reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
		reg &= ~LTSSM_EN;
		spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);
	}
	pcie->link_gen = of_pci_get_max_link_speed(np);
	if (pcie->link_gen < 0 || pcie->link_gen > 3)
		pcie->link_gen = 3;

	reg = spacemit_pcie_readl(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD);
	dev_info(dev, "pcie config device cmd fault: 0x%x\n", reg);
	reg |= PCIE_AUX_PWR_DET;
	spacemit_pcie_writel(pcie, PCIECTRL_SPACEMIT_CONF_DEVICE_CMD, reg);
	dev_info(dev, "pcie config device cmd in rc mode: 0x%x\n", reg);

	reg = spacemit_pcie_readl(pcie, PCIE_CTRL_LOGIC);
	reg |= PCIE_IGNORE_PERSTN;
	spacemit_pcie_writel(pcie, PCIE_CTRL_LOGIC, reg);

	ret = spacemit_add_pcie_port(pcie, pdev);
	if (ret < 0)
		goto err_clk;

	ret = devm_request_irq(dev, irq, spacemit_pcie_irq_handler,
			       IRQF_SHARED, "spacemit-pcie", pcie);
	if (ret) {
		dev_err(dev, "failed to request spacemit-pcie irq\n");
		goto err_clk;
	}

	return 0;

err_clk:
	spacemit_pcie_clk_disable(pcie);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int spacemit_pcie_wait_l2(struct spacemit_pcie *pcie)
{
	u32 value;
	u32 reg;
	int err;

	reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQ_EN);
	reg |= PME_TURN_OFF;
	spacemit_pcie_phy_ahb_writel(pcie, SPACEMIT_PHY_AHB_IRQ_EN, reg);
	udelay(1);
	reg = spacemit_pcie_phy_ahb_readl(pcie, SPACEMIT_PHY_AHB_IRQ_EN);
	reg &= ~PME_TURN_OFF;
	spacemit_pcie_phy_ahb_writel(pcie, SPACEMIT_PHY_AHB_IRQ_EN, reg);

	err = readl_poll_timeout(pcie->phy_ahb + SPACEMIT_PHY_AHB_LINK_STS,
				 value, PCIE_LINK_IS_L2(value), 20,
				 jiffies_to_usecs(5 * HZ));
	if (err) {
		pr_err("PCIe link enter L2 timeout!\n");
		return err;
	}

	return 0;
}

static int spacemit_pcie_suspend(struct device *dev)
{
	struct spacemit_pcie *pcie = dev_get_drvdata(dev);
	struct dw_pcie *pci = pcie->pci;
	u32 val;

	if (pcie->mode != DW_PCIE_RC_TYPE)
		return 0;

	/* clear MSE */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= ~PCI_COMMAND_MEMORY;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	return 0;
}

static int spacemit_pcie_resume(struct device *dev)
{
	struct spacemit_pcie *pcie = dev_get_drvdata(dev);
	struct dw_pcie *pci = pcie->pci;
	u32 val;

	if (pcie->mode != DW_PCIE_RC_TYPE)
		return 0;

	/* set MSE */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val |= PCI_COMMAND_MEMORY;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

	return 0;
}

static int spacemit_pcie_suspend_noirq(struct device *dev)
{
	struct spacemit_pcie *pcie = dev_get_drvdata(dev);
	struct dw_pcie  *pci = pcie->pci;
	u32 reg;

	pcie->link_is_up = dw_pcie_link_up(pci);
	dev_info(dev, "link is %s\n", pcie->link_is_up ? "up" : "down");

	if (pcie->link_is_up)
		spacemit_pcie_wait_l2(pcie);
	spacemit_pcie_disable_phy(pcie);

	return 0;
}

static int spacemit_pcie_resume_noirq(struct device *dev)
{
	struct spacemit_pcie *pcie = dev_get_drvdata(dev);
	struct dw_pcie  *pci = pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	u32 reg;

	ret = spacemit_pcie_enable_phy(pcie);
	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		return ret;
	}

	return 0;
}
#endif

static const struct dev_pm_ops spacemit_pcie_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(spacemit_pcie_suspend, spacemit_pcie_resume)
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(spacemit_pcie_suspend_noirq,
				      spacemit_pcie_resume_noirq)
};

static struct platform_driver spacemit_pcie_driver = {
	.probe = spacemit_pcie_probe,
	.driver = {
		.name	= "spacemit-pcie",
		.of_match_table = of_spacemit_pcie_match,
		.suppress_bind_attrs = true,
		.pm	= &spacemit_pcie_pm_ops,
	},
};
module_platform_driver(spacemit_pcie_driver);
