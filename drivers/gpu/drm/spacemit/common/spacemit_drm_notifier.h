/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DRM_NOTIFIER_H_
#define _SPACEMIT_DRM_NOTIFIER_H_

#include <linux/notifier.h>

enum {
	DRM_PANEL_EARLY_EVENT_BLANK = 0,
	DRM_PANEL_EVENT_BLANK,
	DRM_PANEL_BLANK_UNBLANK,
	DRM_PANEL_BLANK_POWERDOWN,
	DRM_PANEL_TOUCH_INT,
};

struct spacemit_drm_notifier {
	int blank;
};

int spacemit_drm_notifier_register_client(struct notifier_block *nb);

int spacemit_drm_notifier_unregister_client(struct notifier_block *nb);

int spacemit_drm_notifier_call_chain(unsigned long val, void *v);

extern struct blocking_notifier_head drm_notifier_list;

#endif
