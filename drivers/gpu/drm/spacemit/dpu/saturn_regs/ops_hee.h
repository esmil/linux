/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef _SATURN_hee_OPS_H_
#define _SATURN_hee_OPS_H_

#include "../../spacemit_crtc.h"

void saturn_hee_update_hdr_matrix(struct drm_plane *plane, struct spacemit_plane_state *spacemit_pstate);
void saturn_hee_update_csc_matrix(struct drm_plane *plane, struct drm_plane_state *old_state);
void saturn_hee_conf_dpuctrl_color_matrix(struct spacemit_crtc *a_crtc, struct drm_crtc_state *old_state);
void saturn_hee_conf_scaler_x(struct drm_plane_state *state, struct cmdlist_regs *cl_scl);
void saturn_hee_conf_scaler_coefs(struct drm_plane *plane, struct spacemit_plane_state *spacemit_pstate, struct cmdlist_regs
		*cl_scl);
void saturn_hee_enable_vsync(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, bool enable);
void saturn_hee_enable_cfg_irq(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, bool enable);
void saturn_hee_cfg_ready(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
void saturn_hee_sw_start(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
void saturn_hee_dpu_init(struct spacemit_crtc *a_crtc);
void saturn_hee_plane_update_hw_channel(struct drm_plane *plane, int slice_id);
void saturn_hee_plane_disable_hw_channel(struct drm_plane *plane, struct drm_plane_state *old_state);
void saturn_hee_irq_enable(struct spacemit_crtc *a_crtc, bool enable);
void saturn_hee_conf_dpuctrl(struct drm_crtc *crtc, struct drm_crtc_state *old_state);
void saturn_hee_wb_config(struct spacemit_crtc *a_crtc, struct spacemit_wb *wb, struct drm_framebuffer *fb, struct cmdlist_regs *cl_wb, int slice_id);
void saturn_hee_wb_disable(struct spacemit_crtc *a_crtc);
int saturn_hee_is_wb_en(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
uint32_t saturn_hee_get_cfg_rdy(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
uint32_t saturn_hee_get_int_sts(struct spacemit_hw_device *hwdev, int dev_id);
uint32_t saturn_hee_get_irq_bit(enum spacemit_dpu_irq irq_id, int dev_id);
void saturn_hee_clr_int_sts(struct spacemit_crtc *a_crtc, enum spacemit_dpu_irq irq, int dev_id);
void saturn_hee_dpu_disable(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
void saturn_hee_dpu_restart(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
int saturn_hee_dpu_stop_check(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev);
void saturn_hee_enable_cmdlist(struct spacemit_crtc *a_crtc, struct spacemit_hw_device *hwdev, int id, bool enable);
void saturn_hee_cfg_cmdlist(struct spacemit_hw_device *hwdev, int id, u32 chy, u32 addrl, u32 addrh);
void saturn_hee_rdma_contig_mem(struct spacemit_hw_device *hwdev, u8 tbu_id, phys_addr_t contig_pa, struct drm_framebuffer *fb, struct cmdlist_regs *cl_rdma, struct drm_plane *plane);
void saturn_hee_rdma_dmmu(struct spacemit_hw_device *hwdev, u8 tbu_id, struct tbu_instance *tbu, struct drm_framebuffer *fb, u32 val, struct cmdlist_regs *cl_rdma, struct drm_plane *plane);
void saturn_hee_wb_dmmu(struct spacemit_hw_device *hwdev, u8 tbu_id, struct tbu_instance *tbu, struct drm_framebuffer *fb, u32 val, u8 fbc_mode, int header_size, struct spacemit_wb *wb);
int saturn_hee_get_cl_rdma_buf(struct spacemit_crtc *a_crtc);
void saturn_hee_cmdlist_fill_data_row(struct cmdlist *cl, u32 strobe, u32 offset, u32 value[]);
void saturn_hee_cmdlist_fill_conf_row(struct cmdlist *cl, struct spacemit_hw_device *hwdev, u8 crtc_id);
void saturn_hee_wb_cmdlist(struct cmdlist *cl, struct spacemit_hw_device *hwdev, struct spacemit_drm_private *priv, u8 crtc_id, u8 dev_id);
void saturn_hee_cmdlist_dump_node(struct cmdlist *cl);
#ifdef CONFIG_SPACEMIT_DEBUG
void hee_dpu_dump_reg(struct spacemit_crtc *a_crtc);
void hee_dpu_dump_rdma_status(struct spacemit_drm_private *priv);
#endif
#endif
