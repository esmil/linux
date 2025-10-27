/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 SPACEMIT Micro Limited
 * All Rights Reserved.
 */

#ifndef VCAM_CLK_H_YAHGAWLS
#define VCAM_CLK_H_YAHGAWLS

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/reset.h>

/* maximum number of device clock */
#define CAM_SOC_MAX_CLK (32)

struct vcam_clk_info {
	const char *clk_name;
	unsigned int clk_rate;
	struct clk *clk;
};

int vcam_get_dt_reset_info(struct device *dev, const char *rst_name,
			   struct reset_control **rst);
int vcam_get_dt_clk_info(struct device *dev, const char *clk_name,
			 struct vcam_clk_info *clk_info);
int vcam_update_clock_rate(struct vcam_clk_info *clk_info, unsigned int rate);

#endif /* end of include guard: VCAM_CLK_H_YAHGAWLS */
