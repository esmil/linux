/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SYSFS_DISPLAY_H_
#define _SYSFS_DISPLAY_H_

#include <linux/device.h>

extern struct class *display_class;

int spacemit_dpu_sysfs_init(struct device *dev);
int spacemit_dsi_sysfs_init(struct device *dev);
int spacemit_dphy_sysfs_init(struct device *dev);
int spacemit_mipi_panel_sysfs_init(struct device *dev);

#endif
