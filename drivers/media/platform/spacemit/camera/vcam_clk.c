/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 SPACEMIT Micro Limited
 * All Rights Reserved.
 */
#define DEBUG

#include <linux/printk.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/reset.h>
#include "vcam_clk.h"

/**
 * vcam_get_dt_reset_info - get reset resource from device tree
 * @dev: device pointer
 * @rst_name: reset name in device tree
 * @rst: return reset_control pointer
 *
 * Return: 0 on success, negative errno otherwise
 */
int vcam_get_dt_reset_info(struct device *dev, const char *rst_name,
			   struct reset_control **rst)
{
	int rc;

	if (!dev || !rst_name || !rst) {
		pr_err("%s: invalid parameter\n", __func__);
		return -EINVAL;
	}

	*rst = devm_reset_control_get_optional_shared(dev, rst_name);
	if (IS_ERR_OR_NULL(*rst)) {
		rc = PTR_ERR(*rst);
		dev_err(dev, "failed to get reset %s, rc=%d\n", rst_name, rc);
		*rst = NULL;
		return rc;
	}

	dev_dbg(dev, "reset %s get success\n", rst_name);
	return 0;
}
EXPORT_SYMBOL_GPL(vcam_get_dt_reset_info);

/**
 * vcam_get_dt_clk_info - get clk resourse and assigned rates in probe
 *
 * @dev:
 * @clk_name:
 * @clk_info:
 *
 * Return: 0 on success, error code otherwise.
 */
int vcam_get_dt_clk_info(struct device *dev, const char *clk_name,
			 struct vcam_clk_info *clk_info)
{
	struct device_node *np = dev->of_node;
	int rc;

	if (!dev || !clk_info) {
		pr_err("%s: invald dev=%p or clk_info=%p", __func__, dev,
		       clk_info);
		return -EINVAL;
	}

	clk_info->clk = devm_clk_get(dev, clk_name);
	if (IS_ERR_OR_NULL(clk_info->clk)) {
		rc = PTR_ERR(clk_info->clk);
		dev_err(dev, "could not get clock %s, rc=%d\n", clk_name, rc);
		clk_info->clk = NULL;
		clk_info->clk_rate = 0;
		return rc;
	} else { /* get assigned clock rate */
		int index =
			of_property_match_string(np, "clock-names", clk_name);
		if (index < 0) {
			dev_err(dev, "could not match clock %s,  index=%d\n",
				clk_name, index);
			return index;
		}
		rc = of_property_read_u32_index(np, "clock-rates", index,
						&clk_info->clk_rate);
		if (rc) {
			dev_err(dev, "could not get clock %s rate, rc=%d\n",
				clk_name, rc);
			clk_info->clk_rate = 0;
			rc = 0;
		}
	}
	clk_info->clk_name = clk_name;

	dev_dbg(dev, "clock %s assigned rate %d\n", clk_info->clk_name,
		clk_info->clk_rate);

	return rc;
}
EXPORT_SYMBOL_GPL(vcam_get_dt_clk_info);

int vcam_update_clock_rate(struct vcam_clk_info *clk_info, unsigned int rate)
{
	long clk_val;
	int rc;

	if (!clk_info || !rate)
		return -EINVAL;

	if (clk_info->clk && rate) {
		clk_val = clk_round_rate(clk_info->clk, rate);
		if (clk_val < 0) {
			pr_err("clock %s round rate failed, clk_val=%ld\n",
			       clk_info->clk_name, clk_val);
			return -EINVAL;
		}

		rc = clk_set_rate(clk_info->clk, clk_val);
		if (rc < 0) {
			pr_err("clock %s set rate failed, rc=%d\n",
			       clk_info->clk_name, rc);
			return rc;
		}

		pr_info("clock %s set rate to %ld\n", clk_info->clk_name,
			clk_val);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(vcam_update_clock_rate);
