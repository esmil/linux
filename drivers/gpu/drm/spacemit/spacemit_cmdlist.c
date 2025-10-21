// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/sort.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include "spacemit_cmdlist.h"
#include "spacemit_crtc.h"
#include "spacemit_dpu_reg.h"
#include "spacemit_drm.h"
#include "dpu/dpu_saturn.h"
#include "dpu/dpu_trace.h"

inline struct spacemit_plane_state *cl_to_spacemit_pstate(const struct cmdlist *cl)
{
	return container_of(cl, struct spacemit_plane_state, cl[cl->index]);
}

inline struct spacemit_crtc_state *cl_to_spacemit_cstate(const struct cmdlist *cl)
{
	return container_of(cl, struct spacemit_crtc_state, cl[cl->index]);
}

struct cmdlist *plane_to_cl(struct drm_plane *plane)
{
	struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(plane->state);
	u8 cur_cl = spacemit_pstate->cur_cl;

	return (struct cmdlist *)(&spacemit_pstate->cl[cur_cl]);
}

struct cmdlist *crtc_to_cl(struct drm_crtc *crtc)
{
	struct spacemit_crtc_state *spacemit_cstate = to_spacemit_crtc_state(crtc->state);
	u8 cur_cl = spacemit_cstate->cur_cl;

	return (struct cmdlist *)(&spacemit_cstate->cl[cur_cl]);
}

void print_row(u32 *row)
{
	u32 *p = row;

	DRM_DEBUG("%s: 0x%02x, 0x%02x, 0x%02x, 0x%02x", __func__, *p, *(p+1), *(p+2), *(p+3));
}

static void cmdlist_dump_node(struct cmdlist *cl)
{
	struct spacemit_drm_private *priv;
	struct spacemit_hw_device *hwdev;
	struct spacemit_crtc *a_crtc;
	char *type;
	int zpos = -1, rdma_id = -1;

	if (cl == NULL)
		return;

	if (cl->type == CMDLIST_PLANE) {
		struct spacemit_plane_state *spacemit_pstate = cl_to_spacemit_pstate(cl);

		a_crtc = to_spacemit_crtc(spacemit_pstate->state.crtc);
		rdma_id = spacemit_pstate->rdma_id;
		zpos = (int)(spacemit_pstate->state.zpos);
		type = "RDMA";
	} else if (cl->type == CMDLIST_CRTC) {
		struct spacemit_crtc_state *spacemit_cstate = cl_to_spacemit_cstate(cl);

		a_crtc = to_spacemit_crtc(spacemit_cstate->base.crtc);
		type = "CRTC";
	} else {
		trace_printk("Dump Unknown cmdlist group\n");
		return;
	}

	trace_cmdlist_dump_node(type, cl->index, rdma_id, a_crtc->dev_id, zpos, cl->nod_len);

	priv = a_crtc->crtc.dev->dev_private;
	hwdev = priv->hwdev;
	hwdev->cmdlist_dump_node(cl);

}

