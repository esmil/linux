/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef PREPIPE_X_REG_H
#define PREPIPE_X_REG_H

typedef union {
	struct {
	//REGISTER saturn_prepipe_top_reg_0
	UINT32:5;
	UINT32 m_ncolor_key_en        : 1;
	UINT32:26;

	//REGISTER saturn_prepipe_top_reg_1
	struct {
	UINT32:32;
	} saturn_prepipe_top_reg_1[76];

	//REGISTER saturn_prepipe_top_reg_77
	UINT32:16;
	UINT32 m_pcolor_key_R_thr0    : 10;
	UINT32:6;

	//REGISTER saturn_prepipe_top_reg_78
	UINT32 m_pcolor_key_R_thr1    : 10;
	UINT32:6;
	UINT32 m_pcolor_key_G_thr0    : 10;
	UINT32:6;

	//REGISTER saturn_prepipe_top_reg_79
	UINT32 m_pcolor_key_G_thr1    : 10;
	UINT32:6;
	UINT32 m_pcolor_key_B_thr0    : 10;
	UINT32:6;

	//REGISTER saturn_prepipe_top_reg_80
	UINT32 m_pcolor_key_B_thr1    : 10;
	UINT32:22;

	//REGISTER saturn_prepipe_top_reg_81
	UINT32 prepq_secu_en          : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_82
	UINT32 DITHER_EN              : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_83
	UINT32 DITHER_CG2_AUTO_EN     : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_84
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER saturn_prepipe_top_reg_85
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_86
	UINT32 force_update_en_se     : 1;
	UINT32 vsync_update_en_se     : 1;
	UINT32 shadow_read_en_se      : 1;
	UINT32:29;

	//REGISTER saturn_prepipe_top_reg_87
	UINT32 force_update_pulse_se  : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_88
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_89
	UINT32 trigger                : 1;
	UINT32:31;

	//REGISTER saturn_prepipe_top_reg_90
	UINT32 trigger2               : 1;
	UINT32:31;

	};

	INT32 value32[91];

} PREPIPE_X_REG;

#endif
