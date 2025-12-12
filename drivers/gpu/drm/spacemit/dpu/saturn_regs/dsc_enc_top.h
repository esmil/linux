/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef DSC_ENC_TOP_REG_H
#define DSC_ENC_TOP_REG_H

typedef union {
	struct {
	//REGISTER dsc_enc_reg_0
	UINT32 dsc_support_version         : 4;
	UINT32 dsc_enable                  : 1;
	UINT32 full_ich_err_precision      : 1;
	UINT32:2;
	UINT32 pps_identifier              : 8;
	UINT32 linebuf_depth               : 4;
	UINT32 bits_per_component          : 4;
	UINT32:8;

	//REGISTER dsc_enc_reg_1
	UINT32 bits_per_pixel              : 10;
	UINT32 vbr_enable                  : 1;
	UINT32 simple_422                  : 1;
	UINT32 convert_rgb                 : 1;
	UINT32 block_pred_enable           : 1;
	UINT32:2;
	UINT32 pic_height                  : 16;

	//REGISTER dsc_enc_reg_2
	UINT32 pic_width                   : 16;
	UINT32 slice_height                : 16;

	//REGISTER dsc_enc_reg_3
	UINT32 slice_width                 : 16;
	UINT32 chunk_size                  : 16;

	//REGISTER dsc_enc_reg_4
	UINT32 initial_xmit_delay          : 10;
	UINT32:6;
	UINT32 initial_dec_delay           : 16;

	//REGISTER dsc_enc_reg_5
	UINT32 initial_scale_value         : 6;
	UINT32:2;
	UINT32 scale_increment_interval    : 16;
	UINT32:8;

	//REGISTER dsc_enc_reg_6
	UINT32 scale_decrement_interval    : 12;
	UINT32:4;
	UINT32 first_line_bpg_offset       : 5;
	UINT32:11;

	//REGISTER dsc_enc_reg_7
	UINT32 nfl_bpg_offset              : 16;
	UINT32 slice_bpg_offset            : 16;

	//REGISTER dsc_enc_reg_8
	UINT32 initial_offset              : 16;
	UINT32 final_offset                : 16;

	//REGISTER dsc_enc_reg_9
	UINT32 flatness_min_qp             : 5;
	UINT32:3;
	UINT32 flatness_max_qp             : 5;
	UINT32:3;
	UINT32 rc_model_size               : 16;

	//REGISTER dsc_enc_reg_10
	UINT32 rc_edge_factor              : 4;
	UINT32:4;
	UINT32 rc_quant_incr_limit0        : 5;
	UINT32:3;
	UINT32 rc_quant_incr_limit1        : 5;
	UINT32:3;
	UINT32 rc_tgt_offset_lo            : 4;
	UINT32 rc_tgt_offset_hi            : 4;

	//REGISTER dsc_enc_reg_11
	struct {
	UINT32 rc_buf_thresh               : 8;
	UINT32:24;
	} dsc_enc_reg_11[14];

	//REGISTER dsc_enc_reg_25
	struct {
	UINT32 range_min_qp                : 5;
	UINT32:27;
	} dsc_enc_reg_25[15];

	//REGISTER dsc_enc_reg_40
	struct {
	UINT32 range_max_qp                : 5;
	UINT32:27;
	} dsc_enc_reg_40[15];

	//REGISTER dsc_enc_reg_55
	struct {
	UINT32 range_bpg_offset            : 6;
	UINT32:26;
	} dsc_enc_reg_55[15];

	//REGISTER dsc_enc_reg_70
	UINT32 native_422                  : 1;
	UINT32 native_420                  : 1;
	UINT32 second_line_bpg_offset      : 5;
	UINT32:1;
	UINT32 nsl_bpg_offset              : 16;
	UINT32:8;

	//REGISTER dsc_enc_reg_71
	UINT32 second_line_offset_adj      : 16;
	UINT32:16;

	//REGISTER dsc_enc_reg_72
	struct {
	UINT32:32;
	} dsc_enc_reg_72[9];

	//REGISTER dsc_enc_reg_81
	UINT32 dbg_path_start_enable       : 1;
	UINT32 dbg_dsc_i_ready             : 1;
	UINT32 dbg_dsc_o_ready             : 1;
	UINT32 dbg_path_core_ready         : 4;
	UINT32:1;
	UINT32 dbg_path_ratebuf_ready      : 4;
	UINT32:20;

	//REGISTER dsc_enc_reg_82
	UINT32 dbg_path_in_pxl_cnt         : 16;
	UINT32 dbg_path_in_line_cnt        : 16;

	//REGISTER dsc_enc_reg_83
	struct {
	UINT32 dbg_dsc_slice_in_group_cnt  : 16;
	UINT32:16;
	} dsc_enc_reg_83[4];

	//REGISTER dsc_enc_reg_87
	struct {
	UINT32 dbg_dsc_slice_in_line_cnt   : 16;
	UINT32:16;
	} dsc_enc_reg_87[4];

	//REGISTER dsc_enc_reg_91
	struct {
	UINT32 dbg_dsc_slice_out_byte_cnt  : 16;
	UINT32:16;
	} dsc_enc_reg_91[4];

	//REGISTER dsc_enc_reg_95
	struct {
	UINT32 dbg_dsc_slice_out_line_cnt  : 16;
	UINT32:16;
	} dsc_enc_reg_95[4];

	//REGISTER dsc_enc_reg_99
	UINT32 dbg_dsc_output_byte_cnt     : 16;
	UINT32 dbg_dsc_output_line_cnt     : 16;

	//REGISTER dsc_enc_reg_100
	UINT32 dsc_dbg_irq_raw             : 32;

	//REGISTER dsc_enc_reg_101
	UINT32 dsc_dbg_irq_mask            : 32;

	//REGISTER dsc_enc_reg_102
	UINT32 dsc_dbg_irq_status          : 32;

	//REGISTER dsc_enc_reg_103
	UINT32 cfg_sec                     : 1;
	UINT32:31;

	//REGISTER dsc_enc_reg_104
	UINT32 force_update_en             : 1;
	UINT32 vsync_update_en             : 1;
	UINT32 shadow_read_en              : 1;
	UINT32:29;

	//REGISTER dsc_enc_reg_105
	UINT32 force_update_pulse          : 1;
	UINT32:31;

	//REGISTER dsc_enc_reg_106
	UINT32 force_update_en_se          : 1;
	UINT32 vsync_update_en_se          : 1;
	UINT32 shadow_read_en_se           : 1;
	UINT32:29;

	//REGISTER dsc_enc_reg_107
	UINT32 force_update_pulse_se       : 1;
	UINT32:31;

	//REGISTER dsc_enc_reg_108
	UINT32 icg_override                : 1;
	UINT32:31;

	//REGISTER dsc_enc_reg_109
	UINT32 trigger                     : 1;
	UINT32:31;

	//REGISTER dsc_enc_reg_110
	UINT32 trigger2                    : 1;
	UINT32:31;

	};

	INT32 value32[111];

} DSC_ENC_TOP_REG;

#endif
typedef DSC_ENC_TOP_REG DSC_ENC_TOP_REG;