void cmdlist_regs_packing(struct cmdlist *cl, cmdlist_mode_type_t mod, struct cmdlist_regs *cl_regs)
{
	struct spacemit_crtc *a_crtc;
	struct spacemit_drm_private *priv;
	struct spacemit_hw_device *hwdev;
	u32 *reg_base;
	int i;
	u32 index;

	if (cl_regs == NULL)
		return;

	if (cl->type == CMDLIST_PLANE) {
		struct spacemit_plane_state *spacemit_pstate = cl_to_spacemit_pstate(cl);

		a_crtc = to_spacemit_crtc(spacemit_pstate->state.crtc);

		DRM_DEBUG("-----%s----- rch_id = %d, ch_y = %u\n", __func__,
			  spacemit_pstate->rdma_id, spacemit_pstate->state.crtc_y);
	} else if (cl->type == CMDLIST_CRTC) {
		struct spacemit_crtc_state *spacemit_cstate = cl_to_spacemit_cstate(cl);

		a_crtc = to_spacemit_crtc(spacemit_cstate->base.crtc);
	} else {
		DRM_ERROR("Unsupported cmdlist type:%d\n", cl->type);
		return;
	}

	priv = a_crtc->crtc.dev->dev_private;
	hwdev = priv->hwdev;
	reg_base = (u32 *)(cl_regs->module);

	cl->mode_mask |= mod;
	if (cl->va == NULL) {
		cl->size = PER_CMDLIST_SIZE;
		cl->va = dma_alloc_coherent(a_crtc->dev, cl->size, &cl->pa, GFP_KERNEL | __GFP_ZERO);
		if (cl->va == NULL) {
			DRM_ERROR("Failed to allocate %d bytes for dpu type:%d cmdlist buffer\n",
				  PER_CMDLIST_SIZE, cl->type);
			return;
		}
	}

	for (i = 0; i < cl_regs->size;) {
		u32 value[CMDLIST_ROW_REGS] = {0x0};
		u32 offset, strobe = 0;
		u8 regs_in_row = 0;

		if (cl_regs->flags[i]) {
			offset = cl_regs->base + i * sizeof(u32);
			for (u8 j = 0; j < CMDLIST_ROW_REGS; j++) {
				index = i + j;
				if (likely(index < cl_regs->size)) {
					if (cl_regs->flags[index]) {
						value[j] = reg_base[index];
						regs_in_row++;
						strobe |= CMDLIST_REG_STROBE(j);
					}
				} else
					break;
			}
			hwdev->cmdlist_fill_data_row(cl, strobe, offset, value);
			i += regs_in_row;
			cl->nod_len++;
		} else
			i++;
	}

	DRM_DEBUG("-----%s----- row_num = %d\n", __func__, cl->nod_len);
}

void cmdlist_sort_by_group(struct drm_crtc *crtc)
{
	struct cmdlist *first_cl;
	struct cmdlist *last_cl;
	struct cmdlist *p;
	struct cmdlist *prev;
	struct drm_plane *plane;
	struct spacemit_crtc_rdma *rdmas = to_spacemit_crtc_state(crtc->state)->rdmas;
	struct spacemit_drm_private *priv = crtc->dev->dev_private;

#ifndef CONFIG_DRM_SPACEMIT_CMDLIST
	return;
#endif
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		struct spacemit_plane_state *spacemit_pstate = to_spacemit_plane_state(plane->state);
		u32 rdma_id = spacemit_pstate->rdma_id;

		first_cl = (struct cmdlist *)(&(spacemit_pstate->cl[0]));

		if (!first_cl->va)
			continue;
		else {
			int i;
			struct cmdlist *tmp_cl = first_cl;

			/* Add a plane's cmdlist items into a list */
			for (i = 1; i < MAX_CL_NUM; i++) {
				if (spacemit_pstate->cl[i].va) {
					tmp_cl->next = (struct cmdlist *)(&(spacemit_pstate->cl[i]));
					tmp_cl = tmp_cl->next;
				}
			}
			last_cl = tmp_cl;
			last_cl->next = NULL;
		}

		rdmas[rdma_id].in_use = true;
		if (priv->cmdlist_groups[rdma_id]) {
			p = priv->cmdlist_groups[rdma_id];
			prev = NULL;
			while (p) {
				if (cl_to_spacemit_pstate(p)->state.crtc_y < spacemit_pstate->state.crtc_y) {
					prev = p;
					p = p->next;
				} else
					break;
			}
			if (!prev) {
				priv->cmdlist_groups[rdma_id] = first_cl;
				last_cl->next = p;
			} else {
				prev->next = first_cl;
				last_cl->next = p;
			}
		} else
			priv->cmdlist_groups[rdma_id] = first_cl;
	}
}

void cmdlist_atomic_commit(struct drm_crtc *crtc,
			   struct drm_crtc_state *old_state)
{
	int i;
	struct cmdlist *cur_cl, *first_cl;
	struct spacemit_crtc *a_crtc = NULL;
	u32 chy, addrl, addrh;
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct spacemit_crtc_rdma *cur_rdmas = to_spacemit_crtc_state(crtc->state)->rdmas;
	struct spacemit_crtc_rdma *old_rdmas = to_spacemit_crtc_state(old_state)->rdmas;

#ifndef CONFIG_DRM_SPACEMIT_CMDLIST
	return;
#endif
	a_crtc = to_spacemit_crtc(crtc);

