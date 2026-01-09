/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef TMG_REG_H
#define TMG_REG_H

typedef union {
	struct {
	//REGISTER saturn_tmg_reg_0
	UINT32 split_en                     : 1;
	UINT32 cmd_screen                   : 1;
	UINT32 fm_timing_en                 : 1;
	UINT32 cmd_wait_en                  : 1;
	UINT32 cmd_wait_te                  : 1;
	UINT32 disp_ready_man_en            : 1;
	UINT32 ready_1_non_active           : 1;
	UINT32 hsp                          : 1;
	UINT32 vsp                          : 1;
	UINT32 fm_cmd_tmout_eq_eof          : 1;
	UINT32 skip_frm_en                  : 1;
	UINT32 swap_rgb_en                  : 1;
	UINT32:20;

	//REGISTER saturn_tmg_reg_1
	UINT32 dsc_enable                   : 1;
	UINT32 dsc_one_line_byte_remainder  : 2;
	UINT32:29;

	//REGISTER saturn_tmg_reg_2
	UINT32 drift_timeout                : 12;
	UINT32 user                         : 4;
	UINT32 active_frm_num               : 8;
	UINT32 inactive_frm_num             : 8;

	//REGISTER saturn_tmg_reg_3
	UINT32 split_overlap                : 10;
	UINT32:6;
	UINT32 hfp                          : 12;
	UINT32:4;

	//REGISTER saturn_tmg_reg_4
	UINT32 hsync_width                  : 10;
	UINT32:6;
	UINT32 hbp                          : 12;
	UINT32:4;

	//REGISTER saturn_tmg_reg_5
	UINT32 vfp                          : 16;
	UINT32 vsync_width                  : 10;
	UINT32:6;

	//REGISTER saturn_tmg_reg_6
	UINT32 vbp                          : 12;
	UINT32:4;
	UINT32 h_active                     : 14;
	UINT32:2;

	//REGISTER saturn_tmg_reg_7
	UINT32 v_active                     : 14;
	UINT32:18;

	//REGISTER saturn_tmg_reg_8
	UINT32 fm_cmd_tmout_num             : 24;
	UINT32:8;

	//REGISTER saturn_tmg_reg_9
	UINT32 background_r                 : 12;
	UINT32:4;
	UINT32 background_g                 : 12;
	UINT32:4;

	//REGISTER saturn_tmg_reg_10
	UINT32 background_b                 : 12;
	UINT32:20;

	//REGISTER saturn_tmg_reg_11
	UINT32 eof_1st_ln_dly_num           : 16;
	UINT32 eof_2nd_ln_dly_num           : 16;

	//REGISTER saturn_tmg_reg_12
	UINT32 cfg_ln_num_intp              : 17;
	UINT32:15;

	//REGISTER saturn_tmg_reg_13
	UINT32 frame_te_dly_num             : 32;

	//REGISTER saturn_tmg_reg_14
	UINT32 sof_pre_ln_num               : 17;
	UINT32:15;

	//REGISTER saturn_tmg_reg_15
	UINT32 vsync_align_with_hsync       : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_16
	UINT32 dfc_low_thre                 : 12;
	UINT32:4;
	UINT32 dfc_high_thre                : 12;
	UINT32:4;

	//REGISTER saturn_tmg_reg_17
	UINT32 urgent_vld_en                : 1;
	UINT32:7;
	UINT32 urgent_l_thre                : 12;
	UINT32:12;

	//REGISTER saturn_tmg_reg_18
	UINT32 urgent_h_thre                : 12;
	UINT32:20;

	//REGISTER saturn_tmg_reg_19
	UINT32 dbg_disp_ready_i             : 2;
	UINT32:30;

	//REGISTER saturn_tmg_reg_20
	UINT32 dbg_tmg_cur_state_i          : 32;

	//REGISTER saturn_tmg_reg_21
	UINT32 dfc_low_hold_cycle           : 32;

	//REGISTER saturn_tmg_reg_22
	UINT32 dbg_dfc_mode                 : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_23
	UINT32 force_update_en              : 1;
	UINT32 vsync_update_en              : 1;
	UINT32 shadow_read_en               : 1;
	UINT32:29;

	//REGISTER saturn_tmg_reg_24
	UINT32 force_update_pulse           : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_25
	UINT32 icg_override                 : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_26
	UINT32 trigger                      : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_27
	UINT32 trigger2                     : 1;
	UINT32:31;

	//REGISTER saturn_tmg_reg_28
	UINT32 mem0_wr_cnt                  : 32;

	//REGISTER saturn_tmg_reg_29
	UINT32 mem1_wr_cnt                  : 32;

	};

	INT32 value32[30];

} TMG_REG;

#endif
typedef TMG_REG TMG_REG;
