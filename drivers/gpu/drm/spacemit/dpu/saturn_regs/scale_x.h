/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef SCALE_X_REG_H
#define SCALE_X_REG_H

typedef union {
	struct {
	//REGISTER saturn_scaler_8x4tap_reg_0
	UINT32 m_nscl_hor_enable           : 1;
	UINT32 m_nscl_ver_enable           : 1;
	UINT32 m_nyuv_narrow               : 1;
	UINT32:29;

	//REGISTER saturn_scaler_8x4tap_reg_1
	UINT32 m_nscl_input_width          : 16;
	UINT32 m_nscl_input_height         : 16;

	//REGISTER saturn_scaler_8x4tap_reg_2
	UINT32 m_nscl_output_width         : 16;
	UINT32 m_nscl_output_height        : 16;

	//REGISTER saturn_scaler_8x4tap_reg_3
	UINT32 m_nscl_hor_init_phase_l32b  : 32;

	//REGISTER saturn_scaler_8x4tap_reg_4
	UINT32 m_nscl_hor_init_phase_h1b   : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_5
	UINT32 m_nscl_ver_init_phase_l32b  : 32;

	//REGISTER saturn_scaler_8x4tap_reg_6
	UINT32 m_nscl_ver_init_phase_h1b   : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_7
	UINT32 m_nscl_hor_delta_phase      : 20;
	UINT32:12;

	//REGISTER saturn_scaler_8x4tap_reg_8
	UINT32 m_nscl_ver_delta_phase      : 20;
	UINT32:12;

	//REGISTER saturn_scaler_8x4tap_reg_9
	UINT32 m_nscl_hor_coef0            : 16;
	UINT32 m_nscl_hor_coef1            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_10
	UINT32 m_nscl_hor_coef2            : 16;
	UINT32 m_nscl_hor_coef3            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_11
	UINT32 m_nscl_hor_coef4            : 16;
	UINT32 m_nscl_hor_coef5            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_12
	UINT32 m_nscl_hor_coef6            : 16;
	UINT32 m_nscl_hor_coef7            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_13
	UINT32 m_nscl_hor_coef8            : 16;
	UINT32 m_nscl_hor_coef9            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_14
	UINT32 m_nscl_hor_coef10           : 16;
	UINT32 m_nscl_hor_coef11           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_15
	UINT32 m_nscl_hor_coef12           : 16;
	UINT32 m_nscl_hor_coef13           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_16
	UINT32 m_nscl_hor_coef14           : 16;
	UINT32 m_nscl_hor_coef15           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_17
	UINT32 m_nscl_hor_coef16           : 16;
	UINT32 m_nscl_hor_coef17           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_18
	UINT32 m_nscl_hor_coef18           : 16;
	UINT32 m_nscl_hor_coef19           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_19
	UINT32 m_nscl_hor_coef20           : 16;
	UINT32 m_nscl_hor_coef21           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_20
	UINT32 m_nscl_hor_coef22           : 16;
	UINT32 m_nscl_hor_coef23           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_21
	UINT32 m_nscl_hor_coef24           : 16;
	UINT32 m_nscl_hor_coef25           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_22
	UINT32 m_nscl_hor_coef26           : 16;
	UINT32 m_nscl_hor_coef27           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_23
	UINT32 m_nscl_hor_coef28           : 16;
	UINT32 m_nscl_hor_coef29           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_24
	UINT32 m_nscl_hor_coef30           : 16;
	UINT32 m_nscl_hor_coef31           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_25
	UINT32 m_nscl_hor_coef32           : 16;
	UINT32 m_nscl_hor_coef33           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_26
	UINT32 m_nscl_hor_coef34           : 16;
	UINT32 m_nscl_hor_coef35           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_27
	UINT32 m_nscl_hor_coef36           : 16;
	UINT32 m_nscl_hor_coef37           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_28
	UINT32 m_nscl_hor_coef38           : 16;
	UINT32 m_nscl_hor_coef39           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_29
	UINT32 m_nscl_hor_coef40           : 16;
	UINT32 m_nscl_hor_coef41           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_30
	UINT32 m_nscl_hor_coef42           : 16;
	UINT32 m_nscl_hor_coef43           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_31
	UINT32 m_nscl_hor_coef44           : 16;
	UINT32 m_nscl_hor_coef45           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_32
	UINT32 m_nscl_hor_coef46           : 16;
	UINT32 m_nscl_hor_coef47           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_33
	UINT32 m_nscl_hor_coef48           : 16;
	UINT32 m_nscl_hor_coef49           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_34
	UINT32 m_nscl_hor_coef50           : 16;
	UINT32 m_nscl_hor_coef51           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_35
	UINT32 m_nscl_hor_coef52           : 16;
	UINT32 m_nscl_hor_coef53           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_36
	UINT32 m_nscl_hor_coef54           : 16;
	UINT32 m_nscl_hor_coef55           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_37
	UINT32 m_nscl_hor_coef56           : 16;
	UINT32 m_nscl_hor_coef57           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_38
	UINT32 m_nscl_hor_coef58           : 16;
	UINT32 m_nscl_hor_coef59           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_39
	UINT32 m_nscl_hor_coef60           : 16;
	UINT32 m_nscl_hor_coef61           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_40
	UINT32 m_nscl_hor_coef62           : 16;
	UINT32 m_nscl_hor_coef63           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_41
	UINT32 m_nscl_hor_coef64           : 16;
	UINT32 m_nscl_hor_coef65           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_42
	UINT32 m_nscl_hor_coef66           : 16;
	UINT32 m_nscl_hor_coef67           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_43
	UINT32 m_nscl_hor_coef68           : 16;
	UINT32 m_nscl_hor_coef69           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_44
	UINT32 m_nscl_hor_coef70           : 16;
	UINT32 m_nscl_hor_coef71           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_45
	UINT32 m_nscl_hor_coef72           : 16;
	UINT32 m_nscl_hor_coef73           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_46
	UINT32 m_nscl_hor_coef74           : 16;
	UINT32 m_nscl_hor_coef75           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_47
	UINT32 m_nscl_hor_coef76           : 16;
	UINT32 m_nscl_hor_coef77           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_48
	UINT32 m_nscl_hor_coef78           : 16;
	UINT32 m_nscl_hor_coef79           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_49
	UINT32 m_nscl_hor_coef80           : 16;
	UINT32 m_nscl_hor_coef81           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_50
	UINT32 m_nscl_hor_coef82           : 16;
	UINT32 m_nscl_hor_coef83           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_51
	UINT32 m_nscl_hor_coef84           : 16;
	UINT32 m_nscl_hor_coef85           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_52
	UINT32 m_nscl_hor_coef86           : 16;
	UINT32 m_nscl_hor_coef87           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_53
	UINT32 m_nscl_hor_coef88           : 16;
	UINT32 m_nscl_hor_coef89           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_54
	UINT32 m_nscl_hor_coef90           : 16;
	UINT32 m_nscl_hor_coef91           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_55
	UINT32 m_nscl_hor_coef92           : 16;
	UINT32 m_nscl_hor_coef93           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_56
	UINT32 m_nscl_hor_coef94           : 16;
	UINT32 m_nscl_hor_coef95           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_57
	UINT32 m_nscl_hor_coef96           : 16;
	UINT32 m_nscl_hor_coef97           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_58
	UINT32 m_nscl_hor_coef98           : 16;
	UINT32 m_nscl_hor_coef99           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_59
	UINT32 m_nscl_hor_coef100          : 16;
	UINT32 m_nscl_hor_coef101          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_60
	UINT32 m_nscl_hor_coef102          : 16;
	UINT32 m_nscl_hor_coef103          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_61
	UINT32 m_nscl_hor_coef104          : 16;
	UINT32 m_nscl_hor_coef105          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_62
	UINT32 m_nscl_hor_coef106          : 16;
	UINT32 m_nscl_hor_coef107          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_63
	UINT32 m_nscl_hor_coef108          : 16;
	UINT32 m_nscl_hor_coef109          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_64
	UINT32 m_nscl_hor_coef110          : 16;
	UINT32 m_nscl_hor_coef111          : 16;

	//REGISTER saturn_scaler_8x4tap_reg_65
	UINT32 m_nscl_ver_coef0            : 16;
	UINT32 m_nscl_ver_coef1            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_66
	UINT32 m_nscl_ver_coef2            : 16;
	UINT32 m_nscl_ver_coef3            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_67
	UINT32 m_nscl_ver_coef4            : 16;
	UINT32 m_nscl_ver_coef5            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_68
	UINT32 m_nscl_ver_coef6            : 16;
	UINT32 m_nscl_ver_coef7            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_69
	UINT32 m_nscl_ver_coef8            : 16;
	UINT32 m_nscl_ver_coef9            : 16;

	//REGISTER saturn_scaler_8x4tap_reg_70
	UINT32 m_nscl_ver_coef10           : 16;
	UINT32 m_nscl_ver_coef11           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_71
	UINT32 m_nscl_ver_coef12           : 16;
	UINT32 m_nscl_ver_coef13           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_72
	UINT32 m_nscl_ver_coef14           : 16;
	UINT32 m_nscl_ver_coef15           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_73
	UINT32 m_nscl_ver_coef16           : 16;
	UINT32 m_nscl_ver_coef17           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_74
	UINT32 m_nscl_ver_coef18           : 16;
	UINT32 m_nscl_ver_coef19           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_75
	UINT32 m_nscl_ver_coef20           : 16;
	UINT32 m_nscl_ver_coef21           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_76
	UINT32 m_nscl_ver_coef22           : 16;
	UINT32 m_nscl_ver_coef23           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_77
	UINT32 m_nscl_ver_coef24           : 16;
	UINT32 m_nscl_ver_coef25           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_78
	UINT32 m_nscl_ver_coef26           : 16;
	UINT32 m_nscl_ver_coef27           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_79
	UINT32 m_nscl_ver_coef28           : 16;
	UINT32 m_nscl_ver_coef29           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_80
	UINT32 m_nscl_ver_coef30           : 16;
	UINT32 m_nscl_ver_coef31           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_81
	UINT32 m_nscl_ver_coef32           : 16;
	UINT32 m_nscl_ver_coef33           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_82
	UINT32 m_nscl_ver_coef34           : 16;
	UINT32 m_nscl_ver_coef35           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_83
	UINT32 m_nscl_ver_coef36           : 16;
	UINT32 m_nscl_ver_coef37           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_84
	UINT32 m_nscl_ver_coef38           : 16;
	UINT32 m_nscl_ver_coef39           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_85
	UINT32 m_nscl_ver_coef40           : 16;
	UINT32 m_nscl_ver_coef41           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_86
	UINT32 m_nscl_ver_coef42           : 16;
	UINT32 m_nscl_ver_coef43           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_87
	UINT32 m_nscl_ver_coef44           : 16;
	UINT32 m_nscl_ver_coef45           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_88
	UINT32 m_nscl_ver_coef46           : 16;
	UINT32 m_nscl_ver_coef47           : 16;

	//REGISTER saturn_scaler_8x4tap_reg_89
	struct {
	UINT32:32;
	} saturn_scaler_8x4tap_reg_89[32];

	//REGISTER saturn_scaler_8x4tap_reg_121
	UINT32 cfg_se                      : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_122
	UINT32 force_update_en             : 1;
	UINT32 vsync_update_en             : 1;
	UINT32 shadow_read_en              : 1;
	UINT32:29;

	//REGISTER saturn_scaler_8x4tap_reg_123
	UINT32 force_update_pulse          : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_124
	UINT32 force_update_en_se          : 1;
	UINT32 vsync_update_en_se          : 1;
	UINT32 shadow_read_en_se           : 1;
	UINT32:29;

	//REGISTER saturn_scaler_8x4tap_reg_125
	UINT32 force_update_pulse_se       : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_126
	UINT32 icg_override                : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_127
	UINT32 trigger                     : 1;
	UINT32:31;

	//REGISTER saturn_scaler_8x4tap_reg_128
	UINT32 trigger2                    : 1;
	UINT32:31;

	} b;

	INT32 value32[129];

	struct {
		UINT32 saturn_scaler_reg_0;
		UINT32 saturn_scaler_reg_1;
		UINT32 saturn_scaler_reg_2;
		UINT32 saturn_scaler_reg_3;
		UINT32 saturn_scaler_reg_4;
		UINT32 saturn_scaler_reg_5;
		UINT32 saturn_scaler_reg_6;
		UINT32 saturn_scaler_reg_7;
		UINT32 saturn_scaler_reg_8;
		UINT32 saturn_scaler_reg_9;
		UINT32 saturn_scaler_reg_10;
		UINT32 saturn_scaler_reg_11;
		UINT32 saturn_scaler_reg_12;
		UINT32 saturn_scaler_reg_13;
		UINT32 saturn_scaler_reg_14;
		UINT32 saturn_scaler_reg_15;
		UINT32 saturn_scaler_reg_16;
		UINT32 saturn_scaler_reg_17;
		UINT32 saturn_scaler_reg_18;
		UINT32 saturn_scaler_reg_19;
		UINT32 saturn_scaler_reg_20;
		UINT32 saturn_scaler_reg_21;
		UINT32 saturn_scaler_reg_22;
		UINT32 saturn_scaler_reg_23;
		UINT32 saturn_scaler_reg_24;
		UINT32 saturn_scaler_reg_25;
		UINT32 saturn_scaler_reg_26;
		UINT32 saturn_scaler_reg_27;
		UINT32 saturn_scaler_reg_28;
		UINT32 saturn_scaler_reg_29;
		UINT32 saturn_scaler_reg_30;
		UINT32 saturn_scaler_reg_31;
		UINT32 saturn_scaler_reg_32;
		UINT32 saturn_scaler_reg_33;
		UINT32 saturn_scaler_reg_34;
		UINT32 saturn_scaler_reg_35;
		UINT32 saturn_scaler_reg_36;
		UINT32 saturn_scaler_reg_37;
		UINT32 saturn_scaler_reg_38;
		UINT32 saturn_scaler_reg_39;
		UINT32 saturn_scaler_reg_40;
		UINT32 saturn_scaler_reg_41;
		UINT32 saturn_scaler_reg_42;
		UINT32 saturn_scaler_reg_43;
		UINT32 saturn_scaler_reg_44;
		UINT32 saturn_scaler_reg_45;
		UINT32 saturn_scaler_reg_46;
		UINT32 saturn_scaler_reg_47;
		UINT32 saturn_scaler_reg_48;
		UINT32 saturn_scaler_reg_49;
		UINT32 saturn_scaler_reg_50;
		UINT32 saturn_scaler_reg_51;
		UINT32 saturn_scaler_reg_52;
		UINT32 saturn_scaler_reg_53;
		UINT32 saturn_scaler_reg_54;
		UINT32 saturn_scaler_reg_55;
		UINT32 saturn_scaler_reg_56;
		UINT32 saturn_scaler_reg_57;
		UINT32 saturn_scaler_reg_58;
		UINT32 saturn_scaler_reg_59;
		UINT32 saturn_scaler_reg_60;
		UINT32 saturn_scaler_reg_61;
		UINT32 saturn_scaler_reg_62;
		UINT32 saturn_scaler_reg_63;
		UINT32 saturn_scaler_reg_64;
		UINT32 saturn_scaler_reg_65;
		UINT32 saturn_scaler_reg_66;
		UINT32 saturn_scaler_reg_67;
		UINT32 saturn_scaler_reg_68;
		UINT32 saturn_scaler_reg_69;
		UINT32 saturn_scaler_reg_70;
		UINT32 saturn_scaler_reg_71;
		UINT32 saturn_scaler_reg_72;
		UINT32 saturn_scaler_reg_73;
		UINT32 saturn_scaler_reg_74;
		UINT32 saturn_scaler_reg_75;
		UINT32 saturn_scaler_reg_76;
		UINT32 saturn_scaler_reg_77;
		UINT32 saturn_scaler_reg_78;
		UINT32 saturn_scaler_reg_79;
		UINT32 saturn_scaler_reg_80;
		UINT32 saturn_scaler_reg_81;
		UINT32 saturn_scaler_reg_82;
		UINT32 saturn_scaler_reg_83;
		UINT32 saturn_scaler_reg_84;
		UINT32 saturn_scaler_reg_85;
		UINT32 saturn_scaler_reg_86;
		UINT32 saturn_scaler_reg_87;
		UINT32 saturn_scaler_reg_88;
		UINT32 saturn_scaler_reg_89;
		UINT32 saturn_scaler_reg_90;
		UINT32 saturn_scaler_reg_91;
		UINT32 saturn_scaler_reg_92;
		UINT32 saturn_scaler_reg_93;
		UINT32 saturn_scaler_reg_94;
		UINT32 saturn_scaler_reg_95;
		UINT32 saturn_scaler_reg_96;
		UINT32 saturn_scaler_reg_97;
		UINT32 saturn_scaler_reg_98;
		UINT32 saturn_scaler_reg_99;
		UINT32 saturn_scaler_reg_100;
		UINT32 saturn_scaler_reg_101;
		UINT32 saturn_scaler_reg_102;
		UINT32 saturn_scaler_reg_103;
		UINT32 saturn_scaler_reg_104;
		UINT32 saturn_scaler_reg_105;
		UINT32 saturn_scaler_reg_106;
		UINT32 saturn_scaler_reg_107;
		UINT32 saturn_scaler_reg_108;
		UINT32 saturn_scaler_reg_109;
		UINT32 saturn_scaler_reg_110;
		UINT32 saturn_scaler_reg_111;
		UINT32 saturn_scaler_reg_112;
		UINT32 saturn_scaler_reg_113;
		UINT32 saturn_scaler_reg_114;
		UINT32 saturn_scaler_reg_115;
		UINT32 saturn_scaler_reg_116;
		UINT32 saturn_scaler_reg_117;
		UINT32 saturn_scaler_reg_118;
		UINT32 saturn_scaler_reg_119;
		UINT32 saturn_scaler_reg_120;
		UINT32 saturn_scaler_reg_121;
		UINT32 saturn_scaler_reg_122;
		UINT32 saturn_scaler_reg_123;
		UINT32 saturn_scaler_reg_124;
		UINT32 saturn_scaler_reg_125;
		UINT32 saturn_scaler_reg_126;
		UINT32 saturn_scaler_reg_127;
		UINT32 saturn_scaler_reg_128;
	} v;

} SCALE_X_REG;

#endif

typedef SCALE_X_REG SCALER_X_REG;
