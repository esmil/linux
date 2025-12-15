// SPDX-License-Identifier: GPL-2.0-only
/*
 * dwc3-generic-plat.c - DesignWare USB3 generic platform driver
 *
 * Copyright (C) 2025 Ze Huang <huang.ze@linux.dev>
 *
 * Inspired by dwc3-qcom.c and dwc3-of-simple.c
 */

#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/pm_wakeirq.h>
#include "glue.h"

#ifdef CONFIG_SOC_SPACEMIT
#define SPACEMIT_K1_LFPS_WAKE_STATUS BIT(29)
#define SPACEMIT_K1_CDWS_WAKE_STATUS BIT(28)
#define SPACEMIT_K1_ID_WAKE_STATUS BIT(27)
#define SPACEMIT_K1_VBUS_WAKE_STATUS BIT(26)
#define SPACEMIT_K1_LINS1_WAKE_STATUS BIT(25)
#define SPACEMIT_K1_LINS0_WAKE_STATUS BIT(24)

#define SPACEMIT_K1_CDWS_WAKE_CLEAR BIT(20)
#define SPACEMIT_K1_ID_WAKE_CLEAR BIT(19)
#define SPACEMIT_K1_VBUS_WAKE_CLEAR BIT(18)
#define SPACEMIT_K1_LINS1_WAKE_CLEAR BIT(17)
#define SPACEMIT_K1_LINS0_WAKE_CLEAR BIT(16)
#define SPACEMIT_K1_LFPS_WAKE_CLEAR BIT(14)

#define SPACEMIT_K1_WAKEUP_INT_MASK BIT(15)
#define SPACEMIT_K1_LFPS_WAKE_MASK BIT(13)
#define SPACEMIT_K1_CDWS_WAKE_MASK BIT(12)
#define SPACEMIT_K1_ID_WAKE_MASK BIT(11)
#define SPACEMIT_K1_VBUS_WAKE_MASK BIT(10)
#define SPACEMIT_K1_LINS1_WAKE_MASK BIT(9)
#define SPACEMIT_K1_LINS0_WAKE_MASK BIT(8)
#endif
struct dwc3_generic {
	struct device		*dev;
	struct dwc3		dwc;
	struct clk_bulk_data	*clks;
	int			num_clocks;
	struct reset_control	*resets;
#ifdef CONFIG_SOC_SPACEMIT
	bool reset_on_resume;
	void *priv;
#endif
};

#define to_dwc3_generic(d) container_of((d), struct dwc3_generic, dwc)

#ifdef CONFIG_SOC_SPACEMIT
struct spacemit_k1_privdata {
	void __iomem *wakeup_reg;
	int wakeup_irq;
};

static void __maybe_unused
spacemit_k1_enable_wakeup_irqs(struct dwc3_generic *dwc3g)
{
	struct spacemit_k1_privdata *priv = dwc3g->priv;
	u32 reg;

	reg = readl(priv->wakeup_reg);
	reg |= (SPACEMIT_K1_LFPS_WAKE_MASK | SPACEMIT_K1_LINS0_WAKE_MASK |
		SPACEMIT_K1_LINS1_WAKE_MASK);
	writel(reg, priv->wakeup_reg);
}

static void spacemit_k1_disable_wakeup_irqs(struct dwc3_generic *dwc3g)
{
	struct spacemit_k1_privdata *priv = dwc3g->priv;
	u32 reg;

	reg = readl(priv->wakeup_reg);
	reg &= ~(SPACEMIT_K1_LFPS_WAKE_MASK | SPACEMIT_K1_LINS0_WAKE_MASK |
		 SPACEMIT_K1_LINS1_WAKE_MASK);
	writel(reg, priv->wakeup_reg);
}

static void spacemit_k1_clear_wakeup_irqs(struct dwc3_generic *dwc3g)
{
	struct spacemit_k1_privdata *priv = dwc3g->priv;
	u32 reg;

	reg = readl(priv->wakeup_reg);
	reg |= (SPACEMIT_K1_LFPS_WAKE_CLEAR | SPACEMIT_K1_LINS0_WAKE_CLEAR |
		SPACEMIT_K1_LINS1_WAKE_CLEAR);
	writel(reg, priv->wakeup_reg);
}

static irqreturn_t spacemit_k1_wakeup_interrupt(int irq, void *data)
{
	struct dwc3_generic *dwc3g = data;
	struct spacemit_k1_privdata *priv = dwc3g->priv;
	u32 reg;

	reg = readl(priv->wakeup_reg);

	spacemit_k1_disable_wakeup_irqs(dwc3g);
	spacemit_k1_clear_wakeup_irqs(dwc3g);

	return IRQ_HANDLED;
}
#endif

static void dwc3_generic_reset_control_assert(void *data)
{
	reset_control_assert(data);
}

