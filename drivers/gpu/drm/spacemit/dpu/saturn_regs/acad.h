/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef ACAD_REG_H
#define ACAD_REG_H

typedef union {
	struct {
	//REGISTER saturn_acad_with_curve_reg_0
	UINT32 acad_en                 : 1;
	UINT32 m_n_hist_mode           : 1;
	UINT32 m_n_curve_mode          : 1;
	UINT32 m_n_etm_mode            : 1;
	UINT32:28;

	//REGISTER saturn_acad_with_curve_reg_1
	UINT32 acad_restart            : 1;
	UINT32:31;

	//REGISTER saturn_acad_with_curve_reg_2
	UINT32:32;

	//REGISTER saturn_acad_with_curve_reg_3
	UINT32 m_n_roi_x               : 13;
	UINT32:3;
	UINT32 m_n_roi_y               : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_4
	UINT32 m_n_roi_w               : 13;
	UINT32:3;
	UINT32 m_n_roi_h               : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_5
	UINT32 m_n_blk_width           : 9;
	UINT32 m_n_blk_num_hori        : 6;
	UINT32:1;
	UINT32 m_n_blk_height          : 9;
	UINT32 m_n_blk_num_vert        : 6;
	UINT32:1;

	//REGISTER saturn_acad_with_curve_reg_6
	UINT32 m_n_weight_scaler_x     : 11;
	UINT32:5;
	UINT32 m_n_weight_scaler_y     : 11;
	UINT32:5;

	//REGISTER saturn_acad_with_curve_reg_7
	UINT32 m_n_sub_roi0_x          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi0_y          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_8
	UINT32 m_n_sub_roi0_w          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi0_h          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_9
	UINT32 m_n_sub_roi1_x          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi1_y          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_10
	UINT32 m_n_sub_roi1_w          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi1_h          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_11
	UINT32 m_n_sub_roi2_x          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi2_y          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_12
	UINT32 m_n_sub_roi2_w          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi2_h          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_13
	UINT32 m_n_sub_roi3_x          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi3_y          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_14
	UINT32 m_n_sub_roi3_w          : 13;
	UINT32:3;
	UINT32 m_n_sub_roi3_h          : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_15
	UINT32 tp_alpha                : 9;
	UINT32:7;
	UINT32 m_n_curve_alpha         : 13;
	UINT32:3;

	//REGISTER saturn_acad_with_curve_reg_16
	UINT32 m_n_offset0             : 11;
	UINT32:5;
	UINT32 m_n_offset1             : 11;
	UINT32:5;

	//REGISTER saturn_acad_with_curve_reg_17
	UINT32 m_n_dst_alpha           : 8;
	UINT32 m_n_alpha_step          : 8;
	UINT32 m_n_skip_frames         : 8;
	UINT32 m_p_segment_thr0        : 8;

	//REGISTER saturn_acad_with_curve_reg_18
	UINT32 m_p_segment_thr1        : 8;
	UINT32 m_p_segment_thr2        : 8;
	UINT32 m_p_segment_thr3        : 8;
	UINT32 m_p_segment_thr4        : 8;

	//REGISTER saturn_acad_with_curve_reg_19
	UINT32 m_p_contrast_curve0_0   : 16;
	UINT32 m_p_contrast_curve1_0   : 16;

	//REGISTER saturn_acad_with_curve_reg_20
	UINT32 m_p_contrast_curve0_1   : 16;
	UINT32 m_p_contrast_curve1_1   : 16;

	//REGISTER saturn_acad_with_curve_reg_21
	UINT32 m_p_contrast_curve0_2   : 16;
	UINT32 m_p_contrast_curve1_2   : 16;

	//REGISTER saturn_acad_with_curve_reg_22
	UINT32 m_p_contrast_curve0_3   : 16;
	UINT32 m_p_contrast_curve1_3   : 16;

	//REGISTER saturn_acad_with_curve_reg_23
	UINT32 m_p_contrast_curve0_4   : 16;
	UINT32 m_p_contrast_curve1_4   : 16;

	//REGISTER saturn_acad_with_curve_reg_24
	UINT32 m_p_contrast_curve0_5   : 16;
	UINT32 m_p_contrast_curve1_5   : 16;

	//REGISTER saturn_acad_with_curve_reg_25
	UINT32 m_p_contrast_curve0_6   : 16;
	UINT32 m_p_contrast_curve1_6   : 16;

	//REGISTER saturn_acad_with_curve_reg_26
	UINT32 m_p_contrast_curve0_7   : 16;
	UINT32 m_p_contrast_curve1_7   : 16;

	//REGISTER saturn_acad_with_curve_reg_27
	UINT32 m_p_contrast_curve0_8   : 16;
	UINT32 m_p_contrast_curve1_8   : 16;

	//REGISTER saturn_acad_with_curve_reg_28
	UINT32 m_p_contrast_curve0_9   : 16;
	UINT32 m_p_contrast_curve1_9   : 16;

	//REGISTER saturn_acad_with_curve_reg_29
	UINT32 m_p_contrast_curve0_10  : 16;
	UINT32 m_p_contrast_curve1_10  : 16;

	//REGISTER saturn_acad_with_curve_reg_30
	UINT32 m_p_contrast_curve0_11  : 16;
	UINT32 m_p_contrast_curve1_11  : 16;

	//REGISTER saturn_acad_with_curve_reg_31
	UINT32 m_p_contrast_curve0_12  : 16;
	UINT32 m_p_contrast_curve1_12  : 16;

	//REGISTER saturn_acad_with_curve_reg_32
	UINT32 m_p_contrast_curve0_13  : 16;
	UINT32 m_p_contrast_curve1_13  : 16;

	//REGISTER saturn_acad_with_curve_reg_33
	UINT32 m_p_contrast_curve0_14  : 16;
	UINT32 m_p_contrast_curve1_14  : 16;

	//REGISTER saturn_acad_with_curve_reg_34
	UINT32 m_p_contrast_curve0_15  : 16;
	UINT32 m_p_contrast_curve1_15  : 16;

	//REGISTER saturn_acad_with_curve_reg_35
	UINT32 m_p_contrast_curve0_16  : 16;
	UINT32 m_p_contrast_curve1_16  : 16;

	//REGISTER saturn_acad_with_curve_reg_36
	UINT32 m_p_contrast_curve0_17  : 16;
	UINT32 m_p_contrast_curve1_17  : 16;

	//REGISTER saturn_acad_with_curve_reg_37
	UINT32 m_p_contrast_curve0_18  : 16;
	UINT32 m_p_contrast_curve1_18  : 16;

	//REGISTER saturn_acad_with_curve_reg_38
	UINT32 m_p_contrast_curve0_19  : 16;
	UINT32 m_p_contrast_curve1_19  : 16;

	//REGISTER saturn_acad_with_curve_reg_39
	UINT32 m_p_contrast_curve0_20  : 16;
	UINT32 m_p_contrast_curve1_20  : 16;

	//REGISTER saturn_acad_with_curve_reg_40
	UINT32 m_p_contrast_curve0_21  : 16;
	UINT32 m_p_contrast_curve1_21  : 16;

	//REGISTER saturn_acad_with_curve_reg_41
	UINT32 m_p_contrast_curve0_22  : 16;
	UINT32 m_p_contrast_curve1_22  : 16;

	//REGISTER saturn_acad_with_curve_reg_42
	UINT32 m_p_contrast_curve0_23  : 16;
	UINT32 m_p_contrast_curve1_23  : 16;

	//REGISTER saturn_acad_with_curve_reg_43
	UINT32 m_p_contrast_curve0_24  : 16;
	UINT32 m_p_contrast_curve1_24  : 16;

	//REGISTER saturn_acad_with_curve_reg_44
	UINT32 m_p_contrast_curve0_25  : 16;
	UINT32 m_p_contrast_curve1_25  : 16;

	//REGISTER saturn_acad_with_curve_reg_45
	UINT32 m_p_contrast_curve0_26  : 16;
	UINT32 m_p_contrast_curve1_26  : 16;

	//REGISTER saturn_acad_with_curve_reg_46
	UINT32 m_p_contrast_curve0_27  : 16;
	UINT32 m_p_contrast_curve1_27  : 16;

	//REGISTER saturn_acad_with_curve_reg_47
	UINT32 m_p_contrast_curve0_28  : 16;
	UINT32 m_p_contrast_curve1_28  : 16;

	//REGISTER saturn_acad_with_curve_reg_48
	UINT32 m_p_contrast_curve0_29  : 16;
	UINT32 m_p_contrast_curve1_29  : 16;

	//REGISTER saturn_acad_with_curve_reg_49
	UINT32 m_p_contrast_curve0_30  : 16;
	UINT32 m_p_contrast_curve1_30  : 16;

	//REGISTER saturn_acad_with_curve_reg_50
	UINT32 m_p_contrast_curve0_31  : 16;
	UINT32 m_p_contrast_curve1_31  : 16;

	//REGISTER saturn_acad_with_curve_reg_51
	UINT32 m_p_contrast_curve0_32  : 16;
	UINT32 m_p_contrast_curve1_32  : 16;

	//REGISTER saturn_acad_with_curve_reg_52
	UINT32 m_p_contrast_curve0_33  : 16;
	UINT32 m_p_contrast_curve1_33  : 16;

	//REGISTER saturn_acad_with_curve_reg_53
	UINT32 m_p_contrast_curve0_34  : 16;
	UINT32 m_p_contrast_curve1_34  : 16;

	//REGISTER saturn_acad_with_curve_reg_54
	UINT32 m_p_contrast_curve0_35  : 16;
	UINT32 m_p_contrast_curve1_35  : 16;

	//REGISTER saturn_acad_with_curve_reg_55
	UINT32 m_p_contrast_curve0_36  : 16;
	UINT32 m_p_contrast_curve1_36  : 16;

	//REGISTER saturn_acad_with_curve_reg_56
	UINT32 m_p_contrast_curve0_37  : 16;
	UINT32 m_p_contrast_curve1_37  : 16;

	//REGISTER saturn_acad_with_curve_reg_57
	UINT32 m_p_contrast_curve0_38  : 16;
	UINT32 m_p_contrast_curve1_38  : 16;

	//REGISTER saturn_acad_with_curve_reg_58
	UINT32 m_p_contrast_curve0_39  : 16;
	UINT32 m_p_contrast_curve1_39  : 16;

	//REGISTER saturn_acad_with_curve_reg_59
	UINT32 m_p_contrast_curve0_40  : 16;
	UINT32 m_p_contrast_curve1_40  : 16;

	//REGISTER saturn_acad_with_curve_reg_60
	UINT32 m_p_contrast_curve0_41  : 16;
	UINT32 m_p_contrast_curve1_41  : 16;

	//REGISTER saturn_acad_with_curve_reg_61
	UINT32 m_p_contrast_curve0_42  : 16;
	UINT32 m_p_contrast_curve1_42  : 16;

	//REGISTER saturn_acad_with_curve_reg_62
	UINT32 m_p_contrast_curve0_43  : 16;
	UINT32 m_p_contrast_curve1_43  : 16;

	//REGISTER saturn_acad_with_curve_reg_63
	UINT32 m_p_contrast_curve0_44  : 16;
	UINT32 m_p_contrast_curve1_44  : 16;

	//REGISTER saturn_acad_with_curve_reg_64
	UINT32 m_p_contrast_curve0_45  : 16;
	UINT32 m_p_contrast_curve1_45  : 16;

	//REGISTER saturn_acad_with_curve_reg_65
	UINT32 m_p_contrast_curve0_46  : 16;
	UINT32 m_p_contrast_curve1_46  : 16;

	//REGISTER saturn_acad_with_curve_reg_66
	UINT32 m_p_contrast_curve0_47  : 16;
	UINT32 m_p_contrast_curve1_47  : 16;

	//REGISTER saturn_acad_with_curve_reg_67
	UINT32 m_p_contrast_curve0_48  : 16;
	UINT32 m_p_contrast_curve1_48  : 16;

	//REGISTER saturn_acad_with_curve_reg_68
	UINT32 m_p_contrast_curve0_49  : 16;
	UINT32 m_p_contrast_curve1_49  : 16;

	//REGISTER saturn_acad_with_curve_reg_69
	UINT32 m_p_contrast_curve0_50  : 16;
	UINT32 m_p_contrast_curve1_50  : 16;

	//REGISTER saturn_acad_with_curve_reg_70
	UINT32 m_p_contrast_curve0_51  : 16;
	UINT32 m_p_contrast_curve1_51  : 16;

	//REGISTER saturn_acad_with_curve_reg_71
	UINT32 m_p_contrast_curve0_52  : 16;
	UINT32 m_p_contrast_curve1_52  : 16;

	//REGISTER saturn_acad_with_curve_reg_72
	UINT32 m_p_contrast_curve0_53  : 16;
	UINT32 m_p_contrast_curve1_53  : 16;

	//REGISTER saturn_acad_with_curve_reg_73
	UINT32 m_p_contrast_curve0_54  : 16;
	UINT32 m_p_contrast_curve1_54  : 16;

	//REGISTER saturn_acad_with_curve_reg_74
	UINT32 m_p_contrast_curve0_55  : 16;
	UINT32 m_p_contrast_curve1_55  : 16;

	//REGISTER saturn_acad_with_curve_reg_75
	UINT32 m_p_contrast_curve0_56  : 16;
	UINT32 m_p_contrast_curve1_56  : 16;

	//REGISTER saturn_acad_with_curve_reg_76
	UINT32 m_p_contrast_curve0_57  : 16;
	UINT32 m_p_contrast_curve1_57  : 16;

	//REGISTER saturn_acad_with_curve_reg_77
	UINT32 m_p_contrast_curve0_58  : 16;
	UINT32 m_p_contrast_curve1_58  : 16;

	//REGISTER saturn_acad_with_curve_reg_78
	UINT32 m_p_contrast_curve0_59  : 16;
	UINT32 m_p_contrast_curve1_59  : 16;

	//REGISTER saturn_acad_with_curve_reg_79
	UINT32 m_p_contrast_curve0_60  : 16;
	UINT32 m_p_contrast_curve1_60  : 16;

	//REGISTER saturn_acad_with_curve_reg_80
	UINT32 m_p_contrast_curve0_61  : 16;
	UINT32 m_p_contrast_curve1_61  : 16;

	//REGISTER saturn_acad_with_curve_reg_81
	UINT32 m_p_contrast_curve0_62  : 16;
	UINT32 m_p_contrast_curve1_62  : 16;

	//REGISTER saturn_acad_with_curve_reg_82
	UINT32 m_p_contrast_curve0_63  : 16;
	UINT32 m_p_contrast_curve1_63  : 16;

	//REGISTER saturn_acad_with_curve_reg_83
	UINT32 m_p_contrast_curve0_64  : 16;
	UINT32 m_p_contrast_curve1_64  : 16;

	//REGISTER saturn_acad_with_curve_reg_84
	UINT32 m_p_contrast_curve2_0   : 16;
	UINT32 m_p_contrast_curve3_0   : 16;

	//REGISTER saturn_acad_with_curve_reg_85
	UINT32 m_p_contrast_curve2_1   : 16;
	UINT32 m_p_contrast_curve3_1   : 16;

	//REGISTER saturn_acad_with_curve_reg_86
	UINT32 m_p_contrast_curve2_2   : 16;
	UINT32 m_p_contrast_curve3_2   : 16;

	//REGISTER saturn_acad_with_curve_reg_87
	UINT32 m_p_contrast_curve2_3   : 16;
	UINT32 m_p_contrast_curve3_3   : 16;

	//REGISTER saturn_acad_with_curve_reg_88
	UINT32 m_p_contrast_curve2_4   : 16;
	UINT32 m_p_contrast_curve3_4   : 16;

	//REGISTER saturn_acad_with_curve_reg_89
	UINT32 m_p_contrast_curve2_5   : 16;
	UINT32 m_p_contrast_curve3_5   : 16;

	//REGISTER saturn_acad_with_curve_reg_90
	UINT32 m_p_contrast_curve2_6   : 16;
	UINT32 m_p_contrast_curve3_6   : 16;

	//REGISTER saturn_acad_with_curve_reg_91
	UINT32 m_p_contrast_curve2_7   : 16;
	UINT32 m_p_contrast_curve3_7   : 16;

	//REGISTER saturn_acad_with_curve_reg_92
	UINT32 m_p_contrast_curve2_8   : 16;
	UINT32 m_p_contrast_curve3_8   : 16;

	//REGISTER saturn_acad_with_curve_reg_93
	UINT32 m_p_contrast_curve2_9   : 16;
	UINT32 m_p_contrast_curve3_9   : 16;

	//REGISTER saturn_acad_with_curve_reg_94
	UINT32 m_p_contrast_curve2_10  : 16;
	UINT32 m_p_contrast_curve3_10  : 16;

	//REGISTER saturn_acad_with_curve_reg_95
	UINT32 m_p_contrast_curve2_11  : 16;
	UINT32 m_p_contrast_curve3_11  : 16;

	//REGISTER saturn_acad_with_curve_reg_96
	UINT32 m_p_contrast_curve2_12  : 16;
	UINT32 m_p_contrast_curve3_12  : 16;

	//REGISTER saturn_acad_with_curve_reg_97
	UINT32 m_p_contrast_curve2_13  : 16;
	UINT32 m_p_contrast_curve3_13  : 16;

	//REGISTER saturn_acad_with_curve_reg_98
	UINT32 m_p_contrast_curve2_14  : 16;
	UINT32 m_p_contrast_curve3_14  : 16;

	//REGISTER saturn_acad_with_curve_reg_99
	UINT32 m_p_contrast_curve2_15  : 16;
	UINT32 m_p_contrast_curve3_15  : 16;

	//REGISTER saturn_acad_with_curve_reg_100
	UINT32 m_p_contrast_curve2_16  : 16;
	UINT32 m_p_contrast_curve3_16  : 16;

	//REGISTER saturn_acad_with_curve_reg_101
	UINT32 m_p_contrast_curve2_17  : 16;
	UINT32 m_p_contrast_curve3_17  : 16;

	//REGISTER saturn_acad_with_curve_reg_102
	UINT32 m_p_contrast_curve2_18  : 16;
	UINT32 m_p_contrast_curve3_18  : 16;

	//REGISTER saturn_acad_with_curve_reg_103
	UINT32 m_p_contrast_curve2_19  : 16;
	UINT32 m_p_contrast_curve3_19  : 16;

	//REGISTER saturn_acad_with_curve_reg_104
	UINT32 m_p_contrast_curve2_20  : 16;
	UINT32 m_p_contrast_curve3_20  : 16;

	//REGISTER saturn_acad_with_curve_reg_105
	UINT32 m_p_contrast_curve2_21  : 16;
	UINT32 m_p_contrast_curve3_21  : 16;

	//REGISTER saturn_acad_with_curve_reg_106
	UINT32 m_p_contrast_curve2_22  : 16;
	UINT32 m_p_contrast_curve3_22  : 16;

	//REGISTER saturn_acad_with_curve_reg_107
	UINT32 m_p_contrast_curve2_23  : 16;
	UINT32 m_p_contrast_curve3_23  : 16;

	//REGISTER saturn_acad_with_curve_reg_108
	UINT32 m_p_contrast_curve2_24  : 16;
	UINT32 m_p_contrast_curve3_24  : 16;

	//REGISTER saturn_acad_with_curve_reg_109
	UINT32 m_p_contrast_curve2_25  : 16;
	UINT32 m_p_contrast_curve3_25  : 16;

	//REGISTER saturn_acad_with_curve_reg_110
	UINT32 m_p_contrast_curve2_26  : 16;
	UINT32 m_p_contrast_curve3_26  : 16;

	//REGISTER saturn_acad_with_curve_reg_111
	UINT32 m_p_contrast_curve2_27  : 16;
	UINT32 m_p_contrast_curve3_27  : 16;

	//REGISTER saturn_acad_with_curve_reg_112
	UINT32 m_p_contrast_curve2_28  : 16;
	UINT32 m_p_contrast_curve3_28  : 16;

	//REGISTER saturn_acad_with_curve_reg_113
	UINT32 m_p_contrast_curve2_29  : 16;
	UINT32 m_p_contrast_curve3_29  : 16;

	//REGISTER saturn_acad_with_curve_reg_114
	UINT32 m_p_contrast_curve2_30  : 16;
	UINT32 m_p_contrast_curve3_30  : 16;

	//REGISTER saturn_acad_with_curve_reg_115
	UINT32 m_p_contrast_curve2_31  : 16;
	UINT32 m_p_contrast_curve3_31  : 16;

	//REGISTER saturn_acad_with_curve_reg_116
	UINT32 m_p_contrast_curve2_32  : 16;
	UINT32 m_p_contrast_curve3_32  : 16;

	//REGISTER saturn_acad_with_curve_reg_117
	UINT32 m_p_contrast_curve2_33  : 16;
	UINT32 m_p_contrast_curve3_33  : 16;

	//REGISTER saturn_acad_with_curve_reg_118
	UINT32 m_p_contrast_curve2_34  : 16;
	UINT32 m_p_contrast_curve3_34  : 16;

	//REGISTER saturn_acad_with_curve_reg_119
	UINT32 m_p_contrast_curve2_35  : 16;
	UINT32 m_p_contrast_curve3_35  : 16;

	//REGISTER saturn_acad_with_curve_reg_120
	UINT32 m_p_contrast_curve2_36  : 16;
	UINT32 m_p_contrast_curve3_36  : 16;

	//REGISTER saturn_acad_with_curve_reg_121
	UINT32 m_p_contrast_curve2_37  : 16;
	UINT32 m_p_contrast_curve3_37  : 16;

	//REGISTER saturn_acad_with_curve_reg_122
	UINT32 m_p_contrast_curve2_38  : 16;
	UINT32 m_p_contrast_curve3_38  : 16;

	//REGISTER saturn_acad_with_curve_reg_123
	UINT32 m_p_contrast_curve2_39  : 16;
	UINT32 m_p_contrast_curve3_39  : 16;

	//REGISTER saturn_acad_with_curve_reg_124
	UINT32 m_p_contrast_curve2_40  : 16;
	UINT32 m_p_contrast_curve3_40  : 16;

	//REGISTER saturn_acad_with_curve_reg_125
	UINT32 m_p_contrast_curve2_41  : 16;
	UINT32 m_p_contrast_curve3_41  : 16;

	//REGISTER saturn_acad_with_curve_reg_126
	UINT32 m_p_contrast_curve2_42  : 16;
	UINT32 m_p_contrast_curve3_42  : 16;

	//REGISTER saturn_acad_with_curve_reg_127
	UINT32 m_p_contrast_curve2_43  : 16;
	UINT32 m_p_contrast_curve3_43  : 16;

	//REGISTER saturn_acad_with_curve_reg_128
	UINT32 m_p_contrast_curve2_44  : 16;
	UINT32 m_p_contrast_curve3_44  : 16;

	//REGISTER saturn_acad_with_curve_reg_129
	UINT32 m_p_contrast_curve2_45  : 16;
	UINT32 m_p_contrast_curve3_45  : 16;

	//REGISTER saturn_acad_with_curve_reg_130
	UINT32 m_p_contrast_curve2_46  : 16;
	UINT32 m_p_contrast_curve3_46  : 16;

	//REGISTER saturn_acad_with_curve_reg_131
	UINT32 m_p_contrast_curve2_47  : 16;
	UINT32 m_p_contrast_curve3_47  : 16;

	//REGISTER saturn_acad_with_curve_reg_132
	UINT32 m_p_contrast_curve2_48  : 16;
	UINT32 m_p_contrast_curve3_48  : 16;

	//REGISTER saturn_acad_with_curve_reg_133
	UINT32 m_p_contrast_curve2_49  : 16;
	UINT32 m_p_contrast_curve3_49  : 16;

	//REGISTER saturn_acad_with_curve_reg_134
	UINT32 m_p_contrast_curve2_50  : 16;
	UINT32 m_p_contrast_curve3_50  : 16;

	//REGISTER saturn_acad_with_curve_reg_135
	UINT32 m_p_contrast_curve2_51  : 16;
	UINT32 m_p_contrast_curve3_51  : 16;

	//REGISTER saturn_acad_with_curve_reg_136
	UINT32 m_p_contrast_curve2_52  : 16;
	UINT32 m_p_contrast_curve3_52  : 16;

	//REGISTER saturn_acad_with_curve_reg_137
	UINT32 m_p_contrast_curve2_53  : 16;
	UINT32 m_p_contrast_curve3_53  : 16;

	//REGISTER saturn_acad_with_curve_reg_138
	UINT32 m_p_contrast_curve2_54  : 16;
	UINT32 m_p_contrast_curve3_54  : 16;

	//REGISTER saturn_acad_with_curve_reg_139
	UINT32 m_p_contrast_curve2_55  : 16;
	UINT32 m_p_contrast_curve3_55  : 16;

	//REGISTER saturn_acad_with_curve_reg_140
	UINT32 m_p_contrast_curve2_56  : 16;
	UINT32 m_p_contrast_curve3_56  : 16;

	//REGISTER saturn_acad_with_curve_reg_141
	UINT32 m_p_contrast_curve2_57  : 16;
	UINT32 m_p_contrast_curve3_57  : 16;

	//REGISTER saturn_acad_with_curve_reg_142
	UINT32 m_p_contrast_curve2_58  : 16;
	UINT32 m_p_contrast_curve3_58  : 16;

	//REGISTER saturn_acad_with_curve_reg_143
	UINT32 m_p_contrast_curve2_59  : 16;
	UINT32 m_p_contrast_curve3_59  : 16;

	//REGISTER saturn_acad_with_curve_reg_144
	UINT32 m_p_contrast_curve2_60  : 16;
	UINT32 m_p_contrast_curve3_60  : 16;

	//REGISTER saturn_acad_with_curve_reg_145
	UINT32 m_p_contrast_curve2_61  : 16;
	UINT32 m_p_contrast_curve3_61  : 16;

	//REGISTER saturn_acad_with_curve_reg_146
	UINT32 m_p_contrast_curve2_62  : 16;
	UINT32 m_p_contrast_curve3_62  : 16;

	//REGISTER saturn_acad_with_curve_reg_147
	UINT32 m_p_contrast_curve2_63  : 16;
	UINT32 m_p_contrast_curve3_63  : 16;

	//REGISTER saturn_acad_with_curve_reg_148
	UINT32 m_p_contrast_curve2_64  : 16;
	UINT32 m_p_contrast_curve3_64  : 16;

	//REGISTER saturn_acad_with_curve_reg_149
	UINT32 force_update_en         : 1;
	UINT32 vsync_update_en         : 1;
	UINT32 shadow_read_en          : 1;
	UINT32:29;

	//REGISTER saturn_acad_with_curve_reg_150
	UINT32 force_update_pulse      : 1;
	UINT32:31;

	//REGISTER saturn_acad_with_curve_reg_151
	UINT32 icg_override            : 1;
	UINT32:31;

	//REGISTER saturn_acad_with_curve_reg_152
	UINT32 trigger                 : 1;
	UINT32:31;

	//REGISTER saturn_acad_with_curve_reg_153
	UINT32 trigger2                : 1;
	UINT32:31;

	};

	INT32 value32[154];

} ACAD_REG;

#endif
