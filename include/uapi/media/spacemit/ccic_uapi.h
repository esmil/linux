/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ccic_uapi.h - uapi for ccic
 *
 * Copyright (C) 2025 Spacemit Ltd.
 */

#ifndef _CCIC_UAPI_H_
#define _CCIC_UAPI_H_

#include <linux/videodev2.h>

enum csi_work_mode {
	CSI_WORK_MODE_NORMAL = 0,
	CSI_WORK_MODE_VC,
	CSI_WORK_MODE_MAX,
};

struct csi_phy_param {
	unsigned int lane_num;
	unsigned int mipi_mbps;
};

struct csi_mode_param {
	unsigned int mode;
	unsigned int dt_filter_en;
};

struct csi_out_path_param {
	struct csi_phy_param phy_param;
	struct csi_mode_param mode_param;
	unsigned int vc;
	unsigned int dt_filter0_en;
	unsigned int filter0_pattern;
	unsigned int dt_filter1_en;
	unsigned int filter1_pattern;
	unsigned int dma_channel;
};

struct csi_path_info {
	unsigned int id;
	unsigned int csi_id;
	unsigned int path_id;
};

#define BASE_VIDIOC_CSI             (BASE_VIDIOC_PRIVATE + 20)
#define CSI_VIDIOC_G_PATH_INFO	_IOR('V', BASE_VIDIOC_CSI + 1, struct csi_path_info)
#define CSI_VIDIOC_FLUSH_BUF	_IO('V', BASE_VIDIOC_CSI + 2)

#endif
