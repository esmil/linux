/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef LUT_3D_REG_H
#define LUT_3D_REG_H

typedef union {
	struct {
	//REGISTER Lut_3d_reg_0
	UINT32 lut_3d_en        : 1;
	UINT32:31;

	//REGISTER Lut_3d_reg_1
	UINT32 lut_3d_cfg_done  : 1;
	UINT32:31;

	//REGISTER Lut_3d_reg_2
	UINT32 m_pLut0_r        : 11;
	UINT32:5;
	UINT32 m_pLut0_g        : 11;
	UINT32:5;

	//REGISTER Lut_3d_reg_3
	UINT32 m_pLut0_b        : 11;
	UINT32:5;
	UINT32 m_pLut1_r        : 11;
	UINT32:5;

	//REGISTER Lut_3d_reg_4
	UINT32 m_pLut1_g        : 11;
	UINT32:5;
	UINT32 m_pLut1_b        : 11;
	UINT32:5;

	//REGISTER lut_3d_reg_5to7804
	struct {
	UINT32 m_pLut2_r        : 11;
	UINT32:5;
	UINT32 m_pLut2_g        : 11;
	UINT32:5;
	} lut_3d_reg_5to7804[7800];

	};

	INT32 value32[7805];

} LUT_3D_REG;

#endif

typedef LUT_3D_REG LUT_3D_REG;
