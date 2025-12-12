/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_DRM_H_
#define _SPACEMIT_DRM_H_

#include <drm/drm_print.h>
#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>
#include <drm/drm_vblank.h>
#include <drm/drm_probe_helper.h>
#include <linux/dma-mapping.h>
#include "spacemit_dmmu.h"

struct spacemit_drm_private {
	struct drm_device *ddev;
	struct device *dev;
	struct spacemit_hw_device *hwdev;
	int hw_ver;
	bool contig_mem;
	struct cmdlist **cmdlist_groups;
	struct drm_writeback_connector **wb_connector;
#ifdef CONFIG_SPACEMIT_DEBUG
	bool underrun_debug;
	struct drm_atomic_state *old_state;
	spinlock_t ur_dump_lock;
#endif
};

extern struct platform_driver spacemit_dpu_driver;
extern struct platform_driver spacemit_dphy_driver;
extern struct platform_driver spacemit_wb_driver;
extern struct platform_driver spacemit_dsi_driver;

#endif /* _SPACEMIT_DRM_H_ */
