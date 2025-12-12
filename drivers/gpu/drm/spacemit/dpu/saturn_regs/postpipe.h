/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef POSTPIPE_REG_H
#define POSTPIPE_REG_H

typedef union {
	struct {
	//REGISTER saturn_postpipe_reg_0
	UINT32 dither_en           : 1;
	UINT32 postpq_en           : 1;
	UINT32 dither_mem_lp_en    : 1;
	UINT32 rgb2yuv_en          : 1;
	UINT32 yuv2rgb_en          : 1;
	UINT32 split_en            : 1;
	UINT32:26;

	//REGISTER saturn_postpipe_reg_1
	UINT32 rgb2yuv_matrix00    : 16;
	UINT32 rgb2yuv_matrix01    : 16;

	//REGISTER saturn_postpipe_reg_2
	UINT32 rgb2yuv_matrix02    : 16;
	UINT32 rgb2yuv_matrix03    : 16;

	//REGISTER saturn_postpipe_reg_3
	UINT32 rgb2yuv_matrix10    : 16;
	UINT32 rgb2yuv_matrix11    : 16;

	//REGISTER saturn_postpipe_reg_4
	UINT32 rgb2yuv_matrix12    : 16;
	UINT32 rgb2yuv_matrix13    : 16;

	//REGISTER saturn_postpipe_reg_5
	UINT32 rgb2yuv_matrix20    : 16;
	UINT32 rgb2yuv_matrix21    : 16;

	//REGISTER saturn_postpipe_reg_6
	UINT32 rgb2yuv_matrix22    : 16;
	UINT32 rgb2yuv_matrix23    : 16;

	//REGISTER saturn_postpipe_reg_7
	UINT32 yuv2rgb_matrix00    : 16;
	UINT32 yuv2rgb_matrix01    : 16;

	//REGISTER saturn_postpipe_reg_8
	UINT32 yuv2rgb_matrix02    : 16;
	UINT32 yuv2rgb_matrix03    : 16;

	//REGISTER saturn_postpipe_reg_9
	UINT32 yuv2rgb_matrix10    : 16;
	UINT32 yuv2rgb_matrix11    : 16;

	//REGISTER saturn_postpipe_reg_10
	UINT32 yuv2rgb_matrix12    : 16;
	UINT32 yuv2rgb_matrix13    : 16;

	//REGISTER saturn_postpipe_reg_11
	UINT32 yuv2rgb_matrix20    : 16;
	UINT32 yuv2rgb_matrix21    : 16;

	//REGISTER saturn_postpipe_reg_12
	UINT32 yuv2rgb_matrix22    : 16;
	UINT32 yuv2rgb_matrix23    : 16;

	//REGISTER saturn_postpipe_reg_13
	UINT32 m_inwidth           : 16;
	UINT32 m_inheight          : 16;

	//REGISTER saturn_postpipe_reg_14
	UINT32 split_overlap       : 10;
	UINT32:22;

	//REGISTER saturn_postpipe_reg_15
	struct {
	UINT32:32;
	} saturn_postpipe_reg_15[4];

	//REGISTER saturn_postpipe_reg_19
	UINT32 dither_mode         : 1;
	UINT32 dither_auto_temp    : 1;
	UINT32 dither_rotate_mode  : 2;
	UINT32 dither_out_dpth0    : 4;
	UINT32 dither_out_dpth1    : 4;
	UINT32 dither_out_dpth2    : 4;
	UINT32 dither_temp_value   : 8;
	UINT32 dither_pattern_bit  : 3;
	UINT32:5;

	//REGISTER saturn_postpipe_reg_20
	UINT32 dither_bayer_map00  : 8;
	UINT32 dither_bayer_map01  : 8;
	UINT32 dither_bayer_map02  : 8;
	UINT32 dither_bayer_map03  : 8;

	//REGISTER saturn_postpipe_reg_21
	UINT32 dither_bayer_map04  : 8;
	UINT32 dither_bayer_map05  : 8;
	UINT32 dither_bayer_map06  : 8;
	UINT32 dither_bayer_map07  : 8;

	//REGISTER saturn_postpipe_reg_22
	UINT32 dither_bayer_map10  : 8;
	UINT32 dither_bayer_map11  : 8;
	UINT32 dither_bayer_map12  : 8;
	UINT32 dither_bayer_map13  : 8;

	//REGISTER saturn_postpipe_reg_23
	UINT32 dither_bayer_map14  : 8;
	UINT32 dither_bayer_map15  : 8;
	UINT32 dither_bayer_map16  : 8;
	UINT32 dither_bayer_map17  : 8;

	//REGISTER saturn_postpipe_reg_24
	UINT32 dither_bayer_map20  : 8;
	UINT32 dither_bayer_map21  : 8;
	UINT32 dither_bayer_map22  : 8;
	UINT32 dither_bayer_map23  : 8;

	//REGISTER saturn_postpipe_reg_25
	UINT32 dither_bayer_map24  : 8;
	UINT32 dither_bayer_map25  : 8;
	UINT32 dither_bayer_map26  : 8;
	UINT32 dither_bayer_map27  : 8;

	//REGISTER saturn_postpipe_reg_26
	UINT32 dither_bayer_map30  : 8;
	UINT32 dither_bayer_map31  : 8;
	UINT32 dither_bayer_map32  : 8;
	UINT32 dither_bayer_map33  : 8;

	//REGISTER saturn_postpipe_reg_27
	UINT32 dither_bayer_map34  : 8;
	UINT32 dither_bayer_map35  : 8;
	UINT32 dither_bayer_map36  : 8;
	UINT32 dither_bayer_map37  : 8;

	//REGISTER saturn_postpipe_reg_28
	UINT32 dither_bayer_map40  : 8;
	UINT32 dither_bayer_map41  : 8;
	UINT32 dither_bayer_map42  : 8;
	UINT32 dither_bayer_map43  : 8;

	//REGISTER saturn_postpipe_reg_29
	UINT32 dither_bayer_map44  : 8;
	UINT32 dither_bayer_map45  : 8;
	UINT32 dither_bayer_map46  : 8;
	UINT32 dither_bayer_map47  : 8;

	//REGISTER saturn_postpipe_reg_30
	UINT32 dither_bayer_map50  : 8;
	UINT32 dither_bayer_map51  : 8;
	UINT32 dither_bayer_map52  : 8;
	UINT32 dither_bayer_map53  : 8;

	//REGISTER saturn_postpipe_reg_31
	UINT32 dither_bayer_map54  : 8;
	UINT32 dither_bayer_map55  : 8;
	UINT32 dither_bayer_map56  : 8;
	UINT32 dither_bayer_map57  : 8;

	//REGISTER saturn_postpipe_reg_32
	UINT32 dither_bayer_map60  : 8;
	UINT32 dither_bayer_map61  : 8;
	UINT32 dither_bayer_map62  : 8;
	UINT32 dither_bayer_map63  : 8;

	//REGISTER saturn_postpipe_reg_33
	UINT32 dither_bayer_map64  : 8;
	UINT32 dither_bayer_map65  : 8;
	UINT32 dither_bayer_map66  : 8;
	UINT32 dither_bayer_map67  : 8;

	//REGISTER saturn_postpipe_reg_34
	UINT32 dither_bayer_map70  : 8;
	UINT32 dither_bayer_map71  : 8;
	UINT32 dither_bayer_map72  : 8;
	UINT32 dither_bayer_map73  : 8;

	//REGISTER saturn_postpipe_reg_35
	UINT32 dither_bayer_map74  : 8;
	UINT32 dither_bayer_map75  : 8;
	UINT32 dither_bayer_map76  : 8;
	UINT32 dither_bayer_map77  : 8;

	//REGISTER saturn_postpipe_reg_36
	UINT32 preproc_loc         : 32;

	//REGISTER saturn_postpipe_reg_37
	UINT32 dither_proc_loc     : 32;

	//REGISTER saturn_postpipe_reg_38
	UINT32 split_location      : 32;

	//REGISTER saturn_postpipe_reg_39
	UINT32 p0_out_location     : 32;

	//REGISTER saturn_postpipe_reg_40
	UINT32 p1_out_location     : 32;

	//REGISTER saturn_postpipe_reg_41
	UINT32 pospq_valid         : 1;
	UINT32 pospq_ready         : 1;
	UINT32 acad_valid          : 1;
	UINT32 acad_ready          : 1;
	UINT32 hsv_valid           : 1;
	UINT32 hsv_ready           : 1;
	UINT32 endmatrix_valid     : 1;
	UINT32 endmatrix_ready     : 1;
	UINT32 to_scl_valid        : 1;
	UINT32 scl_ready           : 1;
	UINT32 lut3d_valid         : 1;
	UINT32 lut3d_ready         : 1;
	UINT32 ee_valid            : 1;
	UINT32 ee_ready            : 1;
	UINT32 usrgamma_valid      : 1;
	UINT32 usrgamma_ready      : 1;
	UINT32 dither_valid        : 1;
	UINT32 dither_ready        : 1;
	UINT32 rc_valid            : 1;
	UINT32 rc_ready            : 1;
	UINT32 split_valid         : 1;
	UINT32 split_ready         : 1;
	UINT32 dsc_p0_valid        : 1;
	UINT32 dsc_p0_ready        : 1;
	UINT32 dsc_p1_valid        : 1;
	UINT32 dsc_p1_ready        : 1;
	UINT32 postpipe_p0_valid   : 1;
	UINT32 postpipe_p0_ready   : 1;
	UINT32 postpipe_p1_valid   : 1;
	UINT32 postpipe_p1_ready   : 1;
	UINT32:2;

	//REGISTER saturn_postpipe_reg_42
	struct {
	UINT32:32;
	} saturn_postpipe_reg_42[3];

	//REGISTER saturn_postpipe_reg_45
	UINT32 force_update_en     : 1;
	UINT32 vsync_update_en     : 1;
	UINT32 shadow_read_en      : 1;
	UINT32:29;

	//REGISTER saturn_postpipe_reg_46
	UINT32 force_update_pulse  : 1;
	UINT32:31;

	//REGISTER saturn_postpipe_reg_47
	UINT32 icg_override        : 1;
	UINT32:31;

	//REGISTER saturn_postpipe_reg_48
	UINT32 trigger             : 1;
	UINT32:31;

	//REGISTER saturn_postpipe_reg_49
	UINT32 trigger2            : 1;
	UINT32:31;

	};

	INT32 value32[50];

} POSTPIPE_REG;

#endif
