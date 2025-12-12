/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SATURN_FBCMEM_H_
#define _SATURN_FBCMEM_H_

#include <drm/drm_plane.h>
#include "../spacemit_crtc.h"
#include "../spacemit_dpu_reg.h"
#include "dpu_saturn.h"

#define FBCMEM_UNIT	(32)	//fbcmem is 32 bytes per unit

typedef enum FBC_BLOCK_SIZE_E {
	FBC_BLOCK_SIZE_16x16, //0
	FBC_BLOCK_SIZE_32x8,  //1
	FBC_BLOCK_SIZE_LIMIT, //error block size
} FBC_BLOCK_SIZE_T;

int get_raw_data_plane_rdma_mem_size(u32 drm_4cc_fmt, bool rot_90_or_270,
				u32 plane_crop_width, u32 *output_mem_size);

int get_afbc_data_plane_min_rdma_mem_size(u8 rdma_work_mode, u32 drm_4cc_fmt,
	u32 crop_start_x, u32 crop_start_y, u32 crop_width, u32 crop_height,
	u32 fbc_block_size, bool rot_90_or_270, u8 min_lines, u32 *output_mem_size);

int saturn_cal_layer_fbcmem_size(struct drm_plane *plane,
				 struct drm_plane_state *state);

int saturn_adjust_rdma_fbcmem(struct spacemit_hw_device *hwdev,
			      struct spacemit_crtc_rdma *rdmas);

#endif
