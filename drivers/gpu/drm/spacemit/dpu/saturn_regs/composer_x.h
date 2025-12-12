/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef COMPOSER_X_REG_H
#define COMPOSER_X_REG_H

typedef union {
	struct {
	//REGISTER saturn_cmps_reg_0
	UINT32 module_enable            : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_1
	UINT32 dst_w                    : 16;
	UINT32 dst_h                    : 16;

	//REGISTER saturn_cmps_reg_2
	UINT32 bg_color_r               : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_3
	UINT32 bg_color_g               : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_4
	UINT32 bg_color_b               : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_5
	UINT32 bg_color_a               : 8;
	UINT32:24;

	//REGISTER saturn_cmps_reg_6
	struct {
	UINT32:32;
	} saturn_cmps_reg_6[2];

	//REGISTER saturn_cmps_reg_8
	UINT32 layer00_area_left        : 16;
	UINT32 layer00_area_right       : 16;

	//REGISTER saturn_cmps_reg_9
	UINT32 layer00_area_top         : 16;
	UINT32 layer00_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_10
	UINT32 layer00_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_11
	UINT32 layer00_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_12
	UINT32 layer00_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_13
	UINT32 layer00_solid_color_a    : 8;
	UINT32 layer00_rsv00            : 1;
	UINT32 layer00_rsv01            : 1;
	UINT32 layer00_rsv02            : 1;
	UINT32 layer00_rsv03            : 1;
	UINT32 layer00_rsv04            : 1;
	UINT32 layer00_rsv05            : 1;
	UINT32 layer00_blend_mode       : 2;
	UINT32 layer00_solid_area_mode  : 1;
	UINT32 layer00_spe_alpha_mode   : 1;
	UINT32 layer00_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_14
	UINT32 layer00_en               : 1;
	UINT32 layer00_blend_sel        : 2;
	UINT32 layer00_solid_en         : 1;
	UINT32 layer00_dma_id           : 4;
	UINT32 layer00_layer_alpha      : 8;
	UINT32 layer00_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_15
	UINT32 layer01_area_left        : 16;
	UINT32 layer01_area_right       : 16;

	//REGISTER saturn_cmps_reg_16
	UINT32 layer01_area_top         : 16;
	UINT32 layer01_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_17
	UINT32 layer01_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_18
	UINT32 layer01_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_19
	UINT32 layer01_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_20
	UINT32 layer01_solid_color_a    : 8;
	UINT32 layer01_rsv00            : 1;
	UINT32 layer01_rsv01            : 1;
	UINT32 layer01_rsv02            : 1;
	UINT32 layer01_rsv03            : 1;
	UINT32 layer01_rsv04            : 1;
	UINT32 layer01_rsv05            : 1;
	UINT32 layer01_blend_mode       : 2;
	UINT32 layer01_solid_area_mode  : 1;
	UINT32 layer01_spe_alpha_mode   : 1;
	UINT32 layer01_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_21
	UINT32 layer01_en               : 1;
	UINT32 layer01_blend_sel        : 2;
	UINT32 layer01_solid_en         : 1;
	UINT32 layer01_dma_id           : 4;
	UINT32 layer01_layer_alpha      : 8;
	UINT32 layer01_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_22
	UINT32 layer02_area_left        : 16;
	UINT32 layer02_area_right       : 16;

	//REGISTER saturn_cmps_reg_23
	UINT32 layer02_area_top         : 16;
	UINT32 layer02_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_24
	UINT32 layer02_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_25
	UINT32 layer02_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_26
	UINT32 layer02_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_27
	UINT32 layer02_solid_color_a    : 8;
	UINT32 layer02_rsv00            : 1;
	UINT32 layer02_rsv01            : 1;
	UINT32 layer02_rsv02            : 1;
	UINT32 layer02_rsv03            : 1;
	UINT32 layer02_rsv04            : 1;
	UINT32 layer02_rsv05            : 1;
	UINT32 layer02_blend_mode       : 2;
	UINT32 layer02_solid_area_mode  : 1;
	UINT32 layer02_spe_alpha_mode   : 1;
	UINT32 layer02_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_28
	UINT32 layer02_en               : 1;
	UINT32 layer02_blend_sel        : 2;
	UINT32 layer02_solid_en         : 1;
	UINT32 layer02_dma_id           : 4;
	UINT32 layer02_layer_alpha      : 8;
	UINT32 layer02_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_29
	UINT32 layer03_area_left        : 16;
	UINT32 layer03_area_right       : 16;

	//REGISTER saturn_cmps_reg_30
	UINT32 layer03_area_top         : 16;
	UINT32 layer03_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_31
	UINT32 layer03_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_32
	UINT32 layer03_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_33
	UINT32 layer03_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_34
	UINT32 layer03_solid_color_a    : 8;
	UINT32 layer03_rsv00            : 1;
	UINT32 layer03_rsv01            : 1;
	UINT32 layer03_rsv02            : 1;
	UINT32 layer03_rsv03            : 1;
	UINT32 layer03_rsv04            : 1;
	UINT32 layer03_rsv05            : 1;
	UINT32 layer03_blend_mode       : 2;
	UINT32 layer03_solid_area_mode  : 1;
	UINT32 layer03_spe_alpha_mode   : 1;
	UINT32 layer03_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_35
	UINT32 layer03_en               : 1;
	UINT32 layer03_blend_sel        : 2;
	UINT32 layer03_solid_en         : 1;
	UINT32 layer03_dma_id           : 4;
	UINT32 layer03_layer_alpha      : 8;
	UINT32 layer03_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_36
	UINT32 layer04_area_left        : 16;
	UINT32 layer04_area_right       : 16;

	//REGISTER saturn_cmps_reg_37
	UINT32 layer04_area_top         : 16;
	UINT32 layer04_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_38
	UINT32 layer04_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_39
	UINT32 layer04_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_40
	UINT32 layer04_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_41
	UINT32 layer04_solid_color_a    : 8;
	UINT32 layer04_rsv00            : 1;
	UINT32 layer04_rsv01            : 1;
	UINT32 layer04_rsv02            : 1;
	UINT32 layer04_rsv03            : 1;
	UINT32 layer04_rsv04            : 1;
	UINT32 layer04_rsv05            : 1;
	UINT32 layer04_blend_mode       : 2;
	UINT32 layer04_solid_area_mode  : 1;
	UINT32 layer04_spe_alpha_mode   : 1;
	UINT32 layer04_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_42
	UINT32 layer04_en               : 1;
	UINT32 layer04_blend_sel        : 2;
	UINT32 layer04_solid_en         : 1;
	UINT32 layer04_dma_id           : 4;
	UINT32 layer04_layer_alpha      : 8;
	UINT32 layer04_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_43
	UINT32 layer05_area_left        : 16;
	UINT32 layer05_area_right       : 16;

	//REGISTER saturn_cmps_reg_44
	UINT32 layer05_area_top         : 16;
	UINT32 layer05_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_45
	UINT32 layer05_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_46
	UINT32 layer05_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_47
	UINT32 layer05_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_48
	UINT32 layer05_solid_color_a    : 8;
	UINT32 layer05_rsv00            : 1;
	UINT32 layer05_rsv01            : 1;
	UINT32 layer05_rsv02            : 1;
	UINT32 layer05_rsv03            : 1;
	UINT32 layer05_rsv04            : 1;
	UINT32 layer05_rsv05            : 1;
	UINT32 layer05_blend_mode       : 2;
	UINT32 layer05_solid_area_mode  : 1;
	UINT32 layer05_spe_alpha_mode   : 1;
	UINT32 layer05_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_49
	UINT32 layer05_en               : 1;
	UINT32 layer05_blend_sel        : 2;
	UINT32 layer05_solid_en         : 1;
	UINT32 layer05_dma_id           : 4;
	UINT32 layer05_layer_alpha      : 8;
	UINT32 layer05_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_50
	UINT32 layer06_area_left        : 16;
	UINT32 layer06_area_right       : 16;

	//REGISTER saturn_cmps_reg_51
	UINT32 layer06_area_top         : 16;
	UINT32 layer06_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_52
	UINT32 layer06_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_53
	UINT32 layer06_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_54
	UINT32 layer06_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_55
	UINT32 layer06_solid_color_a    : 8;
	UINT32 layer06_rsv00            : 1;
	UINT32 layer06_rsv01            : 1;
	UINT32 layer06_rsv02            : 1;
	UINT32 layer06_rsv03            : 1;
	UINT32 layer06_rsv04            : 1;
	UINT32 layer06_rsv05            : 1;
	UINT32 layer06_blend_mode       : 2;
	UINT32 layer06_solid_area_mode  : 1;
	UINT32 layer06_spe_alpha_mode   : 1;
	UINT32 layer06_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_56
	UINT32 layer06_en               : 1;
	UINT32 layer06_blend_sel        : 2;
	UINT32 layer06_solid_en         : 1;
	UINT32 layer06_dma_id           : 4;
	UINT32 layer06_layer_alpha      : 8;
	UINT32 layer06_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_57
	UINT32 layer07_area_left        : 16;
	UINT32 layer07_area_right       : 16;

	//REGISTER saturn_cmps_reg_58
	UINT32 layer07_area_top         : 16;
	UINT32 layer07_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_59
	UINT32 layer07_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_60
	UINT32 layer07_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_61
	UINT32 layer07_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_62
	UINT32 layer07_solid_color_a    : 8;
	UINT32 layer07_rsv00            : 1;
	UINT32 layer07_rsv01            : 1;
	UINT32 layer07_rsv02            : 1;
	UINT32 layer07_rsv03            : 1;
	UINT32 layer07_rsv04            : 1;
	UINT32 layer07_rsv05            : 1;
	UINT32 layer07_blend_mode       : 2;
	UINT32 layer07_solid_area_mode  : 1;
	UINT32 layer07_spe_alpha_mode   : 1;
	UINT32 layer07_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_63
	UINT32 layer07_en               : 1;
	UINT32 layer07_blend_sel        : 2;
	UINT32 layer07_solid_en         : 1;
	UINT32 layer07_dma_id           : 4;
	UINT32 layer07_layer_alpha      : 8;
	UINT32 layer07_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_64
	UINT32 layer08_area_left        : 16;
	UINT32 layer08_area_right       : 16;

	//REGISTER saturn_cmps_reg_65
	UINT32 layer08_area_top         : 16;
	UINT32 layer08_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_66
	UINT32 layer08_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_67
	UINT32 layer08_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_68
	UINT32 layer08_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_69
	UINT32 layer08_solid_color_a    : 8;
	UINT32 layer08_rsv00            : 1;
	UINT32 layer08_rsv01            : 1;
	UINT32 layer08_rsv02            : 1;
	UINT32 layer08_rsv03            : 1;
	UINT32 layer08_rsv04            : 1;
	UINT32 layer08_rsv05            : 1;
	UINT32 layer08_blend_mode       : 2;
	UINT32 layer08_solid_area_mode  : 1;
	UINT32 layer08_spe_alpha_mode   : 1;
	UINT32 layer08_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_70
	UINT32 layer08_en               : 1;
	UINT32 layer08_blend_sel        : 2;
	UINT32 layer08_solid_en         : 1;
	UINT32 layer08_dma_id           : 4;
	UINT32 layer08_layer_alpha      : 8;
	UINT32 layer08_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_71
	UINT32 layer09_area_left        : 16;
	UINT32 layer09_area_right       : 16;

	//REGISTER saturn_cmps_reg_72
	UINT32 layer09_area_top         : 16;
	UINT32 layer09_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_73
	UINT32 layer09_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_74
	UINT32 layer09_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_75
	UINT32 layer09_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_76
	UINT32 layer09_solid_color_a    : 8;
	UINT32 layer09_rsv00            : 1;
	UINT32 layer09_rsv01            : 1;
	UINT32 layer09_rsv02            : 1;
	UINT32 layer09_rsv03            : 1;
	UINT32 layer09_rsv04            : 1;
	UINT32 layer09_rsv05            : 1;
	UINT32 layer09_blend_mode       : 2;
	UINT32 layer09_solid_area_mode  : 1;
	UINT32 layer09_spe_alpha_mode   : 1;
	UINT32 layer09_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_77
	UINT32 layer09_en               : 1;
	UINT32 layer09_blend_sel        : 2;
	UINT32 layer09_solid_en         : 1;
	UINT32 layer09_dma_id           : 4;
	UINT32 layer09_layer_alpha      : 8;
	UINT32 layer09_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_78
	UINT32 layer10_area_left        : 16;
	UINT32 layer10_area_right       : 16;

	//REGISTER saturn_cmps_reg_79
	UINT32 layer10_area_top         : 16;
	UINT32 layer10_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_80
	UINT32 layer10_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_81
	UINT32 layer10_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_82
	UINT32 layer10_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_83
	UINT32 layer10_solid_color_a    : 8;
	UINT32 layer10_rsv00            : 1;
	UINT32 layer10_rsv01            : 1;
	UINT32 layer10_rsv02            : 1;
	UINT32 layer10_rsv03            : 1;
	UINT32 layer10_rsv04            : 1;
	UINT32 layer10_rsv05            : 1;
	UINT32 layer10_blend_mode       : 2;
	UINT32 layer10_solid_area_mode  : 1;
	UINT32 layer10_spe_alpha_mode   : 1;
	UINT32 layer10_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_84
	UINT32 layer10_en               : 1;
	UINT32 layer10_blend_sel        : 2;
	UINT32 layer10_solid_en         : 1;
	UINT32 layer10_dma_id           : 4;
	UINT32 layer10_layer_alpha      : 8;
	UINT32 layer10_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_85
	UINT32 layer11_area_left        : 16;
	UINT32 layer11_area_right       : 16;

	//REGISTER saturn_cmps_reg_86
	UINT32 layer11_area_top         : 16;
	UINT32 layer11_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_87
	UINT32 layer11_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_88
	UINT32 layer11_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_89
	UINT32 layer11_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_90
	UINT32 layer11_solid_color_a    : 8;
	UINT32 layer11_rsv00            : 1;
	UINT32 layer11_rsv01            : 1;
	UINT32 layer11_rsv02            : 1;
	UINT32 layer11_rsv03            : 1;
	UINT32 layer11_rsv04            : 1;
	UINT32 layer11_rsv05            : 1;
	UINT32 layer11_blend_mode       : 2;
	UINT32 layer11_solid_area_mode  : 1;
	UINT32 layer11_spe_alpha_mode   : 1;
	UINT32 layer11_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_91
	UINT32 layer11_en               : 1;
	UINT32 layer11_blend_sel        : 2;
	UINT32 layer11_solid_en         : 1;
	UINT32 layer11_dma_id           : 4;
	UINT32 layer11_layer_alpha      : 8;
	UINT32 layer11_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_92
	UINT32 layer12_area_left        : 16;
	UINT32 layer12_area_right       : 16;

	//REGISTER saturn_cmps_reg_93
	UINT32 layer12_area_top         : 16;
	UINT32 layer12_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_94
	UINT32 layer12_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_95
	UINT32 layer12_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_96
	UINT32 layer12_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_97
	UINT32 layer12_solid_color_a    : 8;
	UINT32 layer12_rsv00            : 1;
	UINT32 layer12_rsv01            : 1;
	UINT32 layer12_rsv02            : 1;
	UINT32 layer12_rsv03            : 1;
	UINT32 layer12_rsv04            : 1;
	UINT32 layer12_rsv05            : 1;
	UINT32 layer12_blend_mode       : 2;
	UINT32 layer12_solid_area_mode  : 1;
	UINT32 layer12_spe_alpha_mode   : 1;
	UINT32 layer12_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_98
	UINT32 layer12_en               : 1;
	UINT32 layer12_blend_sel        : 2;
	UINT32 layer12_solid_en         : 1;
	UINT32 layer12_dma_id           : 4;
	UINT32 layer12_layer_alpha      : 8;
	UINT32 layer12_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_99
	UINT32 layer13_area_left        : 16;
	UINT32 layer13_area_right       : 16;

	//REGISTER saturn_cmps_reg_100
	UINT32 layer13_area_top         : 16;
	UINT32 layer13_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_101
	UINT32 layer13_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_102
	UINT32 layer13_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_103
	UINT32 layer13_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_104
	UINT32 layer13_solid_color_a    : 8;
	UINT32 layer13_rsv00            : 1;
	UINT32 layer13_rsv01            : 1;
	UINT32 layer13_rsv02            : 1;
	UINT32 layer13_rsv03            : 1;
	UINT32 layer13_rsv04            : 1;
	UINT32 layer13_rsv05            : 1;
	UINT32 layer13_blend_mode       : 2;
	UINT32 layer13_solid_area_mode  : 1;
	UINT32 layer13_spe_alpha_mode   : 1;
	UINT32 layer13_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_105
	UINT32 layer13_en               : 1;
	UINT32 layer13_blend_sel        : 2;
	UINT32 layer13_solid_en         : 1;
	UINT32 layer13_dma_id           : 4;
	UINT32 layer13_layer_alpha      : 8;
	UINT32 layer13_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_106
	UINT32 layer14_area_left        : 16;
	UINT32 layer14_area_right       : 16;

	//REGISTER saturn_cmps_reg_107
	UINT32 layer14_area_top         : 16;
	UINT32 layer14_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_108
	UINT32 layer14_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_109
	UINT32 layer14_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_110
	UINT32 layer14_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_111
	UINT32 layer14_solid_color_a    : 8;
	UINT32 layer14_rsv00            : 1;
	UINT32 layer14_rsv01            : 1;
	UINT32 layer14_rsv02            : 1;
	UINT32 layer14_rsv03            : 1;
	UINT32 layer14_rsv04            : 1;
	UINT32 layer14_rsv05            : 1;
	UINT32 layer14_blend_mode       : 2;
	UINT32 layer14_solid_area_mode  : 1;
	UINT32 layer14_spe_alpha_mode   : 1;
	UINT32 layer14_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_112
	UINT32 layer14_en               : 1;
	UINT32 layer14_blend_sel        : 2;
	UINT32 layer14_solid_en         : 1;
	UINT32 layer14_dma_id           : 4;
	UINT32 layer14_layer_alpha      : 8;
	UINT32 layer14_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_113
	UINT32 layer15_area_left        : 16;
	UINT32 layer15_area_right       : 16;

	//REGISTER saturn_cmps_reg_114
	UINT32 layer15_area_top         : 16;
	UINT32 layer15_area_bottom      : 16;

	//REGISTER saturn_cmps_reg_115
	UINT32 layer15_solid_color_r    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_116
	UINT32 layer15_solid_color_g    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_117
	UINT32 layer15_solid_color_b    : 10;
	UINT32:22;

	//REGISTER saturn_cmps_reg_118
	UINT32 layer15_solid_color_a    : 8;
	UINT32 layer15_rsv00            : 1;
	UINT32 layer15_rsv01            : 1;
	UINT32 layer15_rsv02            : 1;
	UINT32 layer15_rsv03            : 1;
	UINT32 layer15_rsv04            : 1;
	UINT32 layer15_rsv05            : 1;
	UINT32 layer15_blend_mode       : 2;
	UINT32 layer15_solid_area_mode  : 1;
	UINT32 layer15_spe_alpha_mode   : 1;
	UINT32 layer15_colorkey_en      : 1;
	UINT32:13;

	//REGISTER saturn_cmps_reg_119
	UINT32 layer15_en               : 1;
	UINT32 layer15_blend_sel        : 2;
	UINT32 layer15_solid_en         : 1;
	UINT32 layer15_dma_id           : 4;
	UINT32 layer15_layer_alpha      : 8;
	UINT32 layer15_alpha_ratio      : 8;
	UINT32:8;

	//REGISTER saturn_cmps_reg_120
	UINT32 layer_data_not_ready     : 32;

	//REGISTER saturn_cmps_reg_121
	struct {
	UINT32 layer_proc_location      : 32;
	} saturn_cmps_reg_121[16];

	//REGISTER saturn_cmps_reg_137
	UINT32 cmps_proc_location       : 32;

	//REGISTER saturn_cmps_reg_138
	UINT32 cfg_se                   : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_139
	UINT32 force_update_en          : 1;
	UINT32 vsync_update_en          : 1;
	UINT32 shadow_read_en           : 1;
	UINT32:29;

	//REGISTER saturn_cmps_reg_140
	UINT32 force_update_pulse       : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_141
	UINT32 force_update_en_se       : 1;
	UINT32 vsync_update_en_se       : 1;
	UINT32 shadow_read_en_se        : 1;
	UINT32:29;

	//REGISTER saturn_cmps_reg_142
	UINT32 force_update_pulse_se    : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_143
	UINT32 icg_override             : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_144
	UINT32 trigger                  : 1;
	UINT32:31;

	//REGISTER saturn_cmps_reg_145
	UINT32 trigger2                 : 1;
	UINT32:31;

	};

	INT32 value32[146];

} COMPOSER_X_REG;

typedef COMPOSER_X_REG CMPS_X_REG;
#endif
