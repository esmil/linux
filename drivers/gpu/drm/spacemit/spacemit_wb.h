/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SPACEMIT_MB_H_
#define _SPACEMIT_MB_H_

#include <linux/of.h>
#include <linux/device.h>
#include <video/videomode.h>

#include <drm/drm_print.h>
#include <drm/drm_writeback.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_bridge.h>

#include "spacemit_cmdlist.h"
#include "spacemit_lib.h"
#include "spacemit_crtc.h"

#define SPACEMIT_RGB2YUV_COEFFS	12
#define SPACEMIT_WB_INVALID_FORMAT_ID	0xff

struct dpu_mmu_tbl;
struct spacemit_crtc;




enum spacemit_wb_loc {
	SPACEMIT_WB_RDMA0 = 0,
	SPACEMIT_WB_PREPQ0,
	SPACEMIT_WB_RDMA1,
	SPACEMIT_WB_PREPQ1,
	SPACEMIT_WB_RDMA2,
	SPACEMIT_WB_PREPQ2,
	SPACEMIT_WB_RDMA3,
	SPACEMIT_WB_PREPQ3,
	SPACEMIT_WB_RDMA4,
	SPACEMIT_WB_PREPQ4,
	SPACEMIT_WB_RDMA5,
	SPACEMIT_WB_PREPQ5,
	SPACEMIT_WB_RDMA6,
	SPACEMIT_WB_PREPQ6,
	SPACEMIT_WB_RDMA7,
	SPACEMIT_WB_PREPQ7,
	SPACEMIT_WB_RDMA8,
	SPACEMIT_WB_PREPQ8,
	SPACEMIT_WB_RDMA9,
	SPACEMIT_WB_PREPQ9,
	SPACEMIT_WB_RDMA10,
	SPACEMIT_WB_PREPQ10,
	SPACEMIT_WB_RDMA11,
	SPACEMIT_WB_PREPQ11,
	SPACEMIT_WB_COMP0,
	SPACEMIT_WB_COMP1,
	SPACEMIT_WB_COMP2,
	SPACEMIT_WB_POST0,
	SPACEMIT_WB_POST1,
	SPACEMIT_WB_POST2,
};

enum spacemit_wb_format {
	SPACEMIT_WB_FORMAT_ARGB2101010 = 0,
	SPACEMIT_WB_FORMAT_ABGR2101010,
	SPACEMIT_WB_FORMAT_RGBA1010102,
	SPACEMIT_WB_FORMAT_BGRA1010102,
	SPACEMIT_WB_FORMAT_ARGB8888,
	SPACEMIT_WB_FORMAT_ABGR8888,
	SPACEMIT_WB_FORMAT_RGBA8888,
	SPACEMIT_WB_FORMAT_BGRA8888,
	SPACEMIT_WB_FORMAT_XRGB8888,
	SPACEMIT_WB_FORMAT_XBGR8888,
	SPACEMIT_WB_FORMAT_RGBX8888,
	SPACEMIT_WB_FORMAT_BGRX8888,
	SPACEMIT_WB_FORMAT_RGB888,
	SPACEMIT_WB_FORMAT_BGR888,
	SPACEMIT_WB_FORMAT_RGBA5551,
	SPACEMIT_WB_FORMAT_BGRA5551,
	SPACEMIT_WB_FORMAT_ABGR1555,
	SPACEMIT_WB_FORMAT_ARGB1555,
	SPACEMIT_WB_FORMAT_RGBX5551,
	SPACEMIT_WB_FORMAT_BGRX5551,
	SPACEMIT_WB_FORMAT_XBGR1555,
	SPACEMIT_WB_FORMAT_XRGB1555,
	SPACEMIT_WB_FORMAT_RGB565,
	SPACEMIT_WB_FORMAT_BGR565,
	SPACEMIT_WB_FORMAT_YUV420_P2_8_UV,
	SPACEMIT_WB_FORMAT_YUV420_P2_8_VU,
	SPACEMIT_WB_FORMAT_YUV420_P2_10_UV,
	SPACEMIT_WB_FORMAT_YUV420_P2_10_VU,
};

struct spacemit_format_fb2wb {
	u32 format;             /* DRM fourcc */
	int wb_id;		/* WB_FORMAT*/
};

struct spacemit_wb_device {
	uint32_t id;
	struct videomode vm;
	int status;
};

struct spacemit_wb {
	struct platform_device *pdev;
	struct device dev;
	struct drm_device *ddev;
	struct drm_encoder encoder;
	struct drm_writeback_connector wb_connector;
	struct spacemit_wb_device ctx;
	struct spacemit_crtc *a_crtc;
	struct cmdlist_regs *cl_wb;
	struct drm_property *rotation_property;
	struct drm_property *in_x_property;
	struct drm_property *in_y_property;
	struct drm_property *in_w_property;
	struct drm_property *in_h_property;
	struct drm_property *out_x_property;
	struct drm_property *out_y_property;
	struct drm_property *out_w_property;
	struct drm_property *out_h_property;
	struct drm_property *scale_smooth_prop;
	unsigned int rotation;
	unsigned int in_x;
	unsigned int in_y;
	unsigned int in_w;
	unsigned int in_h;
	unsigned int out_x;
	unsigned int out_y;
	unsigned int out_w;
	unsigned int out_h;
	unsigned int format;
	bool use_scl;
	struct spacemit_afbc_state *afbc_state;
};

struct spacemit_connector_state {
	struct drm_connector_state base;
};

int spacemit_wb_get_afbc_header_size(struct spacemit_wb *wb, struct drm_framebuffer *fb, int width, int height);

#define to_spacemit_conn_state(x) container_of(x, struct spacemit_connector_state, base)

void saturn_wb_config(struct spacemit_crtc *a_crtc, struct spacemit_wb *wb, struct drm_framebuffer *fb, struct cmdlist_regs *cl_wb);
#endif
