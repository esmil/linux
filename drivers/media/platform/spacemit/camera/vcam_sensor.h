/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SPACEMIT Camera Verification System - SENSOR Module
 *
 * Copyright (C) 2021 SPACEMIT Micro Limited
 * All Rights Reserved.
 */

#ifndef __VCAM_SENSOR_H__
#define __VCAM_SENSOR_H__

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>

//#define CONFIG_ARCH_SPACEMIT

struct vcam_sensor_device {
	struct platform_device *pdev;
	struct cdev cdev;
	u32 id;
	u32 is_probe_succeed;

	struct gpio_desc *gpio_pwdn;
	struct gpio_desc *gpio_rst;
//#ifdef CONFIG_ARCH_SPACEMIT
	struct gpio_desc *gpio_afvdd;
	struct gpio_desc *gpio_avdd;
	struct gpio_desc *gpio_dvdd;
//#else
	struct gpio_desc *gpio_dptc;
//#endif

	struct regulator *supply_afvdd;
	struct regulator *supply_avdd;
	struct regulator *supply_dovdd;
	struct regulator *supply_dvdd;

	struct clk *mclk;
	u32 dphy[5]; /* DPHY:  CSI2_DPHY1, CSI2_DPHY2, CSI2_DPHY3, CSI2_DPHY5, CSI2_DPHY6 */

	atomic_t usr_cnt;
	struct mutex lock; /* Protects streaming, format, interval*/
};

#endif
