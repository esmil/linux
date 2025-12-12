/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef DPU_INT_REG_H
#define DPU_INT_REG_H

#define DPU_INT_CMDLIST_CH_FRM_CFG_DONE_MASK	(0xff)
#define DPU_INT_CMDLIST_CH_FRM_CFG_DONE		(DPU_INT_CMDLIST_CH_FRM_CFG_DONE_MASK << 11)
#define DPU_INT_WB_OVFLOW			BIT(9)
#define DPU_INT_FRM_TIMING_UNFLOW		BIT(8)
#define DPU_INT_WB_DONE				(BIT(6) | BIT(24))
#define DPU_INT_CFG_RDY_CLR			BIT(5)
#define DPU_INT_FRM_TIMING_CFG_LINE		BIT(4)
#define DPU_INT_FRM_TIMING_CFG_EOF		(BIT(2) | BIT(3))
#define DPU_INT_FRM_TIMING_EOF			BIT(1)
#define DPU_INT_FRM_TIMING_VSYNC		BIT(0)
#define DPU_INT_RDMA_DEBUG	(BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5))
#define DPU_REST_INT_BITS			(DPU_INT_FRM_TIMING_CFG_EOF | \
						 DPU_INT_FRM_TIMING_CFG_LINE | \
						 DPU_INT_FRM_TIMING_EOF | \
						 DPU_INT_CMDLIST_CH_FRM_CFG_DONE)
/* Offline */
#define DPU_INT_OFF_CFG_RDY_CLR			BIT(0)
#define DPU_INT_OFF_WB_DONE			BIT(1)
#define DPU_INT_OFF_SLICE_DONE			BIT(3)
#define DPU_INT_OFF_CL_CH_DONE_MASK		(0xff)
#define DPU_INT_OFF_CMDLIST_CH_DONE		(DPU_INT_OFF_CL_CH_DONE_MASK << 5)
#define DPU_OFF_REST_INT_BITS			(DPU_INT_OFF_CMDLIST_CH_DONE | \
						 DPU_INT_OFF_SLICE_DONE)

