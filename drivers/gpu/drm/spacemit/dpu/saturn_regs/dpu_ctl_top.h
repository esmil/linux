/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef DPU_CTL_TOP_REG_H
#define DPU_CTL_TOP_REG_H

typedef union {
	struct {
	//REGISTER dpu_ctl_top_reg_0
	struct {
	UINT32 cmdlist_rch_en                    : 1;
	UINT32:31;
	} dpu_ctl_top_reg_0[10];

	//REGISTER dpu_ctl_top_reg_10
	struct {
	UINT32 cmdlist_cmps_other_en             : 1;
	UINT32 cmdlist_cmps_top_en               : 1;
	UINT32:30;
	} dpu_ctl_top_reg_10[3];

	//REGISTER dpu_ctl_top_reg_13
	UINT32 cmdlist_rdma_cfg_rdy              : 10;
	UINT32 cmdlist_prepq_cfg_rdy             : 10;
	UINT32 cmdlist_cmps_cfg_rdy              : 3;
	UINT32 cmdlist_wb_cfg_rdy                : 2;
	UINT32:7;

	//REGISTER dpu_ctl_top_reg_14
	UINT32 wb0_sel_id                        : 5;
	UINT32:27;

	//REGISTER dpu_ctl_top_reg_15
	UINT32 wb1_sel_id                        : 5;
	UINT32:27;

	//REGISTER dpu_ctl_top_reg_16
	UINT32 reuse_rdma_act                    : 10;
	UINT32 reuse_prepq_act                   : 10;
	UINT32 reuse_scl_act0                    : 2;
	UINT32 reuse_cmps_act                    : 3;
	UINT32 reuse_wb_act                      : 2;
	UINT32 reuse_scl_act1                    : 2;
	UINT32:3;

	//REGISTER dpu_ctl_top_reg_17
	UINT32 rch_conflict_ints                 : 10;
	UINT32 acad_timeout_ints                 : 1;
	UINT32 wb_timeout_ints                   : 2;
	UINT32 cmdlist_rdma_cfg_timeout_ints     : 10;
	UINT32:9;

	//REGISTER dpu_ctl_top_reg_18
	UINT32 rch_conflict_int_en               : 10;
	UINT32 acad_timeout_int_en               : 1;
	UINT32 wb_timeout_int_en                 : 2;
	UINT32 cmdlist_rdma_cfg_timeout_int_en   : 10;
	UINT32:9;

	//REGISTER dpu_ctl_top_reg_19
	UINT32 rch_conflict_int_raw              : 10;
	UINT32 acad_timeout_int_raw              : 1;
	UINT32 wb_timeout_int_raw                : 2;
	UINT32 cmdlist_rdma_cfg_timeout_int_raw  : 10;
	UINT32:9;

	//REGISTER dpu_ctl_top_reg_20
	UINT32 wb_sel_secu                       : 2;
	UINT32 secu_cfg_icg_override             : 1;
	UINT32 scl_sel_secu                      : 2;
	UINT32:27;

	//REGISTER dpu_ctl_top_reg_21
	UINT32 pslverr_addr                      : 8;
	UINT32:24;

	//REGISTER dpu_ctl_top_reg_22
	struct {
	UINT32 cmdlist_ch_sw_event               : 4;
	UINT32:28;
	} dpu_ctl_top_reg_22[13];

	//REGISTER dpu_ctl_top_reg_35
	UINT32 ctl_rd_shadow                     : 1;
	UINT32:31;

	//REGISTER dpu_ctl_top_reg_36
	struct {
	UINT32 rch_start_cmps_y                  : 16;
	UINT32:16;
	} dpu_ctl_top_reg_36[10];

	//REGISTER dpu_ctl_top_reg_46
	UINT32 wb0_slice_cnt                     : 5;
	UINT32 wb1_slice_cnt                     : 5;
	UINT32 wb_busy                           : 2;
	UINT32 acad_busy                         : 2;
	UINT32 dscw_busy                         : 2;
	UINT32:16;

	//REGISTER dpu_ctl_top_reg_47
	UINT32:32;

	//REGISTER dpu_ctl_top_reg_48
	UINT32:32;

	//REGISTER dpu_ctl_top_reg_49
	UINT32 nml_scl0_reuse_en                 : 1;
	UINT32:31;


	//REGISTER dpu_ctl_top_reg_50
	UINT32 nml_scl1_reuse_en                 : 1;
	UINT32:31;

	//REGISTER dpu_ctl_top_reg_51
	UINT32 nml_scl2_reuse_en                 : 1;
	UINT32:31;

	//REGISTER dpu_ctl_top_reg_52
	UINT32 nml_scl3_reuse_en                 : 1;
	UINT32:31;

	};

	INT32 value32[53];

} DPU_CTL_TOP_REG;

#endif

typedef DPU_CTL_TOP_REG DPU_CTL_TOP_REG;
