/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef EE_REG_H
#define EE_REG_H

typedef union {
	struct {
	//REGISTER saturn_ee_reg_0
	UINT32 m_benable              : 1;
	UINT32:31;

	//REGISTER saturn_ee_reg_1
	UINT32 m_plpf1_0              : 8;
	UINT32 m_plpf1_1              : 8;
	UINT32 m_plpf2_0              : 8;
	UINT32 m_plpf2_1              : 8;

	//REGISTER saturn_ee_reg_2
	UINT32 m_nfreq_exp_level1     : 6;
	UINT32:2;
	UINT32 m_nfreq_exp_level2     : 6;
	UINT32:2;
	UINT32 m_nnode_freq           : 5;
	UINT32:11;

	//REGISTER saturn_ee_reg_3
	UINT32 m_nsingular_p1         : 13;
	UINT32:3;
	UINT32 m_nsingular_p2         : 13;
	UINT32:3;

	//REGISTER saturn_ee_reg_4
	UINT32 m_pcurve_freq_tx_p_0   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_1   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_2   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_3   : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_5
	UINT32 m_pcurve_freq_tx_p_4   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_5   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_6   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_7   : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_6
	UINT32 m_pcurve_freq_tx_p_8   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_9   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_10  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_11  : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_7
	UINT32 m_pcurve_freq_tx_p_12  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_13  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_14  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_p_15  : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_8
	UINT32 m_pcurve_freq_tx_p_16  : 5;
	UINT32:27;

	//REGISTER saturn_ee_reg_9
	UINT32 m_pcurve_freq_tx_n_0   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_1   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_2   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_3   : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_10
	UINT32 m_pcurve_freq_tx_n_4   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_5   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_6   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_7   : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_11
	UINT32 m_pcurve_freq_tx_n_8   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_9   : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_10  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_11  : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_12
	UINT32 m_pcurve_freq_tx_n_12  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_13  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_14  : 5;
	UINT32:3;
	UINT32 m_pcurve_freq_tx_n_15  : 5;
	UINT32:3;

	//REGISTER saturn_ee_reg_13
	UINT32 m_pcurve_freq_tx_n_16  : 5;
	UINT32:27;

	//REGISTER saturn_ee_reg_14
	UINT32 m_nfreq_offset_tx      : 8;
	UINT32:24;

	//REGISTER saturn_ee_reg_15
	UINT32 m_pcurve_luma_p_0      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_1      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_2      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_3      : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_16
	UINT32 m_pcurve_luma_p_4      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_5      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_6      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_7      : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_17
	UINT32 m_pcurve_luma_p_8      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_9      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_10     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_11     : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_18
	UINT32 m_pcurve_luma_p_12     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_13     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_14     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_p_15     : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_19
	UINT32 m_pcurve_luma_p_16     : 7;
	UINT32:25;

	//REGISTER saturn_ee_reg_20
	UINT32 m_pcurve_luma_n_0      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_1      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_2      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_3      : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_21
	UINT32 m_pcurve_luma_n_4      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_5      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_6      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_7      : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_22
	UINT32 m_pcurve_luma_n_8      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_9      : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_10     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_11     : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_23
	UINT32 m_pcurve_luma_n_12     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_13     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_14     : 7;
	UINT32:1;
	UINT32 m_pcurve_luma_n_15     : 7;
	UINT32:1;

	//REGISTER saturn_ee_reg_24
	UINT32 m_pcurve_luma_n_16     : 7;
	UINT32:25;

	//REGISTER saturn_ee_reg_25
	UINT32 m_ntx_clip_p12         : 13;
	UINT32:3;
	UINT32 m_ntx_clip_p10         : 13;
	UINT32:3;

	//REGISTER saturn_ee_reg_26
	UINT32 m_ntx_clip_n12         : 13;
	UINT32:3;
	UINT32 m_ntx_clip_n10         : 13;
	UINT32:3;

	//REGISTER saturn_ee_reg_27
	UINT32 m_ntx_thrd_p12         : 9;
	UINT32:7;
	UINT32 m_ntx_thrd_p10         : 9;
	UINT32:7;

	//REGISTER saturn_ee_reg_28
	UINT32 m_ntx_thrd_n12         : 9;
	UINT32:7;
	UINT32 m_ntx_thrd_n10         : 9;
	UINT32:7;

	//REGISTER saturn_ee_reg_29
	UINT32 m_nclip_pos12          : 12;
	UINT32:4;
	UINT32 m_nclip_pos10          : 12;
	UINT32:4;

	//REGISTER saturn_ee_reg_30
	UINT32 m_nclip_neg12          : 12;
	UINT32:4;
	UINT32 m_nclip_neg10          : 12;
	UINT32:4;

	//REGISTER saturn_ee_reg_31
	UINT32 m_nhc_strength         : 6;
	UINT32:2;
	UINT32 m_pcoff_w_0            : 8;
	UINT32 m_pcoff_w_1            : 8;
	UINT32:8;

	//REGISTER saturn_ee_reg_32
	UINT32 m_ncoff_w_curve        : 11;
	UINT32:21;

	//REGISTER saturn_ee_reg_33
	UINT32 rgb2yuv_matrix00       : 16;
	UINT32 rgb2yuv_matrix01       : 16;

	//REGISTER saturn_ee_reg_34
	UINT32 rgb2yuv_matrix02       : 16;
	UINT32 rgb2yuv_matrix03       : 16;

	//REGISTER saturn_ee_reg_35
	UINT32 rgb2yuv_matrix10       : 16;
	UINT32 rgb2yuv_matrix11       : 16;

	//REGISTER saturn_ee_reg_36
	UINT32 rgb2yuv_matrix12       : 16;
	UINT32 rgb2yuv_matrix13       : 16;

	//REGISTER saturn_ee_reg_37
	UINT32 rgb2yuv_matrix20       : 16;
	UINT32 rgb2yuv_matrix21       : 16;

	//REGISTER saturn_ee_reg_38
	UINT32 rgb2yuv_matrix22       : 16;
	UINT32 rgb2yuv_matrix23       : 16;

	//REGISTER saturn_ee_reg_39
	UINT32 yuv2rgb_matrix00       : 16;
	UINT32 yuv2rgb_matrix01       : 16;

	//REGISTER saturn_ee_reg_40
	UINT32 yuv2rgb_matrix02       : 16;
	UINT32 yuv2rgb_matrix03       : 16;

	//REGISTER saturn_ee_reg_41
	UINT32 yuv2rgb_matrix10       : 16;
	UINT32 yuv2rgb_matrix11       : 16;

	//REGISTER saturn_ee_reg_42
	UINT32 yuv2rgb_matrix12       : 16;
	UINT32 yuv2rgb_matrix13       : 16;

	//REGISTER saturn_ee_reg_43
	UINT32 yuv2rgb_matrix20       : 16;
	UINT32 yuv2rgb_matrix21       : 16;

	//REGISTER saturn_ee_reg_44
	UINT32 yuv2rgb_matrix22       : 16;
	UINT32 yuv2rgb_matrix23       : 16;

	//REGISTER saturn_ee_reg_45
	UINT32 cfg_se                 : 1;
	UINT32:31;

	//REGISTER saturn_ee_reg_46
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER saturn_ee_reg_47
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER saturn_ee_reg_48
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER saturn_ee_reg_49
	UINT32 trigger                : 1;
	UINT32:31;

	//REGISTER saturn_ee_reg_50
	UINT32 trigger2               : 1;
	UINT32:31;

	};

	INT32 value32[51];

} EE_REG;

#endif

typedef EE_REG EE_REG;
