/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef DPU_TOP_REG_H
#define DPU_TOP_REG_H

typedef union {
	struct {
	//REGISTER saturn_led_top_reg_0
	UINT32 Minor_number           : 8;
	UINT32 Major_number           : 8;
	UINT32 Product_ID             : 16;

	//REGISTER saturn_led_top_reg_1
	struct {
	UINT32:32;
	} saturn_led_top_reg_1[3];

	//REGISTER saturn_led_top_reg_4
	UINT32 rd_sdw_reg_en          : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_5
	UINT32:32;

	//REGISTER saturn_led_top_reg_6
	UINT32 shut_down_num          : 8;
	UINT32:24;

	//REGISTER saturn_led_top_reg_7
	UINT32 deep_slep_num          : 8;
	UINT32:24;

	//REGISTER saturn_led_top_reg_8
	struct {
	UINT32:32;
	} saturn_led_top_reg_8[3];

	//REGISTER saturn_led_top_reg_11
	UINT32 rdma_pclk_cg_en        : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_12
	UINT32 pre_ly_pclk_cg_en      : 6;
	UINT32:26;

	//REGISTER saturn_led_top_reg_13
	UINT32 cmps_pclk_cg_en        : 3;
	UINT32:29;

	//REGISTER saturn_led_top_reg_14
	UINT32 scale_pclk_cg_en       : 4;
	UINT32:28;

	//REGISTER saturn_led_top_reg_15
	UINT32 postpipe_pclk_cg_en    : 8;
	UINT32:24;

	//REGISTER saturn_led_top_reg_16
	UINT32 wb_pclk_cg_en          : 2;
	UINT32:30;

	//REGISTER saturn_led_top_reg_17
	struct {
	UINT32:32;
	} saturn_led_top_reg_17[5];

	//REGISTER saturn_led_top_reg_22
	UINT32 top_clk_auto_en        : 9;
	UINT32:23;

	//REGISTER saturn_led_top_reg_23
	UINT32 ctl_clk_auto_en        : 2;
	UINT32:30;

	//REGISTER saturn_led_top_reg_24
	UINT32 cmdlist_clk_auto_en    : 2;
	UINT32:30;

	//REGISTER saturn_led_top_reg_25
	UINT32 rdma_aclk_auto_en      : 4;
	UINT32:28;

	//REGISTER saturn_led_top_reg_26
	UINT32 pre_ly_mclk_auto_en    : 6;
	UINT32:26;

	//REGISTER saturn_led_top_reg_27
	UINT32 cmps_clk_auto_en       : 3;
	UINT32:29;

	//REGISTER saturn_led_top_reg_28
	UINT32 scale_mclk_auto_en     : 4;
	UINT32:28;

	//REGISTER saturn_led_top_reg_29
	UINT32 postpipe_clk_auto_en   : 17;
	UINT32:15;

	//REGISTER saturn_led_top_reg_30
	UINT32 tmg_clk_auto_en        : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_31
	UINT32 wb_clk_auto_en         : 4;
	UINT32:28;

	//REGISTER saturn_led_top_reg_32
	UINT32 dma_mem_lp_en          : 20;
	UINT32:12;

	//REGISTER saturn_led_top_reg_33
	UINT32 postpipe_mem_lp_en     : 10;
	UINT32:22;

	//REGISTER saturn_led_top_reg_34
	UINT32 tmg_mem_lp_en          : 1;
	UINT32 wb_mem_lp_en           : 1;
	UINT32 scale_mem_lp_en        : 1;
	UINT32 scale_obuf_mem_lp_en   : 1;
	UINT32:28;

	//REGISTER saturn_led_top_reg_35
	UINT32:32;

	//REGISTER saturn_led_top_reg_36
	UINT32 prepipe0_valid         : 1;
	UINT32 prepipe0_ready         : 1;
	UINT32 prepipe1_valid         : 1;
	UINT32 prepipe1_ready         : 1;
	UINT32 prepipe2_valid         : 1;
	UINT32 prepipe2_ready         : 1;
	UINT32 prepipe3_valid         : 1;
	UINT32 prepipe3_ready         : 1;
	UINT32 prepipe4_valid         : 1;
	UINT32 prepipe4_ready         : 1;
	UINT32 prepipe5_valid         : 1;
	UINT32 prepipe5_ready         : 1;
	UINT32 compser0_valid         : 1;
	UINT32 compser0_ready         : 1;
	UINT32 compser1_valid         : 1;
	UINT32 compser1_ready         : 1;
	UINT32 pospipe_valid          : 1;
	UINT32 postpipe_ready         : 1;
	UINT32 scl_1d_valid           : 1;
	UINT32 scl_1d_ready           : 1;
	UINT32 scl_2d_valid           : 1;
	UINT32 scl_2d_ready           : 1;
	UINT32:10;

	//REGISTER saturn_led_top_reg_37
	UINT32 cfg_se                 : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_38
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER saturn_led_top_reg_39
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_40
	UINT32 force_update_en_se     : 1;
	UINT32 vsync_update_en_se     : 1;
	UINT32 shadow_read_en_se      : 1;
	UINT32:29;

	//REGISTER saturn_led_top_reg_41
	UINT32 force_update_pulse_se  : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_42
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_43
	UINT32 trigger                : 1;
	UINT32:31;

	//REGISTER saturn_led_top_reg_44
	UINT32 trigger2               : 1;
	UINT32:31;

	};

	INT32 value32[45];

} DPU_TOP_REG;

#endif
typedef DPU_TOP_REG DPU_TOP_REG;
