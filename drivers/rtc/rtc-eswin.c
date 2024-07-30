// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN rtc driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Author: zhangpengcheng@eswincomputing.com
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#define RTC_INT_TO_U84  0xffff9fff
/* RTC CSR Registers */
#define RTC_CCVR        0x00
#define RTC_CMR         0x04
#define RTC_CLR         0x08
#define RTC_CCR         0x0C
#define RTC_CCR_IE      BIT(0)
#define RTC_CCR_MASK    BIT(1)
#define RTC_CCR_EN      BIT(2)
#define RTC_CCR_WEN     BIT(3)
#define RTC_CCR_PEN     BIT(4)
#define RTC_STAT        0x10
#define RTC_STAT_BIT    BIT(0)
#define RTC_RSTAT       0x14
#define RTC_EOI         0x18
#define RTC_VER         0x1C
#define RTC_CPSR        0x20
#define RTC_CPCVR       0x24

struct eswin_rtc_dev {
	struct rtc_device *rtc;
	struct device *dev;
	unsigned long alarm_time;
	void __iomem *csr_base;
	struct clk *clk;
	unsigned int irq_wake;
	struct reset_control *rst_rtc;
};

static int eswin_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct eswin_rtc_dev *pdata = dev_get_drvdata(dev);
	rtc_time64_to_tm(readl(pdata->csr_base + RTC_CCVR), tm);
	return rtc_valid_tm(tm);
}

static int eswin_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct eswin_rtc_dev *pdata = dev_get_drvdata(dev);
	unsigned long tr;

	tr = rtc_tm_to_time64(tm);
	writel(tr, pdata->csr_base + RTC_CLR);
	readl(pdata->csr_base + RTC_CLR); /* Force a barrier */

	return 0;
}

static int eswin_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct eswin_rtc_dev *pdata = dev_get_drvdata(dev);
	rtc_time64_to_tm(pdata->alarm_time, &alrm->time);
	alrm->enabled = readl(pdata->csr_base + RTC_CCR) & RTC_CCR_IE;

	return 0;
}

static int eswin_rtc_alarm_irq_enable(struct device *dev, u32 enabled)
{
	struct eswin_rtc_dev *pdata = dev_get_drvdata(dev);
	u32 ccr;

	ccr = readl(pdata->csr_base + RTC_CCR);
	if (enabled) {
		ccr &= ~RTC_CCR_MASK;
		ccr |= RTC_CCR_IE;
	} else {
		ccr &= ~RTC_CCR_IE;
		ccr |= RTC_CCR_MASK;
	}
	writel(ccr, pdata->csr_base + RTC_CCR);

	return 0;
}

static int eswin_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct eswin_rtc_dev *pdata = dev_get_drvdata(dev);
	unsigned long rtc_time;
	unsigned long alarm_time;
	rtc_time = readl(pdata->csr_base + RTC_CCVR);
	alarm_time = rtc_tm_to_time64(&alrm->time);

	pdata->alarm_time = alarm_time;
	writel((u32) pdata->alarm_time, pdata->csr_base + RTC_CMR);

	eswin_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static const struct rtc_class_ops eswin_rtc_ops = {
	.read_time  = eswin_rtc_read_time,
	.set_time   = eswin_rtc_set_time,
	.read_alarm = eswin_rtc_read_alarm,
	.set_alarm  = eswin_rtc_set_alarm,
	.alarm_irq_enable = eswin_rtc_alarm_irq_enable,
};