typedef union {
	struct {
	//REGISTER dpu_int_reg_0
	UINT32:32;

	//REGISTER dpu_int_reg_1
	UINT32:32;

	//REGISTER dpu_int_reg_2
	UINT32:32;

	//REGISTER dpu_int_reg_3
	UINT32 cmb_frm_timing_vsync_int_msk              : 1;
	UINT32 cmb_frm_timing_eof_int_msk                : 1;
	UINT32 cmb_frm_timing_cfg_eof_int_msk            : 2;
	UINT32 cmb_frm_timing_cfg_line_int_msk           : 1;
	UINT32 cmb_cfg_rdy_clr_int_msk                   : 1;
	UINT32 cmb_wb_frm_done_int_msk                   : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_int_msk             : 1;
	UINT32 cmb_wb_ovflow_int_msk                     : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_int_msk       : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_int_msk                 : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_int_msk             : 1;
	UINT32 acad_eof_int_msk                          : 1;
	UINT32 cmb_te_int_msk                            : 1;
	UINT32 cmb_vsync_updt_int_msk                    : 1;
	UINT32 cmb_vsync_done_free_int_msk               : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_4
	UINT32 cmb_cmdlist_ch_enter_pend_int_msk         : 8;
	UINT32 cmb_arb_dec_pslverr_int_msk               : 1;
	UINT32 cmb_cfg_eof_free_int_msk                  : 2;
	UINT32 cmb_eof_free_int_msk                      : 1;
	UINT32 cmb_sof_free_int_msk                      : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_5
	UINT32 cmb_dma_dbg_int_msk                       : 16;
	UINT32 cmb_outctl_dbg_int_msk                    : 1;
	UINT32 cmb_ctl_dbg_int_msk                       : 1;
	UINT32 cmb_cmdlist_dbg_int_msk                   : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_6
	UINT32 offl0_cfg_rdy_clr_int_msk                 : 1;
	UINT32 offl0_wb_frm_done_int_msk                 : 1;
	UINT32:1;
	UINT32 offl0_wb_slice_done_int_msk               : 1;
	UINT32:1;
	UINT32 offl0_cmdlist_ch_frm_cfg_done_int_msk     : 8;
	UINT32:19;

	//REGISTER dpu_int_reg_7
	UINT32 offl0_cmdlist_ch_enter_pend_int_msk       : 8;
	UINT32 offl0_arb_dec_pslverr_int_msk             : 1;
	UINT32:23;

	//REGISTER dpu_int_reg_8
	UINT32 offl0_nml_dma_dbg_int_msk                 : 16;
	UINT32 offl0_nml_ctl_dbg_int_msk                 : 1;
	UINT32 offl0_nml_cmdlist_dbg_int_msk             : 1;
	UINT32:14;

	//REGISTER dpu_int_reg_9
	UINT32:32;

	//REGISTER dpu_int_reg_10
	UINT32:32;

	//REGISTER dpu_int_reg_11
	UINT32:32;

	//REGISTER dpu_int_reg_12
	UINT32 cmb_frm_timing_vsync_ints                 : 1;
	UINT32 cmb_frm_timing_eof_ints                   : 1;
	UINT32 cmb_frm_timing_cfg_eof_ints               : 2;
	UINT32 cmb_frm_timing_cfg_line_ints              : 1;
	UINT32 cmb_cfg_rdy_clr_ints                      : 1;
	UINT32 cmb_wb_frm_done_ints                      : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_ints                : 1;
	UINT32 cmb_wb_ovflow_ints                        : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_ints          : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_ints                    : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_ints                : 1;
	UINT32 acad_eof_ints                             : 1;
	UINT32 cmb_te_ints                               : 1;
	UINT32 cmb_vsync_updt_ints                       : 1;
	UINT32 cmb_vsync_done_free_ints                  : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_13
	UINT32 cmb_cmdlist_ch_enter_pend_ints            : 8;
	UINT32 cmb_arb_dec_pslverr_ints                  : 1;
	UINT32 cmb_cfg_eof_free_ints                     : 2;
	UINT32 cmb_eof_free_ints                         : 1;
	UINT32 cmb_sof_free_ints                         : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_14
	UINT32 cmb_dma_dbg_ints                          : 16;
	UINT32 cmb_outctl_dbg_ints                       : 1;
	UINT32 cmb_ctl_dbg_ints                          : 1;
	UINT32 cmb_cmdlist_dbg_ints                      : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_15
	UINT32 offl0_cfg_rdy_clr_ints                    : 1;
	UINT32 offl0_wb_frm_done_ints                    : 1;
	UINT32:1;
	UINT32 offl0_wb_slice_done_ints                  : 1;
	UINT32:1;
	UINT32 offl0_cmdlist_ch_frm_cfg_done_ints        : 8;
	UINT32:19;

	//REGISTER dpu_int_reg_16
	UINT32 off0_cmdlist_ch_enter_pend_ints           : 8;
	UINT32 off0_arb_dec_pslverr_ints                 : 1;
	UINT32:23;

	//REGISTER dpu_int_reg_17
	UINT32 offl0_nml_dma_dbg_ints                    : 16;
	UINT32 offl0_nml_ctl_dbg_ints                    : 1;
	UINT32 offl0_nml_cmdlist_dbg_ints                : 1;
	UINT32:14;

	//REGISTER dpu_int_reg_18
	UINT32:32;

	//REGISTER dpu_int_reg_19
	UINT32:32;

	//REGISTER dpu_int_reg_20
	UINT32:32;

	//REGISTER dpu_int_reg_21
	UINT32:32;

	//REGISTER dpu_int_reg_22
	UINT32 cmb_frm_timing_vsync_int_raw              : 1;
	UINT32 cmb_frm_timing_eof_int_raw                : 1;
	UINT32 cmb_frm_timing_cfg_eof_int_raw            : 2;
	UINT32 cmb_frm_timing_cfg_line_int_raw           : 1;
	UINT32 cmb_cfg_rdy_clr_int_raw                   : 1;
	UINT32 cmb_wb_frm_done_int_raw                   : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_int_raw             : 1;
	UINT32 cmb_wb_ovflow_int_raw                     : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_int_raw       : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_int_raw                 : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_int_raw             : 1;
	UINT32 acad_eof_int_raw                          : 1;
	UINT32 cmb_te_int_raw                            : 1;
	UINT32 cmb_vsync_updt_int_raw                    : 1;
	UINT32 cmb_vsync_done_free_int_raw               : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_23
	UINT32 cmb_cmdlist_ch_enter_pend_int_raw         : 8;
	UINT32 cmb_arb_dec_pslverr_int_raw               : 1;
	UINT32 cmb_cfg_eof_free_int_raw                  : 2;
	UINT32 cmb_eof_free_int_raw                      : 1;
	UINT32 cmb_sof_free_int_raw                      : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_24
	UINT32 cmb_dma_dbg_int_raw                       : 16;
	UINT32 cmb_outctl_dbg_int_raw                    : 1;
	UINT32 cmb_ctl_dbg_int_raw                       : 1;
	UINT32 cmb_cmdlist_dbg_int_raw                   : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_25
	UINT32 cmb_arb_dec_paddr                         : 21;
	UINT32:11;

	//REGISTER dpu_int_reg_26
	UINT32 offl0_cfg_rdy_clr_int_raw                 : 1;
	UINT32 offl0_wb_frm_done_int_raw                 : 1;
	UINT32:1;
	UINT32 offl0_wb_slice_done_int_raw               : 1;
	UINT32:1;
	UINT32 offl0_cmdlist_ch_frm_cfg_done_int_raw     : 8;
	UINT32:19;

	//REGISTER dpu_int_reg_27
	UINT32 off0_cmdlist_ch_enter_pend_int_raw        : 8;
	UINT32 off0_arb_dec_pslverr_int_raw              : 1;
	UINT32:23;

	//REGISTER dpu_int_reg_28
	UINT32 offl0_nml_dma_dbg_int_raw                 : 16;
	UINT32 offl0_nml_ctl_dbg_int_raw                 : 1;
	UINT32 offl0_nml_cmdlist_dbg_int_raw             : 1;
	UINT32:14;

	//REGISTER dpu_int_reg_29
	UINT32 offl0_arb_dec_paddr                       : 21;
	UINT32:11;

	//REGISTER dpu_int_reg_30
	UINT32:32;

	//REGISTER dpu_int_reg_31
	UINT32:32;

	//REGISTER dpu_int_reg_32
	UINT32:32;

	//REGISTER dpu_int_reg_33
	UINT32 cmb_frm_timing_vsync_secu_int_msk         : 1;
	UINT32 cmb_frm_timing_eof_secu_int_msk           : 1;
	UINT32 cmb_frm_timing_cfg_eof_secu_int_msk       : 2;
	UINT32 cmb_frm_timing_cfg_line_secu_int_msk      : 1;
	UINT32 cmb_cfg_rdy_clr_secu_int_msk              : 1;
	UINT32 cmb_wb_frm_done_secu_int_msk              : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_secu_int_msk        : 1;
	UINT32 cmb_wb_ovflow_secu_int_msk                : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_secu_int_msk  : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_secu_int_msk            : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_secu_int_msk        : 1;
	UINT32 acad_eof_secu_int_msk                     : 1;
	UINT32 cmb_te_secu_int_msk                       : 1;
	UINT32 cmb_vsync_updt_secu_int_msk               : 1;
	UINT32 cmb_vsync_done_free_secu_int_msk          : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_34
	UINT32 cmb_cmdlist_ch_enter_pend_secu_int_msk    : 8;
	UINT32 cmb_arb_dec_pslverr_secu_int_msk          : 1;
	UINT32 cmb_cfg_eof_free_secu_int_msk             : 2;
	UINT32 cmb_eof_free_secu_int_msk                 : 1;
	UINT32 cmb_sof_free_secu_int_msk                 : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_35
	UINT32 cmb_dma_dbg_secu_int_msk                  : 16;
	UINT32 cmb_outctl_dbg_secu_int_msk               : 1;
	UINT32 cmb_ctl_dbg_secu_int_msk                  : 1;
	UINT32 cmb_cmdlist_dbg_secu_int_msk              : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_36
	UINT32:32;

	//REGISTER dpu_int_reg_37
	UINT32:32;

	//REGISTER dpu_int_reg_38
	UINT32:32;

	//REGISTER dpu_int_reg_39
	UINT32 cmb_frm_timing_vsync_secu_ints            : 1;
	UINT32 cmb_frm_timing_eof_secu_ints              : 1;
	UINT32 cmb_frm_timing_cfg_eof_secu_ints          : 2;
	UINT32 cmb_frm_timing_cfg_line_secu_ints         : 1;
	UINT32 cmb_cfg_rdy_clr_secu_ints                 : 1;
	UINT32 cmb_wb_frm_done_secu_ints                 : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_secu_ints           : 1;
	UINT32 cmb_wb_ovflow_secu_ints                   : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_secu_ints     : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_secu_ints               : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_secu_ints           : 1;
	UINT32 acad_eof_secu_ints                        : 1;
	UINT32 cmb_te_secu_ints                          : 1;
	UINT32 cmb_vsync_updt_secu_ints                  : 1;
	UINT32 cmb_vsync_done_free_secu_ints             : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_40
	UINT32 cmb_cmdlist_ch_enter_pend_secu_ints       : 8;
	UINT32 cmb_arb_dec_pslverr_secu_ints             : 1;
	UINT32 cmb_cfg_eof_free_secu_ints                : 2;
	UINT32 cmb_eof_free_secu_ints                    : 1;
	UINT32 cmb_sof_free_secu_ints                    : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_41
	UINT32 cmb_dma_dbg_secu_ints                     : 16;
	UINT32 cmb_outctl_dbg_secu_ints                  : 1;
	UINT32 cmb_ctl_dbg_secu_ints                     : 1;
	UINT32 cmb_cmdlist_dbg_secu_ints                 : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_42
	struct {
	UINT32:32;
	} dpu_int_reg_42[4];

	//REGISTER dpu_int_reg_46
	UINT32 cmb_frm_timing_vsync_secu_int_raw         : 1;
	UINT32 cmb_frm_timing_eof_secu_int_raw           : 1;
	UINT32 cmb_frm_timing_cfg_eof_secu_int_raw       : 2;
	UINT32 cmb_frm_timing_cfg_line_secu_int_raw      : 1;
	UINT32 cmb_cfg_rdy_clr_secu_int_raw              : 1;
	UINT32 cmb_wb_frm_done_secu_int_raw              : 1;
	UINT32:1;
	UINT32 cmb_frm_timing_unflow_secu_int_raw        : 1;
	UINT32 cmb_wb_ovflow_secu_int_raw                : 1;
	UINT32:1;
	UINT32 cmb_cmdlist_ch_frm_cfg_done_secu_int_raw  : 8;
	UINT32:5;
	UINT32 cmb_wb_slice_done_secu_int_raw            : 1;
	UINT32:1;
	UINT32 video_mode_enter_idle_secu_int_raw        : 1;
	UINT32 acad_eof_secu_int_raw                     : 1;
	UINT32 cmb_te_secu_int_raw                       : 1;
	UINT32 cmb_vsync_updt_secu_int_raw               : 1;
	UINT32 cmb_vsync_done_free_secu_int_raw          : 1;
	UINT32:1;

	//REGISTER dpu_int_reg_47
	UINT32 cmb_cmdlist_ch_enter_pend_secu_int_raw    : 8;
	UINT32 cmb_arb_dec_pslverr_secu_int_raw          : 1;
	UINT32 cmb_cfg_eof_free_secu_int_raw             : 2;
	UINT32 cmb_eof_free_secu_int_raw                 : 1;
	UINT32 cmb_sof_free_secu_int_raw                 : 1;
	UINT32:19;

	//REGISTER dpu_int_reg_48
	UINT32 cmb_dma_dbg_secu_int_raw                  : 16;
	UINT32 cmb_outctl_dbg_secu_int_raw               : 1;
	UINT32 cmb_ctl_dbg_secu_int_raw                  : 1;
	UINT32 cmb_cmdlist_dbg_secu_int_raw              : 1;
	UINT32:13;

	//REGISTER dpu_int_reg_49
	UINT32 cmb_secu_arb_dec_paddr                    : 21;
	UINT32:11;

	} b;

	INT32 value32[50];

} DPU_INT_REG;
typedef DPU_INT_REG DPU_INTP_REG;
#endif