static int dwc3_generic_probe(struct platform_device *pdev)
{
	struct dwc3_probe_data probe_data = {};
	struct device *dev = &pdev->dev;
	struct dwc3_generic *dwc3g;
	struct resource *res;
	int ret;

	dwc3g = devm_kzalloc(dev, sizeof(*dwc3g), GFP_KERNEL);
	if (!dwc3g)
		return -ENOMEM;

	dwc3g->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing memory resource\n");
		return -ENODEV;
	}

	dwc3g->resets = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(dwc3g->resets))
		return dev_err_probe(dev, PTR_ERR(dwc3g->resets), "failed to get resets\n");

	ret = reset_control_assert(dwc3g->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to assert resets\n");

	/* Not strict timing, just for safety */
	udelay(2);

	ret = reset_control_deassert(dwc3g->resets);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert resets\n");

	ret = devm_add_action_or_reset(dev, dwc3_generic_reset_control_assert, dwc3g->resets);
	if (ret)
		return ret;

	ret = devm_clk_bulk_get_all_enabled(dwc3g->dev, &dwc3g->clks);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to get clocks\n");

#ifdef CONFIG_SOC_SPACEMIT
	dwc3g->reset_on_resume =
		device_property_read_bool(dev, "reset-on-resume");

	if (of_device_is_compatible(dev->of_node, "spacemit,k1-dwc3")) {
		if (device_property_read_bool(dev, "wakeup-source")) {
			if (dwc3g->reset_on_resume)
				return dev_err_probe(
					dev, -EINVAL,
					"cannot both reset and wakeup during suspend\n");

			struct spacemit_k1_privdata *priv;
			struct resource *wakeup_res;

			priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
			if (!priv)
				return -ENOMEM;

			wakeup_res =
				platform_get_resource(pdev, IORESOURCE_MEM, 1);
			if (!res)
				return dev_err_probe(
					dev, -ENOMEM,
					"cannot get wakeup reg resource\n");

			priv->wakeup_reg =
				devm_ioremap(dev, wakeup_res->start,
					     resource_size(wakeup_res));
			if (IS_ERR(priv->wakeup_reg))
				return dev_err_probe(
					dev, PTR_ERR(priv->wakeup_reg),
					"Failed to get wakeup reg\n");

			priv->wakeup_irq = platform_get_irq(pdev, 1);
			if (priv->wakeup_irq < 0) {
				dev_err(dev, "missing IRQ resource\n");
				return -EINVAL;
			}

			dwc3g->priv = priv;
			ret = devm_request_irq(dev, priv->wakeup_irq,
					       spacemit_k1_wakeup_interrupt,
					       IRQF_NO_SUSPEND, dev_name(dev),
					       dwc3g);
			if (ret) {
				dev_err(dev,
					"failed to request IRQ #%d --> %d\n",
					priv->wakeup_irq, ret);
				return ret;
			}
			device_init_wakeup(dev, true);
			dev_pm_set_wake_irq(dev, priv->wakeup_irq);
		}
	}
#endif

	dwc3g->num_clocks = ret;
	dwc3g->dwc.dev = dev;
	probe_data.dwc = &dwc3g->dwc;
	probe_data.res = res;
	probe_data.ignore_clocks_and_resets = true;
	ret = dwc3_core_probe(&probe_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register DWC3 Core\n");

	return 0;
}

static void dwc3_generic_remove(struct platform_device *pdev)
{
	struct dwc3 *dwc = platform_get_drvdata(pdev);

	dwc3_core_remove(dwc);
}

static int dwc3_generic_suspend(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_generic *dwc3g = to_dwc3_generic(dwc);
	int ret;

	ret = dwc3_pm_suspend(dwc);
	if (ret)
		return ret;

	clk_bulk_disable_unprepare(dwc3g->num_clocks, dwc3g->clks);

#ifdef CONFIG_SOC_SPACEMIT
	if (dwc3g->reset_on_resume) {
		ret = reset_control_assert(dwc3g->resets);
		if (ret)
			return ret;
	}

	if (of_device_is_compatible(dev->of_node, "spacemit,k1-dwc3") &&
	    device_may_wakeup(dev)) {
		spacemit_k1_clear_wakeup_irqs(dwc3g);
		spacemit_k1_enable_wakeup_irqs(dwc3g);
	}
#endif

	return 0;
}

static int dwc3_generic_resume(struct device *dev)
{
	struct dwc3 *dwc = dev_get_drvdata(dev);
	struct dwc3_generic *dwc3g = to_dwc3_generic(dwc);
	int ret;

#ifdef CONFIG_SOC_SPACEMIT
	if (dwc3g->reset_on_resume) {
		ret = reset_control_deassert(dwc3g->resets);
		if (ret)
			return ret;
	}
#endif

	ret = clk_bulk_prepare_enable(dwc3g->num_clocks, dwc3g->clks);
	if (ret)
		return ret;

	ret = dwc3_pm_resume(dwc);
	if (ret)
		return ret;

	return 0;
}

static int dwc3_generic_runtime_suspend(struct device *dev)
{
	return dwc3_runtime_suspend(dev_get_drvdata(dev));
}

static int dwc3_generic_runtime_resume(struct device *dev)
{
	return dwc3_runtime_resume(dev_get_drvdata(dev));
}

static int dwc3_generic_runtime_idle(struct device *dev)
{
	return dwc3_runtime_idle(dev_get_drvdata(dev));
}

static const struct dev_pm_ops dwc3_generic_dev_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(dwc3_generic_suspend, dwc3_generic_resume)
	RUNTIME_PM_OPS(dwc3_generic_runtime_suspend, dwc3_generic_runtime_resume,
		       dwc3_generic_runtime_idle)
};

static const struct of_device_id dwc3_generic_of_match[] = {
	{ .compatible = "spacemit,k1-dwc3", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dwc3_generic_of_match);

static struct platform_driver dwc3_generic_driver = {
	.probe		= dwc3_generic_probe,
	.remove		= dwc3_generic_remove,
	.driver		= {
		.name	= "dwc3-generic-plat",
		.of_match_table = dwc3_generic_of_match,
		.pm	= pm_ptr(&dwc3_generic_dev_pm_ops),
	},
};
module_platform_driver(dwc3_generic_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DesignWare USB3 generic platform driver");