static irqreturn_t eswin_rtc_interrupt(int irq, void *id)
{
	struct eswin_rtc_dev *pdata = (struct eswin_rtc_dev *) id;
	/* Check if interrupt asserted */
	if (!(readl(pdata->csr_base + RTC_STAT) & RTC_STAT_BIT))
		return IRQ_NONE;

	/* Clear interrupt */
	readl(pdata->csr_base + RTC_EOI);

	rtc_update_irq(pdata->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int eswin_rtc_probe(struct platform_device *pdev)
{
	struct eswin_rtc_dev *pdata;
	struct resource *res;
	int ret;
	int irq;
	unsigned int reg_val;
	unsigned int int_off;
	unsigned int clk_freq;
	struct regmap *regmap;
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);
	pdata->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->csr_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->csr_base))
		return PTR_ERR(pdata->csr_base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return irq;
	}
	ret = devm_request_irq(&pdev->dev, irq, eswin_rtc_interrupt, 0,
                   dev_name(&pdev->dev), pdata);
	if (ret) {
		dev_err(&pdev->dev, "Could not request IRQ\n");
		return ret;
	}

	/* update RTC interrupt to u84 */
	regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "eswin,syscfg");
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(regmap);
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "eswin,syscfg", 1, &int_off);
	if (ret) {
		dev_err(&pdev->dev, "No rtc interrupt offset found\n");
		return -1;
	}
	regmap_read(regmap, int_off, &reg_val);
	reg_val &= (RTC_INT_TO_U84);
	regmap_write(regmap, int_off, reg_val);

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency", &clk_freq);
	if (ret) {
		dev_err(&pdev->dev, "No rtc clock-frequency found\n");
	}
	/* rtc reset init*/
	pdata->rst_rtc = devm_reset_control_get_optional(&pdev->dev, "rtcrst");
	if (IS_ERR_OR_NULL(pdata->rst_rtc)) {
		dev_err(&pdev->dev, "Failed to get rtcrst reset handle\n");
		return -EFAULT;
	}

	/* get RTC clock */
	pdata->clk = devm_clk_get(&pdev->dev, "rtcclk");
	if (IS_ERR(pdata->clk)) {
		dev_err(&pdev->dev, "Couldn't get the clock for RTC\n");
		return -ENODEV;
	}
	/* Enable the clock */
	clk_prepare_enable(pdata->clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable RTC clock: %d\n", ret);
		return -ENODEV;
	}
	/* reset rtc */
	ret = reset_control_assert(pdata->rst_rtc);
	WARN_ON(0 != ret);
	ret = reset_control_deassert(pdata->rst_rtc);
	WARN_ON(0 != ret);

	/* Turn on the clock and the crystal */
	reg_val = readl(pdata->csr_base + RTC_CCR);
	writel(RTC_CCR_EN | reg_val, pdata->csr_base + RTC_CCR);

	/* Turn on the prescaler and set the value */
	writel(clk_freq, pdata->csr_base + RTC_CPSR);
	reg_val = readl(pdata->csr_base + RTC_CCR);
	writel(RTC_CCR_PEN | reg_val, pdata->csr_base + RTC_CCR);

	device_init_wakeup(&pdev->dev, 1);

	pdata->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
                     &eswin_rtc_ops, THIS_MODULE);
	if (IS_ERR(pdata->rtc)) {
		clk_disable_unprepare(pdata->clk);
		return PTR_ERR(pdata->rtc);
	}

	return 0;
}

static void eswin_rtc_remove(struct platform_device *pdev)
{
	struct eswin_rtc_dev *pdata = platform_get_drvdata(pdev);

	eswin_rtc_alarm_irq_enable(&pdev->dev, 0);
	device_init_wakeup(&pdev->dev, 0);
	clk_disable_unprepare(pdata->clk);
}

#ifdef CONFIG_PM_SLEEP
static int eswin_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct eswin_rtc_dev *pdata = platform_get_drvdata(pdev);
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (device_may_wakeup(&pdev->dev)) {
		if (!enable_irq_wake(irq))
			pdata->irq_wake = 1;
	} else {
		eswin_rtc_alarm_irq_enable(dev, 0);
		clk_disable(pdata->clk);
	}

	return 0;
}

static int eswin_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct eswin_rtc_dev *pdata = platform_get_drvdata(pdev);
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (device_may_wakeup(&pdev->dev)) {
		if (pdata->irq_wake) {
			disable_irq_wake(irq);
			pdata->irq_wake = 0;
		}
	} else {
		clk_enable(pdata->clk);
		eswin_rtc_alarm_irq_enable(dev, 1);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(eswin_rtc_pm_ops, eswin_rtc_suspend, eswin_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id eswin_rtc_of_match[] = {
	{.compatible = "eswin,eic7700-rtc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eswin_rtc_of_match);
#endif

static struct platform_driver eswin_rtc_driver = {
	.probe      = eswin_rtc_probe,
	.remove     = eswin_rtc_remove,
	.driver     = {
		.name   = "eswin-rtc",
		.pm = &eswin_rtc_pm_ops,
		.of_match_table = of_match_ptr(eswin_rtc_of_match),
	},
};

module_platform_driver(eswin_rtc_driver);

MODULE_DESCRIPTION("eswin eic7700 RTC driver");
MODULE_AUTHOR("zhangpengcheng@eswin.com>");
MODULE_LICENSE("GPL");