	for (i = 0; i < hwdev->rdma_nums; i++) {
		/* Shut down the rdma used in previous frame first */
		if (old_rdmas[i].in_use) {
			if (a_crtc->is_stopped == false)
				hwdev->enable_cmdlist(a_crtc, hwdev, i, false);
			a_crtc->dpuctrl_ctl_nml_cmdlist_rch_en[i] = 0;
		}

		if (cur_rdmas[i].in_use) {
			DRM_DEBUG("+++++%s+++++ cmdlist group = %d\n", __func__, i);
			cur_cl = priv->cmdlist_groups[i];
			first_cl = cur_cl;
			while (cur_cl) {
				hwdev->cmdlist_fill_conf_row(cur_cl, hwdev, CMDLIST_CMP_INVALID);
				cmdlist_dump_node(cur_cl);
				cur_cl = cur_cl->next;
			}
			chy = cl_to_spacemit_pstate(first_cl)->state.crtc_y;
			addrl = ((priv->cmdlist_groups[i]->pa) & CMDLIST_ADDRL_ALIGN_MASK) >> CMDLIST_ADDRL_ALIGN_BITS;

			addrh = (priv->cmdlist_groups[i]->pa) >> 32;
			hwdev->cfg_cmdlist(hwdev, i, chy, addrl, addrh);
			if (a_crtc->is_stopped == false)
				hwdev->enable_cmdlist(a_crtc, hwdev, i, true);
			a_crtc->dpuctrl_ctl_nml_cmdlist_rch_en[i] = 1;
			priv->cmdlist_groups[i] = NULL;
		}
	}
}

void wb_cmdlist_sort_by_group(void *data)
{
	struct cmdlist *first_cl;
	struct cmdlist *last_cl;
	struct spacemit_crtc *a_crtc = (struct spacemit_crtc *)data;
	struct drm_crtc *crtc = &(a_crtc->crtc);
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	struct drm_crtc_state *crtc_state = a_crtc->crtc.state;
	struct spacemit_crtc_state *spacemit_crtc_state = to_spacemit_crtc_state(crtc_state);
	u8 crtc_id = drm_crtc_index(crtc);
	int i;
	struct cmdlist *tmp_cl;

	first_cl =  (struct cmdlist *)(&(spacemit_crtc_state->cl[0]));

	tmp_cl = first_cl;

	for (i = 1; i < MAX_CL_NUM; i++) {
		if (spacemit_crtc_state->cl[i].va) {
			tmp_cl->next = (struct cmdlist *)(&(spacemit_crtc_state->cl[i]));
			tmp_cl = tmp_cl->next;
		} else
			break;
	}
	last_cl = tmp_cl;
	last_cl->next = NULL;
	priv->cmdlist_groups[hwdev->rdma_nums + crtc_id] = first_cl;
}

void wb_cmdlist_atomic_commit(void *data)
{
	struct cmdlist *cur_cl;
	struct spacemit_crtc *a_crtc = (struct spacemit_crtc *)data;
	struct drm_crtc *crtc = &(a_crtc->crtc);
	struct spacemit_drm_private *priv = crtc->dev->dev_private;
	struct spacemit_hw_device *hwdev = priv->hwdev;
	u8 crtc_id = drm_crtc_index(crtc);
	u8 dev_id = a_crtc->dev_id;

	DRM_DEBUG("+++++cmdlist_atomic_commit+++++ cmdlist group = %d\n", hwdev->rdma_nums + crtc_id);
	cur_cl = priv->cmdlist_groups[hwdev->rdma_nums + crtc_id];
	while (cur_cl) {
		hwdev->cmdlist_fill_conf_row(cur_cl, hwdev, dev_id);
		cmdlist_dump_node(cur_cl);
		cur_cl = cur_cl->next;
	}
	hwdev->wb_cmdlist(cur_cl, hwdev, priv, crtc_id, dev_id);
}
