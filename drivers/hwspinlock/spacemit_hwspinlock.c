// SPDX-License-Identifier: GPL-2.0
/*
 * spacemit hwspinlock driver
 *
 * Copyright (C) 2025 Spacemit
 *
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/delay.h>
#include "hwspinlock_internal.h"

#define SPINLOCK_LOCK(x)	(0x0 + 0x4 * x)
#define SPINLOCK_VER		0x100
#define SPINLOCK_SSTAT		0x104
#define SPINLOCK_STATUS		0x108
#define SPINLOCK_IRQ_EN		0x110
#define SPINLOCK_IRQ_STA	0x114
#define SPINLOCK_NOTTAKEN	0
#define SPINLOCK_BASE_ID	0

struct spacemit_hwspinlock_data {
	struct hwspinlock_device *bank;
	void __iomem *base;
	int num;
};

static int spacemit_hwspinlock_trylock(struct hwspinlock *lock)
{
	void __iomem *reg = lock->priv;

	return (readl(reg) == SPINLOCK_NOTTAKEN);
}

static void spacemit_hwspinlock_unlock(struct hwspinlock *lock)
{
	void __iomem *reg = lock->priv;

	writel(SPINLOCK_NOTTAKEN, reg);
}

static const struct hwspinlock_ops spacemit_hwspinlock_ops = {
	.trylock	= spacemit_hwspinlock_trylock,
	.unlock		= spacemit_hwspinlock_unlock,
};

static void spacemit_hwspinlock_disable(void *data)
{
	return;
}

static int spacemit_hwspinlock_probe(struct platform_device *pdev)
{
	struct spacemit_hwspinlock_data *data;
	struct hwspinlock *lock;
	int err, i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->base = devm_platform_ioremap_resource(pdev, SPINLOCK_BASE_ID);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	data->num = readl(data->base + SPINLOCK_SSTAT);
	data->bank = devm_kzalloc(&pdev->dev, struct_size(data->bank, lock, data->num),
				GFP_KERNEL);
	if (!data->bank) {
		err = -ENOMEM;
		goto bank_fail;
	}

	for (i = 0; i < data->num; i++) {
		lock = &data->bank->lock[i];
		lock->priv = data->base + SPINLOCK_LOCK(i);
	}

	err = devm_add_action_or_reset(&pdev->dev, spacemit_hwspinlock_disable, data);
	if (err) {
		dev_err(&pdev->dev, "failed to add hwspinlock disable action\n");
		goto bank_fail;
	}

	platform_set_drvdata(pdev, data);

	return devm_hwspin_lock_register(&pdev->dev, data->bank, &spacemit_hwspinlock_ops,
					 SPINLOCK_BASE_ID, data->num);

bank_fail:
	return err;
}

static const struct of_device_id spacemit_hwspinlock_ids[] = {
	{ .compatible = "spacemit,hwspinlock", },
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_hwspinlock_ids);

static struct platform_driver spacemit_hwspinlock_driver = {
	.probe = spacemit_hwspinlock_probe,
	.driver = {
		.name		= "spacemit_hwspinlock",
		.of_match_table = spacemit_hwspinlock_ids,
	},
};
module_platform_driver(spacemit_hwspinlock_driver);
