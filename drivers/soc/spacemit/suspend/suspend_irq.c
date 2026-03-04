// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spacemit irq set affinity when standby
 *
 * Copyright (c) 2026 SPACEMIT, Co. Ltd.
 */

#include <linux/module.h>
#include <linux/msi.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/pm.h>
#include "../../../irqchip/irq-riscv-imsic-state.h"

#define MAX_IRQ_NUM		350
struct pm_irq_affinity {
	unsigned int irq;
	unsigned int cpu;
};
static struct pm_irq_affinity *irqaff;

static int active_irq_num;
static struct irq_domain *affi_domain = NULL;

static int irq_affinity_suspend(struct device *dev)
{
	struct irq_data *d;
	struct imsic_vector *vec;
	int irq, i = 0;

	active_irq_num = 0;
	for_each_active_irq(irq) {
		active_irq_num++;
	}

	irqaff = kmalloc(sizeof(struct pm_irq_affinity) * active_irq_num, GFP_KERNEL);
	if (!irqaff) {
		pr_err("failed to alloc mem for irq affinity\n");
		return -ENOMEM;
	}

	for_each_active_irq(irq) {
		d = irq_domain_get_irq_data(affi_domain, irq);
		if (!d)
			continue;

		vec = irq_data_get_irq_chip_data(d);
		if (!vec)
			continue;

		irqaff[i].irq = irq;
		irqaff[i].cpu = vec->cpu;
		i++;
	}

	active_irq_num = i;
	return 0;
}


static int irq_affinity_resume(struct device *dev)
{
	int i, err;

	for (i = 0; i < active_irq_num; i++) {
		err = irq_set_affinity(irqaff[i].irq, cpumask_of(irqaff[i].cpu));
		if (err) {
			pr_err("IRQ%u migrate to cpu%u failed\n", irqaff[i].irq, irqaff[i].cpu);
		}
	}

	return 0;
}

static const struct dev_pm_ops irq_affinity_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(irq_affinity_suspend, irq_affinity_resume)
};

static irqreturn_t affi_irq(int irq, void *devid)
{
	return IRQ_HANDLED;
}

static void affi_irq_write(struct msi_desc *desc, struct msi_msg *msg)
{
	return;
}

static int irq_affinity_probe(struct platform_device *pdev)
{
	struct device *dev;
	int msi_irq, ret = 0;
	struct irq_desc *desc;
	struct irq_data *d;

	dev = &pdev->dev;
	if (!dev_get_msi_domain(dev)) {
		if (is_of_node(dev->fwnode))
			of_msi_configure(dev, to_of_node(dev->fwnode));
	}
	ret = platform_device_msi_init_and_alloc_irqs(dev, 1, affi_irq_write);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret, "Failed to allocate 1 MSI\n");
	}
	msi_irq = msi_get_virq(&pdev->dev, 0);
	ret = request_irq(msi_irq, affi_irq, IRQF_SHARED, dev_name(&pdev->dev), &pdev->dev);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret, "failed to request msi irq\n");
	}
	desc = irq_to_desc(msi_irq);
	d = irq_desc_get_irq_data(desc)->parent_data;
	affi_domain = d->domain;

	active_irq_num = 0;

	return 0;
}

static void irq_affinity_remove(struct platform_device *pdev)
{
	return;
}

static const struct of_device_id irq_affinity_match[] = {
	{ .compatible = "spacemit,irq-affinity-restore", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, irq_affinity_match);

static struct platform_driver irq_affinity_driver = {
	.probe		= irq_affinity_probe,
	.remove		= irq_affinity_remove,
	.driver		= {
		.name	= "irq-affinity-restore",
		.pm	= &irq_affinity_pm_ops,
		.of_match_table = of_match_ptr(irq_affinity_match),
	},
};
module_platform_driver(irq_affinity_driver);
