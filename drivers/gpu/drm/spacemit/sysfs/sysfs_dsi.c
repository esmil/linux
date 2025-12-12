// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "../spacemit_mipi_panel.h"
#include "../spacemit_dsi.h"
#include "../spacemit_crtc.h"
#include "sysfs_display.h"


int spacemit_dsi_sysfs_init(struct device *dev)
{

	return 0;
}
EXPORT_SYMBOL(spacemit_dsi_sysfs_init);

MODULE_DESCRIPTION("Add dsi attribute nodes for userspace");
MODULE_LICENSE("GPL v2");

