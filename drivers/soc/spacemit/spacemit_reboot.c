// SPDX-License-Identifier: GPL-2.0
/*
 * Spacemit k3 soc fastboot mode reboot
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <asm/sbi.h>

#define FLAG_FASTBOOT	0x1
#define FLAG_UPDATER	0x3

static const char *fastboot_cmd = "fastboot";
static const char *updater_cmd = "updater";

struct spacemit_reboot_ctrl {
	void __iomem *base;
	struct notifier_block reset_handler;
};

static int k3_reset_handler(struct notifier_block *this, unsigned long mode, void *cmd)
{
	struct spacemit_reboot_ctrl *info = container_of(this, struct spacemit_reboot_ctrl,
							 reset_handler);
	if (cmd == NULL) {
		pr_info("spacemit reboot: regular reboot\n");
		return NOTIFY_DONE;
	}

	/* Inform RCPU to write related register in P1 */
	if (!strcmp(cmd, fastboot_cmd)) {
		writel(FLAG_FASTBOOT, info->base);
		pr_info("spacemit reboot: fastboot reboot\n");
	} else if (!strcmp(cmd, updater_cmd)) {
		writel(FLAG_UPDATER, info->base);
		pr_info("spacemit reboot: updater reboot\n");
	} else {
		pr_info("spacemit reboot: regular reboot\n");
		return NOTIFY_DONE;
	}
	sbi_ecall(SBI_EXT_SRST, SBI_EXT_SRST_RESET, SBI_SRST_RESET_TYPE_COLD_REBOOT,
		  SBI_SRST_RESET_REASON_NONE, 0, 0, 0, 0);

	while (1);
	return NOTIFY_DONE;
}

static const struct of_device_id spacemit_reboot_of_match[] = {
	{.compatible = "spacemit,k3-reboot"},
	{},
};
MODULE_DEVICE_TABLE(of, spacemit_reboot_of_match);


static int spacemit_reboot_probe(struct platform_device *pdev)
{
	struct spacemit_reboot_ctrl *info;
	int ret;

	dev_info(&pdev->dev, "initializing...\n");

	info = devm_kzalloc(&pdev->dev, sizeof(struct spacemit_reboot_ctrl), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	info->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->base)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(info->base),
				     "failed to ioremap resource\n");
	}

	info->reset_handler.notifier_call = k3_reset_handler;
	info->reset_handler.priority = 255;
	ret = register_restart_handler(&info->reset_handler);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "cannot register restart handler\n");
	}

	platform_set_drvdata(pdev, info);

	/* Clear reserved SRAM buffer */
	writel(0x0, info->base);

	return 0;
}

static void spacemit_reboot_remove(struct platform_device *pdev)
{
	struct spacemit_reboot_ctrl *info = platform_get_drvdata(pdev);

	unregister_restart_handler(&info->reset_handler);
}

static struct platform_driver spacemit_reboot_driver = {
	.driver = {
		.name = "spacemit-reboot",
		.of_match_table = of_match_ptr(spacemit_reboot_of_match),
	},
	.probe = spacemit_reboot_probe,
	.remove = spacemit_reboot_remove,
};

module_platform_driver(spacemit_reboot_driver);
MODULE_DESCRIPTION("K3 fastboot mode reboot");
MODULE_AUTHOR("Spacemit");
MODULE_LICENSE("GPL v2");
