/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef RC_REG_H
#define RC_REG_H

typedef union {
	struct {
	//REGISTER saturn_rc_full_reg_0
	UINT32 module_enable          : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_1
	UINT32 m_nImgWidth            : 16;
	UINT32 m_nImgHeight           : 16;

	//REGISTER saturn_rc_full_reg_2
	UINT32 enbuf_cfg_done         : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_3
	struct {
	UINT32:32;
	} saturn_rc_full_reg_3[4];

	//REGISTER saturn_rc_full_reg_7
	UINT32 m_nRCEn_0              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_0          : 16;

	//REGISTER saturn_rc_full_reg_8
	UINT32 m_nRCStartY_0          : 16;
	UINT32 m_nRCWidth_0           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_9
	UINT32 m_nRCHeight_0          : 10;
	UINT32 m_nRCSymmetric0        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX0        : 16;

	//REGISTER saturn_rc_full_reg_10
	UINT32 m_nRCOffset0           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_11
	UINT32 m_nRCEn_1              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_1          : 16;

	//REGISTER saturn_rc_full_reg_12
	UINT32 m_nRCStartY_1          : 16;
	UINT32 m_nRCWidth_1           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_13
	UINT32 m_nRCHeight_1          : 10;
	UINT32 m_nRCSymmetric1        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX1        : 16;

	//REGISTER saturn_rc_full_reg_14
	UINT32 m_nRCOffset1           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_15
	UINT32 m_nRCEn_2              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_2          : 16;

	//REGISTER saturn_rc_full_reg_16
	UINT32 m_nRCStartY_2          : 16;
	UINT32 m_nRCWidth_2           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_17
	UINT32 m_nRCHeight_2          : 10;
	UINT32 m_nRCSymmetric2        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX2        : 16;

	//REGISTER saturn_rc_full_reg_18
	UINT32 m_nRCOffset2           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_19
	UINT32 m_nRCEn_3              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_3          : 16;

	//REGISTER saturn_rc_full_reg_20
	UINT32 m_nRCStartY_3          : 16;
	UINT32 m_nRCWidth_3           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_21
	UINT32 m_nRCHeight_3          : 10;
	UINT32 m_nRCSymmetric3        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX3        : 16;

	//REGISTER saturn_rc_full_reg_22
	UINT32 m_nRCOffset3           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_23
	UINT32 m_nRCEn_4              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_4          : 16;

	//REGISTER saturn_rc_full_reg_24
	UINT32 m_nRCStartY_4          : 16;
	UINT32 m_nRCWidth_4           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_25
	UINT32 m_nRCHeight_4          : 10;
	UINT32 m_nRCSymmetric4        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX4        : 16;

	//REGISTER saturn_rc_full_reg_26
	UINT32 m_nRCOffset4           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_27
	UINT32 m_nRCEn_5              : 1;
	UINT32:15;
	UINT32 m_nRCStartX_5          : 16;

	//REGISTER saturn_rc_full_reg_28
	UINT32 m_nRCStartY_5          : 16;
	UINT32 m_nRCWidth_5           : 10;
	UINT32:6;

	//REGISTER saturn_rc_full_reg_29
	UINT32 m_nRCHeight_5          : 10;
	UINT32 m_nRCSymmetric5        : 1;
	UINT32:5;
	UINT32 m_nRCSymmStarX5        : 16;

	//REGISTER saturn_rc_full_reg_30
	UINT32 m_nRCOffset5           : 16;
	UINT32:16;

	//REGISTER saturn_rc_full_reg_31
	UINT32 cfg_se                 : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_32
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER saturn_rc_full_reg_33
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_34
	UINT32 force_update_en_se     : 1;
	UINT32 vsync_update_en_se     : 1;
	UINT32 shadow_read_en_se      : 1;
	UINT32:29;

	//REGISTER saturn_rc_full_reg_35
	UINT32 force_update_pulse_se  : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_36
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_37
	UINT32 trigger                : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_38
	UINT32 trigger2               : 1;
	UINT32:31;

	//REGISTER saturn_rc_full_reg_39
	struct {
	UINT32 compress_data          : 32;
	} saturn_rc_full_reg_39[4096];

	};

	INT32 value32[4135];

} RC_REG;

#endif
