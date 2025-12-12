/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_BACKLIGHT_H_
#define _SPACEMIT_BACKLIGHT_H_

struct spacemit_bl_ops {
	int (*get_bl)(void *devdata);
	int (*set_bl)(void *devdata, int brightness);
};

int spacemit_bl_device_register(struct device *parent, void *devdata, struct spacemit_bl_ops *ops, int max);

#endif
