/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef DPU_SCENE_CTL_REG_H
#define DPU_SCENE_CTL_REG_H

typedef union {
	struct {
	//REGISTER dpu_ctl_reg_0
	UINT32 nml_rch_en             : 10;
	UINT32 nml_scl_en             : 4;
	UINT32:2;
	UINT32 nml_wb_en              : 2;
	UINT32 nml_outctl_en          : 1;
	UINT32 nml_dscw_en            : 2;
	UINT32 nml_dscr_en            : 2;
	UINT32 nml_frm_timing_en      : 3;
	UINT32:6;

	//REGISTER dpu_ctl_reg_1
	UINT32 nml_cmd_updt_en        : 1;
	UINT32:31;

	//REGISTER dpu_ctl_reg_2
	UINT32 both_cfg_rdy           : 1;
	UINT32 sw_clr                 : 1;
	UINT32 sw_clr_st              : 1;
	UINT32 nml_cfg_rdy            : 1;
	UINT32:28;

	//REGISTER dpu_ctl_reg_3
	UINT32 video_mod              : 1;
	UINT32 dbg_mod                : 1;
	UINT32 timing_inter0          : 6;
	UINT32 timing_inter1          : 6;
	UINT32:18;

	//REGISTER dpu_ctl_reg_4
	UINT32 sw_start               : 1;
	UINT32 dbg_clr                : 1;
	UINT32:30;

	//REGISTER dpu_ctl_reg_5
	UINT32 reg_nml_value_rst      : 1;
	UINT32:31;

	//REGISTER dpu_ctl_reg_6
	UINT32 nml_init_en            : 1;
	UINT32:31;

	//REGISTER dpu_ctl_reg_7
	UINT32 nml_rch_vrt_reuse      : 10;
	UINT32 cmd_auto_refresh_en    : 1;
	UINT32:21;

	//REGISTER dpu_ctl_reg_8
	UINT32 secu_rch_en            : 10;
	UINT32 secu_rch_vrt_reuse     : 10;
	UINT32 secu_scl_en            : 4;
	UINT32:8;

	//REGISTER dpu_ctl_reg_9
	UINT32 secu_cmd_updt_en       : 1;
	UINT32 secu_cfg_icg_override  : 1;
	UINT32:30;

	//REGISTER dpu_ctl_reg_10
	UINT32 secu_cfg_rdy           : 1;
	UINT32:31;

	//REGISTER dpu_ctl_reg_11
	UINT32 secu_dscw_disable      : 2;
	UINT32 secu_wb_disable        : 2;
	UINT32:28;

	//REGISTER dpu_ctl_reg_12
	UINT32 reg_secu_value_rst     : 1;
	UINT32:31;

	//REGISTER dpu_ctl_reg_13
	UINT32 scene_ctl_dbg0         : 29;
	UINT32:3;

	//REGISTER dpu_ctl_reg_14
	UINT32 scene_ctl_dbg1         : 32;

	};

	INT32 value32[15];

} DPU_SCENE_CTL_REG;

#endif
typedef DPU_SCENE_CTL_REG DPU_CTL_REG;
