// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_blend.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_color_mgmt.h>
#include <linux/component.h>
#include <linux/dma-mapping.h>
#include "spacemit_cmdlist.h"
#include "spacemit_dmmu.h"
#include "spacemit_drm.h"
#include "spacemit_gem.h"
#include "spacemit_lib.h"
#include "dpu/dpu_saturn.h"
#include "dpu/dpu_trace.h"

static const uint64_t supported_format_modifiers[] = {
	// Added for raw format
	DRM_FORMAT_MOD_LINEAR,

	// Added for AFBC(rgb*)
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPLIT | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SC),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_CBR | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_SC),

	/* All RGB formats (except XRGB, RGBX, XBGR, BGRX) */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR),

	/* All RGB formats > 16bpp (except XRGB, RGBX, XBGR, BGRX) */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT),

	/* All 8 or 10 bit YUV 444 formats. */
	/* In DP550, 10 bit YUV 420 format also supported */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT),

	/* YUV 420, 422 P1 8 bit and YUV 444 8 bit/10 bit formats */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED | AFBC_FORMAT_MOD_CBR),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_TILED),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16),

	/* YUV 420, 422 P1 8, 10 bit formats */
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_CBR | AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16 | AFBC_FORMAT_MOD_CBR),

	// For BG16/RG16 (3rd app will use this config, for example: games, nenamark etc ...)
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 | AFBC_FORMAT_MOD_YTR | AFBC_FORMAT_MOD_SPARSE),

	DRM_FORMAT_MOD_INVALID
};

struct spacemit_plane *to_spacemit_plane(struct drm_plane *plane)
{
	return container_of(plane, struct spacemit_plane, plane);
}

static int spacemit_plane_check_rdma(const struct spacemit_hw_rdma *rdma, u32 rdma_id, struct drm_plane_state *state)
{
	unsigned int rot = state->rotation;
	unsigned int zpos = state->zpos;
	u32 format = state->fb->format->format;
	u16 hw_formats = rdma[rdma_id].formats;
	u16 hw_rots = rdma[rdma_id].rots;
	bool afbc = (state->fb->modifier > 0);
	const struct drm_format_info *info = drm_format_info(format);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(state->crtc);

	trace_spacemit_plane_check_rdma(a_crtc->dev_id);
	if ((info->is_yuv) && !afbc && ((hw_formats & FORMAT_RAW_YUV) == 0)) {
		DRM_DEBUG("rdma%d doesn't support RAW YUV format with zpos%d!\n", rdma_id, zpos);
		return -EINVAL;
	}

	if (afbc && ((hw_formats & FORMAT_AFBC) == 0)) {
		DRM_DEBUG("rdma%d doesn't support AFBC format with zpos%d!\n", rdma_id, zpos);
		return -EINVAL;
	}

	if (is_rot_90_270(rot)) {
		if (afbc) {
			if ((hw_rots & ROTATE_AFBC_90_270) == 0) {
				DRM_DEBUG("rdma%d doesn't support AFBC 90/270 rotation with zpos%d\n", rdma_id, zpos);
				return -EINVAL;
			}
		} else {
			if ((hw_rots & ROTATE_RAW_90_270) == 0) {
				DRM_DEBUG("rdma%d doesn't support RAW 90/270 rotation with zpos%d!\n", rdma_id, zpos);
				return -EINVAL;
			}
		}
	} else {
		if ((hw_rots & ROTATE_COMMON) == 0) {
			DRM_DEBUG("rdma%d doesn't support common rotation with zpos%d!\n", rdma_id, zpos);
			return -EINVAL;
		}
	}

	return 0;
}

