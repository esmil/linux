// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include "spacemit-backlight.h"

#define DEV_NAME			"spacemit-backlight"

struct spacemit_bl_device {
	struct spacemit_bl_ops *ops;
	void *devdata;
	int max_brightness;
	int brightness;

	struct platform_device pdev;
};

static ssize_t spacemit_bl_get_brightness(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct spacemit_bl_device *bld = dev_get_platdata(dev);
	int brightness = 0;

	if (bld->ops && bld->ops->get_bl)
		brightness = bld->ops->get_bl(bld->devdata);
	else
		brightness = bld->brightness;

	return snprintf(buf, PAGE_SIZE, "%d\n", brightness);
}

static ssize_t spacemit_bl_get_max_brightness(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct spacemit_bl_device *bld = dev_get_platdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", bld->max_brightness);
}

static ssize_t spacemit_bl_set_brightness(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct spacemit_bl_device *bld = dev_get_platdata(dev);
	int old_brightness;
	int new_brightness;
	int ret;

	if (!bld->ops || !bld->ops->set_bl)
		return -ENODEV;

	ret = sscanf(buf, "%u\n", &new_brightness);
	if (ret != 1 || new_brightness > bld->max_brightness) {
		pr_err("Wrong parameter! please echo 0 ~ %d.\n", bld->max_brightness);
		return -EINVAL;
	}

	if (bld->ops->get_bl)
		old_brightness = bld->ops->get_bl(bld->devdata);
	else
		old_brightness = bld->brightness;

	if (old_brightness != new_brightness) {
		bld->brightness = new_brightness;
		bld->ops->set_bl(bld->devdata, new_brightness);
	}

	return count;
}

static DEVICE_ATTR(brightness, S_IRUGO | S_IWUSR, spacemit_bl_get_brightness, spacemit_bl_set_brightness);
static DEVICE_ATTR(max_brightness, S_IRUGO, spacemit_bl_get_max_brightness, NULL);

static int spacemit_backlight_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = device_create_file(dev, &dev_attr_brightness);
	if (ret)
		pr_err("failed to create device file: brightness\n");
	else
		pr_debug("create device file brightness\n");

	ret = device_create_file(dev, &dev_attr_max_brightness);
	if (ret)
		pr_err("failed to create device file: max_brightness\n");
	else
		pr_debug("create device file max_brightness\n");

	return 0;
}

static void spacemit_backlight_remove(struct platform_device *pdev)
{
}

static void spacemit_backlight_shutdown(struct platform_device *pdev)
{
	struct spacemit_bl_device *bld = container_of(pdev, struct spacemit_bl_device, pdev);

	if (bld->ops && bld->ops->set_bl) {
		bld->brightness = 0;
		bld->ops->set_bl(bld->devdata, 0);
	}
}

int spacemit_bl_device_register(struct device *parent, void *devdata, struct spacemit_bl_ops *ops, int max)
{
	struct spacemit_bl_device *bld;
	int ret;

	bld = kzalloc(sizeof(struct spacemit_bl_device), GFP_KERNEL);
	if (!bld)
		return -ENOMEM;

	bld->pdev.dev.parent = NULL;
	bld->pdev.dev.platform_data = bld;
	bld->pdev.name = DEV_NAME;
	bld->devdata = devdata;
	bld->max_brightness = max;
	bld->ops = ops;

	ret = platform_device_register(&bld->pdev);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(spacemit_bl_device_register);

static const struct of_device_id spacemit_backlight_dt_match[] = {
	{ .compatible = "spacemit,backlight", },
	{ },
};
MODULE_DEVICE_TABLE(of, spacemit_backlight_dt_match);

static struct platform_driver spacemit_backlight_driver = {
	.driver = {
		.name = DEV_NAME,
		.of_match_table = of_match_ptr(spacemit_backlight_dt_match),
	},
	.probe = spacemit_backlight_probe,
	.remove = spacemit_backlight_remove,
	.shutdown = spacemit_backlight_shutdown,
};
module_platform_driver(spacemit_backlight_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPACEMIT Backlight Driver");
