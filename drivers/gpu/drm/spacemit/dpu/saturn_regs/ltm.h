/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef LTM_REG_H
#define LTM_REG_H

typedef union {
	struct {
	//REGISTER outctl_postproc_reg_0
	UINT32 m_npost_proc_en               : 1;
	UINT32 m_ngain_to_full_en            : 1;
	UINT32 m_nmatrix_en                  : 1;
	UINT32 m_nendmatrix_en               : 1;
	UINT32 m_nfront_tmootf_en            : 1;
	UINT32 m_nend_tmootf_en              : 1;
	UINT32 m_neotf_en                    : 1;
	UINT32 m_noetf_en                    : 1;
	UINT32:24;

	//REGISTER outctl_postproc_reg_1
	UINT32 m_neotf_mode                  : 3;
	UINT32:29;

	//REGISTER outctl_postproc_reg_2
	UINT32 m_noetf_mode                  : 3;
	UINT32:13;
	UINT32 m_noetf_max                   : 10;
	UINT32:6;

	//REGISTER outctl_postproc_reg_3
	UINT32 m_pfront_tmootf_gain_table0   : 16;
	UINT32 m_pfront_tmootf_gain_table1   : 16;

	//REGISTER outctl_postproc_reg_4
	UINT32 m_pfront_tmootf_gain_table2   : 16;
	UINT32 m_pfront_tmootf_gain_table3   : 16;

	//REGISTER outctl_postproc_reg_5
	UINT32 m_pfront_tmootf_gain_table4   : 16;
	UINT32 m_pfront_tmootf_gain_table5   : 16;

	//REGISTER outctl_postproc_reg_6
	UINT32 m_pfront_tmootf_gain_table6   : 16;
	UINT32 m_pfront_tmootf_gain_table7   : 16;

	//REGISTER outctl_postproc_reg_7
	UINT32 m_pfront_tmootf_gain_table8   : 16;
	UINT32 m_pfront_tmootf_gain_table9   : 16;

	//REGISTER outctl_postproc_reg_8
	UINT32 m_pfront_tmootf_gain_table10  : 16;
	UINT32 m_pfront_tmootf_gain_table11  : 16;

	//REGISTER outctl_postproc_reg_9
	UINT32 m_pfront_tmootf_gain_table12  : 16;
	UINT32 m_pfront_tmootf_gain_table13  : 16;

	//REGISTER outctl_postproc_reg_10
	UINT32 m_pfront_tmootf_gain_table14  : 16;
	UINT32 m_pfront_tmootf_gain_table15  : 16;

	//REGISTER outctl_postproc_reg_11
	UINT32 m_pfront_tmootf_gain_table16  : 16;
	UINT32 m_pfront_tmootf_gain_table17  : 16;

	//REGISTER outctl_postproc_reg_12
	UINT32 m_pfront_tmootf_gain_table18  : 16;
	UINT32 m_pfront_tmootf_gain_table19  : 16;

	//REGISTER outctl_postproc_reg_13
	UINT32 m_pfront_tmootf_gain_table20  : 16;
	UINT32 m_pfront_tmootf_gain_table21  : 16;

	//REGISTER outctl_postproc_reg_14
	UINT32 m_pfront_tmootf_gain_table22  : 16;
	UINT32 m_pfront_tmootf_gain_table23  : 16;

	//REGISTER outctl_postproc_reg_15
	UINT32 m_pfront_tmootf_gain_table24  : 16;
	UINT32 m_pfront_tmootf_gain_table25  : 16;

	//REGISTER outctl_postproc_reg_16
	UINT32 m_pfront_tmootf_gain_table26  : 16;
	UINT32 m_pfront_tmootf_gain_table27  : 16;

	//REGISTER outctl_postproc_reg_17
	UINT32 m_pfront_tmootf_gain_table28  : 16;
	UINT32 m_pfront_tmootf_gain_table29  : 16;

	//REGISTER outctl_postproc_reg_18
	UINT32 m_pfront_tmootf_gain_table30  : 16;
	UINT32 m_pfront_tmootf_gain_table31  : 16;

	//REGISTER outctl_postproc_reg_19
	UINT32 m_pfront_tmootf_gain_table32  : 16;
	UINT32 m_pfront_tmootf_gain_table33  : 16;

	//REGISTER outctl_postproc_reg_20
	UINT32 m_pfront_tmootf_gain_table34  : 16;
	UINT32 m_pfront_tmootf_gain_table35  : 16;

	//REGISTER outctl_postproc_reg_21
	UINT32 m_pfront_tmootf_gain_table36  : 16;
	UINT32 m_pfront_tmootf_gain_table37  : 16;

	//REGISTER outctl_postproc_reg_22
	UINT32 m_pfront_tmootf_gain_table38  : 16;
	UINT32 m_pfront_tmootf_gain_table39  : 16;

	//REGISTER outctl_postproc_reg_23
	UINT32 m_pfront_tmootf_gain_table40  : 16;
	UINT32 m_pfront_tmootf_gain_table41  : 16;

	//REGISTER outctl_postproc_reg_24
	UINT32 m_pfront_tmootf_gain_table42  : 16;
	UINT32 m_pfront_tmootf_gain_table43  : 16;

	//REGISTER outctl_postproc_reg_25
	UINT32 m_pfront_tmootf_gain_table44  : 16;
	UINT32 m_pfront_tmootf_gain_table45  : 16;

	//REGISTER outctl_postproc_reg_26
	UINT32 m_pfront_tmootf_gain_table46  : 16;
	UINT32 m_pfront_tmootf_gain_table47  : 16;

	//REGISTER outctl_postproc_reg_27
	UINT32 m_pfront_tmootf_gain_table48  : 16;
	UINT32 m_pfront_tmootf_gain_table49  : 16;

	//REGISTER outctl_postproc_reg_28
	UINT32 m_pfront_tmootf_gain_table50  : 16;
	UINT32 m_pfront_tmootf_gain_table51  : 16;

	//REGISTER outctl_postproc_reg_29
	UINT32 m_pfront_tmootf_gain_table52  : 16;
	UINT32 m_pfront_tmootf_gain_table53  : 16;

	//REGISTER outctl_postproc_reg_30
	UINT32 m_pfront_tmootf_gain_table54  : 16;
	UINT32 m_pfront_tmootf_gain_table55  : 16;

	//REGISTER outctl_postproc_reg_31
	UINT32 m_pfront_tmootf_gain_table56  : 16;
	UINT32 m_pfront_tmootf_gain_table57  : 16;

	//REGISTER outctl_postproc_reg_32
	UINT32 m_pfront_tmootf_gain_table58  : 16;
	UINT32 m_pfront_tmootf_gain_table59  : 16;

	//REGISTER outctl_postproc_reg_33
	UINT32 m_pfront_tmootf_gain_table60  : 16;
	UINT32 m_pfront_tmootf_gain_table61  : 16;

	//REGISTER outctl_postproc_reg_34
	UINT32 m_pfront_tmootf_gain_table62  : 16;
	UINT32 m_pfront_tmootf_gain_table63  : 16;

	//REGISTER outctl_postproc_reg_35
	UINT32 m_pfront_tmootf_gain_table64  : 16;
	UINT32 m_nfront_tmootf_shift_bits    : 5;
	UINT32 m_nfront_tmootf_rgb_mode      : 2;
	UINT32:9;

	//REGISTER outctl_postproc_reg_36
	UINT32 m_pend_tmootf_gain_table0     : 16;
	UINT32 m_pend_tmootf_gain_table1     : 16;

	//REGISTER outctl_postproc_reg_37
	UINT32 m_pend_tmootf_gain_table2     : 16;
	UINT32 m_pend_tmootf_gain_table3     : 16;

	//REGISTER outctl_postproc_reg_38
	UINT32 m_pend_tmootf_gain_table4     : 16;
	UINT32 m_pend_tmootf_gain_table5     : 16;

	//REGISTER outctl_postproc_reg_39
	UINT32 m_pend_tmootf_gain_table6     : 16;
	UINT32 m_pend_tmootf_gain_table7     : 16;

	//REGISTER outctl_postproc_reg_40
	UINT32 m_pend_tmootf_gain_table8     : 16;
	UINT32 m_pend_tmootf_gain_table9     : 16;

	//REGISTER outctl_postproc_reg_41
	UINT32 m_pend_tmootf_gain_table10    : 16;
	UINT32 m_pend_tmootf_gain_table11    : 16;

	//REGISTER outctl_postproc_reg_42
	UINT32 m_pend_tmootf_gain_table12    : 16;
	UINT32 m_pend_tmootf_gain_table13    : 16;

	//REGISTER outctl_postproc_reg_43
	UINT32 m_pend_tmootf_gain_table14    : 16;
	UINT32 m_pend_tmootf_gain_table15    : 16;

	//REGISTER outctl_postproc_reg_44
	UINT32 m_pend_tmootf_gain_table16    : 16;
	UINT32 m_pend_tmootf_gain_table17    : 16;

	//REGISTER outctl_postproc_reg_45
	UINT32 m_pend_tmootf_gain_table18    : 16;
	UINT32 m_pend_tmootf_gain_table19    : 16;

	//REGISTER outctl_postproc_reg_46
	UINT32 m_pend_tmootf_gain_table20    : 16;
	UINT32 m_pend_tmootf_gain_table21    : 16;

	//REGISTER outctl_postproc_reg_47
	UINT32 m_pend_tmootf_gain_table22    : 16;
	UINT32 m_pend_tmootf_gain_table23    : 16;

	//REGISTER outctl_postproc_reg_48
	UINT32 m_pend_tmootf_gain_table24    : 16;
	UINT32 m_pend_tmootf_gain_table25    : 16;

	//REGISTER outctl_postproc_reg_49
	UINT32 m_pend_tmootf_gain_table26    : 16;
	UINT32 m_pend_tmootf_gain_table27    : 16;

	//REGISTER outctl_postproc_reg_50
	UINT32 m_pend_tmootf_gain_table28    : 16;
	UINT32 m_pend_tmootf_gain_table29    : 16;

	//REGISTER outctl_postproc_reg_51
	UINT32 m_pend_tmootf_gain_table30    : 16;
	UINT32 m_pend_tmootf_gain_table31    : 16;

	//REGISTER outctl_postproc_reg_52
	UINT32 m_pend_tmootf_gain_table32    : 16;
	UINT32 m_pend_tmootf_gain_table33    : 16;

	//REGISTER outctl_postproc_reg_53
	UINT32 m_pend_tmootf_gain_table34    : 16;
	UINT32 m_pend_tmootf_gain_table35    : 16;

	//REGISTER outctl_postproc_reg_54
	UINT32 m_pend_tmootf_gain_table36    : 16;
	UINT32 m_pend_tmootf_gain_table37    : 16;

	//REGISTER outctl_postproc_reg_55
	UINT32 m_pend_tmootf_gain_table38    : 16;
	UINT32 m_pend_tmootf_gain_table39    : 16;

	//REGISTER outctl_postproc_reg_56
	UINT32 m_pend_tmootf_gain_table40    : 16;
	UINT32 m_pend_tmootf_gain_table41    : 16;

	//REGISTER outctl_postproc_reg_57
	UINT32 m_pend_tmootf_gain_table42    : 16;
	UINT32 m_pend_tmootf_gain_table43    : 16;

	//REGISTER outctl_postproc_reg_58
	UINT32 m_pend_tmootf_gain_table44    : 16;
	UINT32 m_pend_tmootf_gain_table45    : 16;

	//REGISTER outctl_postproc_reg_59
	UINT32 m_pend_tmootf_gain_table46    : 16;
	UINT32 m_pend_tmootf_gain_table47    : 16;

	//REGISTER outctl_postproc_reg_60
	UINT32 m_pend_tmootf_gain_table48    : 16;
	UINT32 m_pend_tmootf_gain_table49    : 16;

	//REGISTER outctl_postproc_reg_61
	UINT32 m_pend_tmootf_gain_table50    : 16;
	UINT32 m_pend_tmootf_gain_table51    : 16;

	//REGISTER outctl_postproc_reg_62
	UINT32 m_pend_tmootf_gain_table52    : 16;
	UINT32 m_pend_tmootf_gain_table53    : 16;

	//REGISTER outctl_postproc_reg_63
	UINT32 m_pend_tmootf_gain_table54    : 16;
	UINT32 m_pend_tmootf_gain_table55    : 16;

	//REGISTER outctl_postproc_reg_64
	UINT32 m_pend_tmootf_gain_table56    : 16;
	UINT32 m_pend_tmootf_gain_table57    : 16;

	//REGISTER outctl_postproc_reg_65
	UINT32 m_pend_tmootf_gain_table58    : 16;
	UINT32 m_pend_tmootf_gain_table59    : 16;

	//REGISTER outctl_postproc_reg_66
	UINT32 m_pend_tmootf_gain_table60    : 16;
	UINT32 m_pend_tmootf_gain_table61    : 16;

	//REGISTER outctl_postproc_reg_67
	UINT32 m_pend_tmootf_gain_table62    : 16;
	UINT32 m_pend_tmootf_gain_table63    : 16;

	//REGISTER outctl_postproc_reg_68
	UINT32 m_pend_tmootf_gain_table64    : 16;
	UINT32 m_nend_tmootf_shift_bits      : 5;
	UINT32 m_nend_tmootf_rgb_mode        : 2;
	UINT32:9;

	//REGISTER outctl_postproc_reg_69
	UINT32 m_pmatrix_table0              : 16;
	UINT32 m_pmatrix_table1              : 16;

	//REGISTER outctl_postproc_reg_70
	UINT32 m_pmatrix_table2              : 16;
	UINT32 m_pmatrix_table3              : 16;

	//REGISTER outctl_postproc_reg_71
	UINT32 m_pmatrix_table4              : 16;
	UINT32 m_pmatrix_table5              : 16;

	//REGISTER outctl_postproc_reg_72
	UINT32 m_pmatrix_table6              : 16;
	UINT32 m_pmatrix_table7              : 16;

	//REGISTER outctl_postproc_reg_73
	UINT32 m_pmatrix_table8              : 16;
	UINT32:16;

	//REGISTER outctl_postproc_reg_74
	UINT32 m_pmatrix_offset0             : 25;
	UINT32:7;

	//REGISTER outctl_postproc_reg_75
	UINT32 m_pmatrix_offset1             : 25;
	UINT32:7;

	//REGISTER outctl_postproc_reg_76
	UINT32 m_pmatrix_offset2             : 25;
	UINT32:7;

	//REGISTER outctl_postproc_reg_77
	UINT32 m_ngain_to_full               : 16;
	UINT32:16;

	//REGISTER outctl_postproc_reg_78
	UINT32 m_pendmatrix_table0           : 16;
	UINT32 m_pendmatrix_table1           : 16;

	//REGISTER outctl_postproc_reg_79
	UINT32 m_pendmatrix_table2           : 16;
	UINT32 m_pendmatrix_table3           : 16;

	//REGISTER outctl_postproc_reg_80
	UINT32 m_pendmatrix_table4           : 16;
	UINT32 m_pendmatrix_table5           : 16;

	//REGISTER outctl_postproc_reg_81
	UINT32 m_pendmatrix_table6           : 16;
	UINT32 m_pendmatrix_table7           : 16;

	//REGISTER outctl_postproc_reg_82
	UINT32 m_pendmatrix_table8           : 16;
	UINT32:16;

	//REGISTER outctl_postproc_reg_83
	UINT32 m_pendmatrix_offset0          : 11;
	UINT32:21;

	//REGISTER outctl_postproc_reg_84
	UINT32 m_pendmatrix_offset1          : 11;
	UINT32:21;

	//REGISTER outctl_postproc_reg_85
	UINT32 m_pendmatrix_offset2          : 11;
	UINT32:21;

	//REGISTER outctl_postproc_reg_86
	UINT32 lut4_group_0                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_87
	UINT32 lut4_group_1                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_88
	UINT32 lut4_group_2                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_89
	UINT32 lut4_group_3                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_90
	UINT32 lut4_group_4                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_91
	UINT32 lut4_group_5                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_92
	UINT32 lut4_group_6                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_93
	UINT32 lut4_group_7                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_94
	UINT32 lut4_group_8                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_95
	UINT32 lut4_group_9                  : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_96
	UINT32 lut4_group_10                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_97
	UINT32 lut4_group_11                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_98
	UINT32 lut4_group_12                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_99
	UINT32 lut4_group_13                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_100
	UINT32 lut4_group_14                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_101
	UINT32 lut4_group_15                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_102
	UINT32 lut4_group_16                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_103
	UINT32 lut4_group_17                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_104
	UINT32 lut4_group_18                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_105
	UINT32 lut4_group_19                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_106
	UINT32 lut4_group_20                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_107
	UINT32 lut4_group_21                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_108
	UINT32 lut4_group_22                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_109
	UINT32 lut4_group_23                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_110
	UINT32 lut4_group_24                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_111
	UINT32 lut4_group_25                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_112
	UINT32 lut4_group_26                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_113
	UINT32 lut4_group_27                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_114
	UINT32 lut4_group_28                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_115
	UINT32 lut4_group_29                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_116
	UINT32 lut4_group_30                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_117
	UINT32 lut4_group_31                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_118
	UINT32 lut4_group_32                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_119
	UINT32 lut4_group_33                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_120
	UINT32 lut4_group_34                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_121
	UINT32 lut4_group_35                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_122
	UINT32 lut4_group_36                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_123
	UINT32 lut4_group_37                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_124
	UINT32 lut4_group_38                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_125
	UINT32 lut4_group_39                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_126
	UINT32 lut4_group_40                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_127
	UINT32 lut4_group_41                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_128
	UINT32 lut4_group_42                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_129
	UINT32 lut4_group_43                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_130
	UINT32 lut4_group_44                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_131
	UINT32 lut4_group_45                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_132
	UINT32 lut4_group_46                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_133
	UINT32 lut4_group_47                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_134
	UINT32 lut4_group_48                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_135
	UINT32 lut4_group_49                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_136
	UINT32 lut4_group_50                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_137
	UINT32 lut4_group_51                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_138
	UINT32 lut4_group_52                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_139
	UINT32 lut4_group_53                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_140
	UINT32 lut4_group_54                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_141
	UINT32 lut4_group_55                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_142
	UINT32 lut4_group_56                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_143
	UINT32 lut4_group_57                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_144
	UINT32 lut4_group_58                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_145
	UINT32 lut4_group_59                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_146
	UINT32 lut4_group_60                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_147
	UINT32 lut4_group_61                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_148
	UINT32 lut4_group_62                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_149
	UINT32 lut4_group_63                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_150
	UINT32 lut4_group_64                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_151
	UINT32 lut4_group_65                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_152
	UINT32 lut4_group_66                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_153
	UINT32 lut4_group_67                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_154
	UINT32 lut4_group_68                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_155
	UINT32 lut4_group_69                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_156
	UINT32 lut4_group_70                 : 24;
	UINT32:8;

	//REGISTER outctl_postproc_reg_157
	UINT32 m_tmootf_weightYr             : 12;
	UINT32:4;
	UINT32 m_tmootf_weightYg             : 12;
	UINT32:4;

	//REGISTER outctl_postproc_reg_158
	UINT32 m_tmootf_weightYb             : 12;
	UINT32:20;

	//REGISTER outctl_postproc_reg_159
	UINT32 force_update_en               : 1;
	UINT32 vsync_update_en               : 1;
	UINT32 shadow_read_en                : 1;
	UINT32:29;

	//REGISTER outctl_postproc_reg_160
	UINT32 force_update_pulse            : 1;
	UINT32:31;

	//REGISTER outctl_postproc_reg_161
	UINT32 icg_override                  : 1;
	UINT32:31;

	//REGISTER outctl_postproc_reg_162
	UINT32 trigger                       : 1;
	UINT32:31;

	//REGISTER outctl_postproc_reg_163
	UINT32 trigger2                      : 1;
	UINT32:31;

	};

	INT32 value32[164];

} LTM_REG;

#endif

typedef LTM_REG LTM_REG;