static int spacemit_plane_atomic_check_hdr_coefs(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct spacemit_plane_state *apstate = to_spacemit_plane_state(state);
	struct drm_property_blob *blob;
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	int size = hwdev->hdr_coef_size;
	int *coef_data;
	int n = 0;

	if (apstate->hdr_coefs_blob_prop) {
		blob = apstate->hdr_coefs_blob_prop;
		coef_data = (int *)blob->data;

		for (n = 0; n < size; n++) {
			if ((coef_data[n] > 65535) || (coef_data[n] < 0)) {
				DRM_ERROR("HDR coef is invalid %d\n", coef_data[n]);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int spacemit_plane_atomic_check_scale_coefs(struct drm_plane *plane,
					  struct drm_plane_state *state)
{
	struct spacemit_plane_state *apstate = to_spacemit_plane_state(state);
	struct drm_property_blob *blob;
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	int size = hwdev->hor_scale_coef_size + hwdev->ver_scale_coef_size;
	int *coef_data;
	int n = 0;

	if (apstate->scale_coefs_blob_prop) {
		blob = apstate->scale_coefs_blob_prop;
		coef_data = (int *)blob->data;

		for (n = 0; n < size; n++) {
			if ((coef_data[n] > 32766) || (coef_data[n] < -32767)) {
				DRM_ERROR("scale coef is invalid %d\n", coef_data[n]);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int spacemit_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *atomic_state)
{
	struct drm_plane_state *state = drm_atomic_get_new_plane_state(atomic_state, plane);
	struct drm_framebuffer *fb = state->fb;
	u32 src_w, src_h, src_x, src_y, scl_in_width, scl_in_height;
	u32 crtc_w, crtc_h;
	struct spacemit_plane_state *cur_state = to_spacemit_plane_state(state);
	u32 cur_rdma_id = cur_state->rdma_id;
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	const struct spacemit_hw_rdma *rdmas = hwdev->rdmas;
	struct spacemit_crtc *a_crtc = NULL;

	if (!state->crtc || WARN_ON(!state->fb))
		return 0;

	a_crtc = to_spacemit_crtc(state->crtc);
	trace_spacemit_plane_atomic_check(a_crtc->dev_id);

	src_x = state->src_x >> 16;
	src_y = state->src_y >> 16;
	src_w = state->src_w >> 16;
	src_h = state->src_h >> 16;
	crtc_w = state->crtc_w;
	crtc_h = state->crtc_h;

	/* For multi planes, only support planes with its offsets is set */
	if (fb->format->num_planes > 3 ||
	   (fb->format->num_planes > 2 && fb->offsets[2] == 0) ||
	   (fb->format->num_planes > 1 && fb->offsets[1] == 0)) {
		DRM_ERROR("%s, Unsupported plane format: plane_num:%d offsets[1]:%d offsets[2]:%d\n",
			  __func__, fb->format->num_planes, fb->offsets[1], fb->offsets[2]);
		return -EINVAL;
	}

	if (fb->format->num_planes > 1 && fb->modifier) {
		DRM_ERROR("%s, Unsupported afbc with plane_num:%d\n", __func__, fb->format->num_planes);
		return -EINVAL;
	}

	if (fb->format->format == DRM_FORMAT_NV12 || fb->format->format == DRM_FORMAT_YUV420_8BIT) {
		if (src_x % 2 || src_y % 2 || src_w % 2 || src_h % 2) {
			DRM_ERROR("YUV420 coordinations must be even! src_x:%d src_y:%d src_w:%d src_h:%d\n",
				  src_x, src_y, src_w, src_h);
			return -EINVAL;
		}
	}

	/* adjust rdma id */
	if (src_w == 0 && src_h == 0)
		cur_rdma_id = RDMA_INVALID_ID;
	else if (src_w == 0 || src_h == 0) {
		/* check whether plane is invalid when not solid */
		DRM_ERROR("%s, Unsupported src_w %d src_h %d\n", __func__, src_w, src_h);
		return -EINVAL;
	} else {
		/* In case the userspace hasn't set rdma id */
		if (cur_rdma_id == RDMA_INVALID_ID)
			cur_rdma_id = state->zpos;
	}
	cur_state->rdma_id = cur_rdma_id;

	/* Skip solid color */
	if (cur_rdma_id != RDMA_INVALID_ID) {
		if (a_crtc->is_offline_mode)
			cur_state->is_offline = 1;

		if (is_rot_90_270(state->rotation)) {
			scl_in_width  = src_h;
			scl_in_height = src_w;
		} else {
			scl_in_width  = src_w;
			scl_in_height = src_h;
		}

		if (scl_in_width != crtc_w || scl_in_height != crtc_h)
			cur_state->use_scl = true;
		else
			cur_state->use_scl = false;
		if (cur_rdma_id < hwdev->rdma_nums) {
			if (spacemit_plane_check_rdma(rdmas, cur_rdma_id, state))
				return -EINVAL;
		} else {
			DRM_ERROR("Invalid rdma id:%d\n", cur_rdma_id);
			return -EINVAL;
		}

		if (a_crtc->core->cal_layer_fbcmem_size(plane, state)) {
			DRM_ERROR("plane:%d Invalid fbcmem size\n", state->zpos);
			return -EINVAL;
		}

		if (a_crtc->core->calc_plane_mclk_bw(plane, state)) {
			DRM_INFO("plane:%d unsupported mclk or bandwidth\n", state->zpos);
			return -EINVAL;
		}
	}

	if (spacemit_plane_atomic_check_hdr_coefs(plane, state)) {
		DRM_ERROR("The value of hdr coef is invalid\n");
		return -EINVAL;
	}

	if (spacemit_plane_atomic_check_scale_coefs(plane, state)) {
		DRM_ERROR("The value of scale coef is invalid\n");
		return -EINVAL;
	}

	cur_state->format = spacemit_plane_hw_get_format_id(fb->format->format);
	if (cur_state->format == SPACEMIT_DPU_INVALID_FORMAT_ID) {
		DRM_ERROR("Can't support format:%p4cc\n", &fb->format->format);
		return -EINVAL;
	}

	/* Use default values as they are not actually used now  */
	cur_state->right_image = 0;
	return 0;
}

static void spacemit_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	int ret = 0;
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct spacemit_crtc *a_crtc = to_spacemit_crtc(plane->state->crtc);
	struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(plane->state);
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	u32 rdma_id = spacemit_pstate->rdma_id;
	int i = 0;
	struct drm_crtc_state *crtc_state;
	struct spacemit_crtc_state *spacemit_crtc_state;
	struct cmdlist *cl = NULL;

	DRM_DEBUG("%s()\n", __func__);
	trace_spacemit_plane_atomic_update(a_crtc->dev_id);

	spacemit_pstate->cur_cl = 0;

	crtc_state = a_crtc->crtc.state;
	spacemit_crtc_state = to_spacemit_crtc_state(crtc_state);
	spacemit_crtc_state->cur_cl = 0;

	hwdev->get_cl_rdma_buf(a_crtc);
	//cl_rdma free is at the end of the function and early return

	hwdev->update_hdr_matrix(plane, spacemit_pstate);
	hwdev->update_csc_matrix(plane, old_state);

	if (hwdev->update_prepipe_gamma)
		hwdev->update_prepipe_gamma(plane, spacemit_pstate);

	if (a_crtc->is_slice_mode) {
		for (i = 0; i < a_crtc->slice_num; i++) {
			hwdev->plane_update_hw_channel(plane, i);

			/* slice 0 config dmmu*/
			if (i == 0) {
				u8 tbu_id;

				spacemit_pstate->mmu_tbl.size = ((PAGE_ALIGN(plane->state->fb->obj[0]->size) >> PAGE_SHIFT) +
						HW_ALIGN_TTB_NUM) * 4;
				spacemit_pstate->mmu_tbl.va = dma_alloc_coherent(a_crtc->dev, spacemit_pstate->mmu_tbl.size,
						&spacemit_pstate->mmu_tbl.pa, GFP_KERNEL | __GFP_ZERO);
				if (spacemit_pstate->mmu_tbl.va == NULL) {
					DRM_ERROR("Failed to allocate %d bytes for dpu plane%d mmu table\n",
							spacemit_pstate->mmu_tbl.size, plane->state->zpos);
					free_cmdlist_regs(a_crtc->cl_rdma);
					return;
				}
				tbu_id = !spacemit_pstate->right_image ? (rdma_id * 2) : (rdma_id * 2 + 1);
				ret = spacemit_dmmu_map(plane->state->fb, &spacemit_pstate->mmu_tbl, tbu_id, NULL, a_crtc->cl_rdma, plane);
				if (!ret)
					cmdlist_regs_packing(plane_to_cl(plane), CMDLIST_MOD_RDMA, a_crtc->cl_rdma);
				else
					DRM_ERROR("%s failed to map plane with ret = %d\n", __func__, ret);
			} else {
				cmdlist_regs_packing(plane_to_cl(plane), CMDLIST_MOD_RDMA, a_crtc->cl_rdma);
			}
			cl = &spacemit_pstate->cl[spacemit_pstate->cur_cl];
			cl->rch_start_cmps_y = a_crtc->slice_wb[0].crtc_h * i;
			cl->cmdlist_ch_y_other = a_crtc->slice_wb[0].crtc_h * (i + 1);

			spacemit_pstate->cur_cl++;
			spacemit_crtc_state->cur_cl++;
		}
	} else {
		hwdev->plane_update_hw_channel(plane, DPU_NO_SLICE);

		/* No need for solid color layer */
		if (rdma_id < hwdev->rdma_nums && spacemit_pstate->cur_cl == 0) {
			u8 tbu_id;

			spacemit_pstate->mmu_tbl.size = ((PAGE_ALIGN(plane->state->fb->obj[0]->size) >> PAGE_SHIFT) +
					HW_ALIGN_TTB_NUM) * 4;
			spacemit_pstate->mmu_tbl.va = dma_alloc_coherent(a_crtc->dev, spacemit_pstate->mmu_tbl.size,
					&spacemit_pstate->mmu_tbl.pa, GFP_KERNEL | __GFP_ZERO);
			if (spacemit_pstate->mmu_tbl.va == NULL) {
				DRM_ERROR("Failed to allocate %d bytes for dpu plane%d mmu table\n",
						spacemit_pstate->mmu_tbl.size, plane->state->zpos);
				free_cmdlist_regs(a_crtc->cl_rdma);
				return;
			}
			tbu_id = !spacemit_pstate->right_image ? (rdma_id * 2) : (rdma_id * 2 + 1);
			ret = spacemit_dmmu_map(plane->state->fb, &spacemit_pstate->mmu_tbl, tbu_id, NULL, a_crtc->cl_rdma, plane);
			if (!ret)
				cmdlist_regs_packing(plane_to_cl(plane), CMDLIST_MOD_RDMA, a_crtc->cl_rdma);
			else
				DRM_ERROR("%s failed to map plane with ret = %d\n", __func__, ret);
		}

		cl = &spacemit_pstate->cl[spacemit_pstate->cur_cl];
		cl->rch_start_cmps_y = 0xDEADBEEF;
		cl->cmdlist_ch_y_other = 0xDEADBEEF;
	}
	free_cmdlist_regs(a_crtc->cl_rdma);
}

static void spacemit_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *old_state = drm_atomic_get_old_plane_state(state, plane);
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	DRM_DEBUG("%s()\n", __func__);

	spacemit_dmmu_unmap(plane);
	hwdev->plane_disable_hw_channel(plane, old_state);
}

static void spacemit_plane_reset(struct drm_plane *plane)
{
	struct spacemit_plane *p = to_spacemit_plane(plane);
	struct spacemit_plane_state *s;
	struct spacemit_drm_private *priv = plane->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;

	DRM_DEBUG("%s()\n", __func__);

	if (plane->state) {
		s = to_spacemit_plane_state(plane->state);
		__drm_atomic_helper_plane_destroy_state(plane->state);
		kfree(s);
		plane->state = NULL;
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (s) {
		__drm_atomic_helper_plane_reset(plane, &s->state);
		s->state.zpos = hwdev->plane_nums - p->hw_pid - 1;
		s->rdma_id = RDMA_INVALID_ID;
		s->is_offline = 0;
		s->scaler_id = SCALER_INVALID_ID;
	}
}

static struct drm_plane_state *
spacemit_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	struct spacemit_plane_state *s;
	struct spacemit_plane_state *old_state = to_spacemit_plane_state(plane->state);
	struct spacemit_crtc *a_crtc = NULL;
	u8 i = 0;

	if (plane->crtc) {
		a_crtc = to_spacemit_crtc(plane->crtc);
		trace_spacemit_plane_atomic_duplicate_state(a_crtc->dev_id);
	}
	DRM_DEBUG("%s()\n", __func__);

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &s->state);

	WARN_ON(s->state.plane != plane);

	for (i = 0; i < MAX_CL_NUM; i++) {
		s->cl[i].index = i;
		s->cl[i].type = CMDLIST_PLANE;
	}

	s->is_offline = old_state->is_offline;
	s->rdma_id = old_state->rdma_id;
	s->format = old_state->format;
	s->right_image = old_state->right_image;
	s->scaler_id = SCALER_INVALID_ID;
	s->use_scl = false;
	s->fbcmem_size = 0;

	if (s->hdr_coefs_blob_prop)
		drm_property_blob_get(s->hdr_coefs_blob_prop);
	if (s->scale_coefs_blob_prop)
		drm_property_blob_get(s->scale_coefs_blob_prop);

	return &s->state;
}

static void spacemit_plane_atomic_destroy_state(struct drm_plane *plane,
					    struct drm_plane_state *state)
{
	int i = 0;
	struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(state);
	struct spacemit_crtc *a_crtc = NULL;

	DRM_DEBUG("%s()\n", __func__);

	if (state->crtc) {
		a_crtc = to_spacemit_crtc(state->crtc);

		if (spacemit_pstate->mmu_tbl.va)
			dma_free_coherent(a_crtc->dev, spacemit_pstate->mmu_tbl.size,
					  spacemit_pstate->mmu_tbl.va, spacemit_pstate->mmu_tbl.pa);

		for (i = 0; i < MAX_CL_NUM; i++) {
			if (spacemit_pstate->cl[i].va)
				dma_free_coherent(a_crtc->dev, spacemit_pstate->cl[i].size,
						  spacemit_pstate->cl[i].va, spacemit_pstate->cl[i].pa);
		}
		trace_spacemit_plane_atomic_destroy_state(a_crtc->dev_id);
	}
	__drm_atomic_helper_plane_destroy_state(state);

	if (spacemit_pstate->hdr_coefs_blob_prop)
		drm_property_blob_put(spacemit_pstate->hdr_coefs_blob_prop);

	if (spacemit_pstate->scale_coefs_blob_prop)
		drm_property_blob_put(spacemit_pstate->scale_coefs_blob_prop);

	kfree(to_spacemit_plane_state(state));
}

static int spacemit_plane_atomic_set_property(struct drm_plane *plane,
					  struct drm_plane_state *state,
					  struct drm_property *property,
					  u64 val)
{
	struct spacemit_plane *p = to_spacemit_plane(plane);
	struct spacemit_plane_state *s = to_spacemit_plane_state(state);
	bool replaced = false;
	int ret = 0;

	DRM_DEBUG("%s() name = %s, val = %llu\n",
		  __func__, property->name, val);

	if (property == p->rdma_id_property)
		s->rdma_id = val;
	else if (property == p->solid_color_property)
		s->solid_color = val;
	else if (property == p->dec_lines_property)
		s->dec_lines = val;
	else if (property == p->hdr_coef_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(plane->dev,
					&s->hdr_coefs_blob_prop,
					val,
					-1,
					sizeof(int),
					&replaced);
		return ret;
	} else if (property == p->scale_coef_property) {
		ret = spacemit_atomic_replace_property_blob_from_id(plane->dev,
					&s->scale_coefs_blob_prop,
					val,
					-1,
					sizeof(int),
					&replaced);
		return ret;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static int spacemit_plane_atomic_get_property(struct drm_plane *plane,
					  const struct drm_plane_state *state,
					  struct drm_property *property,
					  u64 *val)
{
	struct spacemit_plane *p = to_spacemit_plane(plane);
	const struct spacemit_plane_state *s = to_spacemit_plane_state(state);

	DRM_DEBUG("%s() name = %s\n", __func__, property->name);

	if (property == p->rdma_id_property)
		*val = s->rdma_id;
	else if (property == p->solid_color_property)
		*val = s->solid_color;
	else if (property == p->dec_lines_property)
		*val = s->dec_lines;
	else if (property == p->hdr_coef_property) {
		if (s->hdr_coefs_blob_prop)
			*val = (s->hdr_coefs_blob_prop) ? s->hdr_coefs_blob_prop->base.id : 0;
	} else if (property == p->scale_coef_property) {
		if (s->scale_coefs_blob_prop)
			*val = (s->scale_coefs_blob_prop) ? s->scale_coefs_blob_prop->base.id : 0;
	} else {
		DRM_ERROR("property %s is invalid\n", property->name);
		return -EINVAL;
	}

	return 0;
}

static const char * const color_encoding_name[] = {
	[DRM_COLOR_YCBCR_BT601] = "ITU-R BT.601 YCbCr",
	[DRM_COLOR_YCBCR_BT709] = "ITU-R BT.709 YCbCr",
	[DRM_COLOR_YCBCR_BT2020] = "ITU-R BT.2020 YCbCr",
};

static const char * const color_range_name[] = {
	[DRM_COLOR_YCBCR_FULL_RANGE] = "YCbCr full range",
	[DRM_COLOR_YCBCR_LIMITED_RANGE] = "YCbCr limited range",
};

int spacemit_drm_plane_create_color_properties(struct drm_plane *plane,
				      u32 supported_encodings,
				      u32 supported_ranges,
				      enum drm_color_encoding default_encoding,
				      enum drm_color_range default_range)
{
	struct drm_device *dev = plane->dev;
	struct drm_property *prop;
	struct drm_prop_enum_list enum_list[MAX_T(int, DRM_COLOR_ENCODING_MAX,
						       DRM_COLOR_RANGE_MAX)];
	int i, len;

	if (WARN_ON(supported_encodings == 0 ||
		    (supported_encodings & -BIT(DRM_COLOR_ENCODING_MAX)) != 0 ||
		    (supported_encodings & BIT(default_encoding)) == 0))
		return -EINVAL;

	if (WARN_ON(supported_ranges == 0 ||
		    (supported_ranges & -BIT(DRM_COLOR_RANGE_MAX)) != 0 ||
		    (supported_ranges & BIT(default_range)) == 0))
		return -EINVAL;

	len = 0;
	for (i = 0; i < DRM_COLOR_ENCODING_MAX; i++) {
		if ((supported_encodings & BIT(i)) == 0)
			continue;

		enum_list[len].type = i;
		enum_list[len].name = color_encoding_name[i];
		len++;
	}

	prop = drm_property_create_enum(dev, 0, "COLOR_ENCODING",
					enum_list, len);
	if (!prop)
		return -ENOMEM;
	plane->color_encoding_property = prop;
	drm_object_attach_property(&plane->base, prop, default_encoding);
	if (plane->state)
		plane->state->color_encoding = default_encoding;

	len = 0;
	for (i = 0; i < DRM_COLOR_RANGE_MAX; i++) {
		if ((supported_ranges & BIT(i)) == 0)
			continue;

		enum_list[len].type = i;
		enum_list[len].name = color_range_name[i];
		len++;
	}

	prop = drm_property_create_enum(dev, 0,	"COLOR_RANGE",
					enum_list, len);
	if (!prop)
		return -ENOMEM;
	plane->color_range_property = prop;
	drm_object_attach_property(&plane->base, prop, default_range);
	if (plane->state)
		plane->state->color_range = default_range;

	return 0;
}

static int spacemit_plane_create_properties(struct spacemit_plane *p, int index)
{
	struct drm_property *prop;
	unsigned int support_modes = BIT(DRM_MODE_BLEND_PIXEL_NONE) |
		BIT(DRM_MODE_BLEND_PREMULTI) |
		BIT(DRM_MODE_BLEND_COVERAGE);
	int ret = 0;

	DRM_DEBUG("%s()\n", __func__);
	/* create rotation property */
	drm_plane_create_rotation_property(&p->plane,
					   DRM_MODE_ROTATE_0,
					   DRM_MODE_ROTATE_MASK |
					   DRM_MODE_REFLECT_MASK);

	/* create zpos property */
	drm_plane_create_zpos_immutable_property(&p->plane, index);

	/* create layer alpha property */
	drm_plane_create_alpha_property(&p->plane);

	/* create blend mode property */
	drm_plane_create_blend_mode_property(&p->plane, support_modes);

	prop = drm_property_create_range(p->plane.dev, 0,
			"RDMA_ID", 0, ULLONG_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&p->plane.base, prop, 0);
	p->rdma_id_property = prop;

	prop = drm_property_create_range(p->plane.dev, 0,
			"dec_lines", 0, 16);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&p->plane.base, prop, 0);
	p->dec_lines_property = prop;

	prop = drm_property_create_range(p->plane.dev, 0,
			"SOLID_COLOR", 0, ULLONG_MAX);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&p->plane.base, prop, 0);
	p->solid_color_property = prop;

	prop = drm_property_create(p->plane.dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"hdr_coefs", 0);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&p->plane.base, prop, 0);
	p->hdr_coef_property = prop;

	prop = drm_property_create(p->plane.dev,
			DRM_MODE_PROP_ATOMIC | DRM_MODE_PROP_BLOB,
			"scale_coefs", 0);
	if (!prop)
		return -ENOMEM;
	drm_object_attach_property(&p->plane.base, prop, 0);
	p->scale_coef_property = prop;

	ret = spacemit_drm_plane_create_color_properties(&p->plane,
					BIT(DRM_COLOR_YCBCR_BT601) |
					BIT(DRM_COLOR_YCBCR_BT709) |
					BIT(DRM_COLOR_YCBCR_BT2020),
					BIT(DRM_COLOR_YCBCR_LIMITED_RANGE) |
					BIT(DRM_COLOR_YCBCR_FULL_RANGE),
					DRM_COLOR_YCBCR_BT601, DRM_COLOR_YCBCR_LIMITED_RANGE);
	if (ret)
		DRM_ERROR("Failed to create color properties %d\n", ret);

	return 0;
}

static const struct drm_plane_helper_funcs spacemit_plane_helper_funcs = {
	.atomic_check = spacemit_plane_atomic_check,
	.atomic_update = spacemit_plane_atomic_update,
	.atomic_disable = spacemit_plane_atomic_disable,
};

static const struct drm_plane_funcs spacemit_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane	= drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = spacemit_plane_reset,
	.atomic_duplicate_state = spacemit_plane_atomic_duplicate_state,
	.atomic_destroy_state = spacemit_plane_atomic_destroy_state,
	.atomic_set_property = spacemit_plane_atomic_set_property,
	.atomic_get_property = spacemit_plane_atomic_get_property,
};

struct drm_plane *spacemit_plane_init(struct drm_device *drm,
					struct spacemit_crtc *a_crtc)
{
	struct drm_plane *primary = NULL;
	struct spacemit_plane *p = NULL;
	enum drm_plane_type plane_type;
	int err, i, j;
	u32 *formats;
	struct spacemit_drm_private *priv = drm->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	u8 n_planes = a_crtc->is_offline_mode == 1 ? hwdev->offline_plane_nums : hwdev->plane_nums;
	u8 n_formats = hwdev->n_formats;
	u8 n_fbcmems = hwdev->n_fbcmems;
	u8 n_rdmas = hwdev->rdma_nums;
	u32 plane_crtc_mask;

	trace_spacemit_plane_init(a_crtc->dev_id);
	if (n_fbcmems * 2 != n_rdmas) {
		DRM_ERROR("Unmatched rdma and fbcmem numbers, n_rdmas:%d n_fbcmems:%d!\n",
			   n_rdmas, n_fbcmems);
		err = -EINVAL;
		return ERR_PTR(err);
	}

	formats = kcalloc(n_formats, sizeof(*formats), GFP_KERNEL);
	if (!formats) {
		err = -ENOMEM;
		return ERR_PTR(err);
	}

	/* Create planes for CRTC(a_crtc->dev_id) */
	plane_crtc_mask = 1 << hwdev->crtc_num++;

	for (i = 0; i < n_planes; i++) {
		p = devm_kzalloc(drm->dev, sizeof(*p), GFP_KERNEL);
		if (!p) {
			kfree(formats);
			return ERR_PTR(-ENOMEM);
		}

		/* build the list of DRM supported formats based on the map */
		for (j = 0; j < n_formats; j++)
			formats[j] = hwdev->formats[j].format;

		plane_type = (i == 0)
			   ? DRM_PLANE_TYPE_PRIMARY
			   : DRM_PLANE_TYPE_OVERLAY;

		err = drm_universal_plane_init(drm, &p->plane, plane_crtc_mask,
					       &spacemit_plane_funcs, formats,
					       n_formats,  supported_format_modifiers,
					       plane_type, NULL);
		if (err) {
			DRM_ERROR("fail to init %s plane%d\n", (i == 0) ? "primary" : "overlay", i);
			return ERR_PTR(err);
		}

		drm_plane_helper_add(&p->plane, &spacemit_plane_helper_funcs);

		spacemit_plane_create_properties(p, i);

		p->hwdev = hwdev;
		p->hw_pid = n_planes - i - 1;
		if (i == 0)
			primary = &p->plane;
	}

	kfree(formats);

	if (p)
		DRM_INFO("dpu plane init ok\n");

	return primary;
}
