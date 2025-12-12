/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef USR_GMA_REG_H
#define USR_GMA_REG_H

typedef union {
	struct {
	//REGISTER saturn_usrgamma_reg_0
	UINT32 usr_gma_en        : 1;
	UINT32:31;

	//REGISTER saturn_usrgamma_reg_1
	UINT32 mem_lp_auto_en    : 1;
	UINT32:31;

	//REGISTER saturn_usrgamma_reg_2
	UINT32 m_ncurve_a_000    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_001    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_3
	UINT32 m_ncurve_a_002    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_003    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_4
	UINT32 m_ncurve_a_004    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_005    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_5
	UINT32 m_ncurve_a_006    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_007    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_6
	UINT32 m_ncurve_a_008    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_009    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_7
	UINT32 m_ncurve_a_010    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_011    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_8
	UINT32 m_ncurve_a_012    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_013    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_9
	UINT32 m_ncurve_a_014    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_015    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_10
	UINT32 m_ncurve_a_016    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_017    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_11
	UINT32 m_ncurve_a_018    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_019    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_12
	UINT32 m_ncurve_a_020    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_021    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_13
	UINT32 m_ncurve_a_022    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_023    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_14
	UINT32 m_ncurve_a_024    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_025    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_15
	UINT32 m_ncurve_a_026    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_027    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_16
	UINT32 m_ncurve_a_028    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_029    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_17
	UINT32 m_ncurve_a_030    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_031    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_18
	UINT32 m_ncurve_a_032    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_033    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_19
	UINT32 m_ncurve_a_034    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_035    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_20
	UINT32 m_ncurve_a_036    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_037    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_21
	UINT32 m_ncurve_a_038    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_039    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_22
	UINT32 m_ncurve_a_040    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_041    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_23
	UINT32 m_ncurve_a_042    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_043    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_24
	UINT32 m_ncurve_a_044    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_045    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_25
	UINT32 m_ncurve_a_046    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_047    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_26
	UINT32 m_ncurve_a_048    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_049    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_27
	UINT32 m_ncurve_a_050    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_051    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_28
	UINT32 m_ncurve_a_052    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_053    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_29
	UINT32 m_ncurve_a_054    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_055    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_30
	UINT32 m_ncurve_a_056    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_057    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_31
	UINT32 m_ncurve_a_058    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_059    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_32
	UINT32 m_ncurve_a_060    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_061    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_33
	UINT32 m_ncurve_a_062    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_063    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_34
	UINT32 m_ncurve_a_064    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_065    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_35
	UINT32 m_ncurve_a_066    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_067    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_36
	UINT32 m_ncurve_a_068    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_069    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_37
	UINT32 m_ncurve_a_070    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_071    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_38
	UINT32 m_ncurve_a_072    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_073    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_39
	UINT32 m_ncurve_a_074    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_075    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_40
	UINT32 m_ncurve_a_076    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_077    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_41
	UINT32 m_ncurve_a_078    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_079    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_42
	UINT32 m_ncurve_a_080    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_081    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_43
	UINT32 m_ncurve_a_082    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_083    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_44
	UINT32 m_ncurve_a_084    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_085    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_45
	UINT32 m_ncurve_a_086    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_087    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_46
	UINT32 m_ncurve_a_088    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_089    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_47
	UINT32 m_ncurve_a_090    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_091    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_48
	UINT32 m_ncurve_a_092    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_093    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_49
	UINT32 m_ncurve_a_094    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_095    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_50
	UINT32 m_ncurve_a_096    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_097    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_51
	UINT32 m_ncurve_a_098    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_099    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_52
	UINT32 m_ncurve_a_100    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_101    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_53
	UINT32 m_ncurve_a_102    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_103    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_54
	UINT32 m_ncurve_a_104    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_105    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_55
	UINT32 m_ncurve_a_106    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_107    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_56
	UINT32 m_ncurve_a_108    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_109    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_57
	UINT32 m_ncurve_a_110    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_111    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_58
	UINT32 m_ncurve_a_112    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_113    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_59
	UINT32 m_ncurve_a_114    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_115    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_60
	UINT32 m_ncurve_a_116    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_117    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_61
	UINT32 m_ncurve_a_118    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_119    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_62
	UINT32 m_ncurve_a_120    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_121    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_63
	UINT32 m_ncurve_a_122    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_123    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_64
	UINT32 m_ncurve_a_124    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_125    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_65
	UINT32 m_ncurve_a_126    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_127    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_66
	UINT32 m_ncurve_a_128    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_129    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_67
	UINT32 m_ncurve_a_130    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_131    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_68
	UINT32 m_ncurve_a_132    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_133    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_69
	UINT32 m_ncurve_a_134    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_135    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_70
	UINT32 m_ncurve_a_136    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_137    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_71
	UINT32 m_ncurve_a_138    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_139    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_72
	UINT32 m_ncurve_a_140    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_141    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_73
	UINT32 m_ncurve_a_142    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_143    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_74
	UINT32 m_ncurve_a_144    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_145    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_75
	UINT32 m_ncurve_a_146    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_147    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_76
	UINT32 m_ncurve_a_148    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_149    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_77
	UINT32 m_ncurve_a_150    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_151    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_78
	UINT32 m_ncurve_a_152    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_153    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_79
	UINT32 m_ncurve_a_154    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_155    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_80
	UINT32 m_ncurve_a_156    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_157    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_81
	UINT32 m_ncurve_a_158    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_159    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_82
	UINT32 m_ncurve_a_160    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_161    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_83
	UINT32 m_ncurve_a_162    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_163    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_84
	UINT32 m_ncurve_a_164    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_165    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_85
	UINT32 m_ncurve_a_166    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_167    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_86
	UINT32 m_ncurve_a_168    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_169    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_87
	UINT32 m_ncurve_a_170    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_171    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_88
	UINT32 m_ncurve_a_172    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_173    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_89
	UINT32 m_ncurve_a_174    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_175    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_90
	UINT32 m_ncurve_a_176    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_177    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_91
	UINT32 m_ncurve_a_178    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_179    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_92
	UINT32 m_ncurve_a_180    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_181    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_93
	UINT32 m_ncurve_a_182    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_183    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_94
	UINT32 m_ncurve_a_184    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_185    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_95
	UINT32 m_ncurve_a_186    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_187    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_96
	UINT32 m_ncurve_a_188    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_189    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_97
	UINT32 m_ncurve_a_190    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_191    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_98
	UINT32 m_ncurve_a_192    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_193    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_99
	UINT32 m_ncurve_a_194    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_195    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_100
	UINT32 m_ncurve_a_196    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_197    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_101
	UINT32 m_ncurve_a_198    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_199    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_102
	UINT32 m_ncurve_a_200    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_201    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_103
	UINT32 m_ncurve_a_202    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_203    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_104
	UINT32 m_ncurve_a_204    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_205    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_105
	UINT32 m_ncurve_a_206    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_207    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_106
	UINT32 m_ncurve_a_208    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_209    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_107
	UINT32 m_ncurve_a_210    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_211    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_108
	UINT32 m_ncurve_a_212    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_213    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_109
	UINT32 m_ncurve_a_214    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_215    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_110
	UINT32 m_ncurve_a_216    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_217    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_111
	UINT32 m_ncurve_a_218    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_219    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_112
	UINT32 m_ncurve_a_220    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_221    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_113
	UINT32 m_ncurve_a_222    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_223    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_114
	UINT32 m_ncurve_a_224    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_225    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_115
	UINT32 m_ncurve_a_226    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_227    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_116
	UINT32 m_ncurve_a_228    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_229    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_117
	UINT32 m_ncurve_a_230    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_231    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_118
	UINT32 m_ncurve_a_232    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_233    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_119
	UINT32 m_ncurve_a_234    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_235    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_120
	UINT32 m_ncurve_a_236    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_237    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_121
	UINT32 m_ncurve_a_238    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_239    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_122
	UINT32 m_ncurve_a_240    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_241    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_123
	UINT32 m_ncurve_a_242    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_243    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_124
	UINT32 m_ncurve_a_244    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_245    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_125
	UINT32 m_ncurve_a_246    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_247    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_126
	UINT32 m_ncurve_a_248    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_249    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_127
	UINT32 m_ncurve_a_250    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_251    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_128
	UINT32 m_ncurve_a_252    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_253    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_129
	UINT32 m_ncurve_a_254    : 10;
	UINT32:6;
	UINT32 m_ncurve_a_255    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_130
	UINT32 m_ncurve_a_256    : 10;
	UINT32:22;

	//REGISTER saturn_usrgamma_reg_131
	UINT32 m_ncurve_b_000    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_001    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_132
	UINT32 m_ncurve_b_002    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_003    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_133
	UINT32 m_ncurve_b_004    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_005    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_134
	UINT32 m_ncurve_b_006    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_007    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_135
	UINT32 m_ncurve_b_008    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_009    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_136
	UINT32 m_ncurve_b_010    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_011    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_137
	UINT32 m_ncurve_b_012    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_013    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_138
	UINT32 m_ncurve_b_014    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_015    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_139
	UINT32 m_ncurve_b_016    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_017    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_140
	UINT32 m_ncurve_b_018    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_019    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_141
	UINT32 m_ncurve_b_020    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_021    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_142
	UINT32 m_ncurve_b_022    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_023    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_143
	UINT32 m_ncurve_b_024    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_025    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_144
	UINT32 m_ncurve_b_026    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_027    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_145
	UINT32 m_ncurve_b_028    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_029    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_146
	UINT32 m_ncurve_b_030    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_031    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_147
	UINT32 m_ncurve_b_032    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_033    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_148
	UINT32 m_ncurve_b_034    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_035    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_149
	UINT32 m_ncurve_b_036    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_037    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_150
	UINT32 m_ncurve_b_038    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_039    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_151
	UINT32 m_ncurve_b_040    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_041    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_152
	UINT32 m_ncurve_b_042    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_043    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_153
	UINT32 m_ncurve_b_044    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_045    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_154
	UINT32 m_ncurve_b_046    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_047    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_155
	UINT32 m_ncurve_b_048    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_049    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_156
	UINT32 m_ncurve_b_050    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_051    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_157
	UINT32 m_ncurve_b_052    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_053    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_158
	UINT32 m_ncurve_b_054    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_055    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_159
	UINT32 m_ncurve_b_056    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_057    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_160
	UINT32 m_ncurve_b_058    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_059    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_161
	UINT32 m_ncurve_b_060    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_061    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_162
	UINT32 m_ncurve_b_062    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_063    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_163
	UINT32 m_ncurve_b_064    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_065    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_164
	UINT32 m_ncurve_b_066    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_067    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_165
	UINT32 m_ncurve_b_068    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_069    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_166
	UINT32 m_ncurve_b_070    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_071    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_167
	UINT32 m_ncurve_b_072    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_073    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_168
	UINT32 m_ncurve_b_074    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_075    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_169
	UINT32 m_ncurve_b_076    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_077    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_170
	UINT32 m_ncurve_b_078    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_079    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_171
	UINT32 m_ncurve_b_080    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_081    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_172
	UINT32 m_ncurve_b_082    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_083    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_173
	UINT32 m_ncurve_b_084    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_085    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_174
	UINT32 m_ncurve_b_086    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_087    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_175
	UINT32 m_ncurve_b_088    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_089    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_176
	UINT32 m_ncurve_b_090    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_091    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_177
	UINT32 m_ncurve_b_092    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_093    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_178
	UINT32 m_ncurve_b_094    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_095    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_179
	UINT32 m_ncurve_b_096    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_097    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_180
	UINT32 m_ncurve_b_098    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_099    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_181
	UINT32 m_ncurve_b_100    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_101    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_182
	UINT32 m_ncurve_b_102    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_103    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_183
	UINT32 m_ncurve_b_104    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_105    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_184
	UINT32 m_ncurve_b_106    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_107    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_185
	UINT32 m_ncurve_b_108    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_109    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_186
	UINT32 m_ncurve_b_110    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_111    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_187
	UINT32 m_ncurve_b_112    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_113    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_188
	UINT32 m_ncurve_b_114    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_115    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_189
	UINT32 m_ncurve_b_116    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_117    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_190
	UINT32 m_ncurve_b_118    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_119    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_191
	UINT32 m_ncurve_b_120    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_121    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_192
	UINT32 m_ncurve_b_122    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_123    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_193
	UINT32 m_ncurve_b_124    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_125    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_194
	UINT32 m_ncurve_b_126    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_127    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_195
	UINT32 m_ncurve_b_128    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_129    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_196
	UINT32 m_ncurve_b_130    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_131    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_197
	UINT32 m_ncurve_b_132    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_133    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_198
	UINT32 m_ncurve_b_134    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_135    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_199
	UINT32 m_ncurve_b_136    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_137    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_200
	UINT32 m_ncurve_b_138    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_139    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_201
	UINT32 m_ncurve_b_140    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_141    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_202
	UINT32 m_ncurve_b_142    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_143    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_203
	UINT32 m_ncurve_b_144    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_145    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_204
	UINT32 m_ncurve_b_146    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_147    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_205
	UINT32 m_ncurve_b_148    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_149    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_206
	UINT32 m_ncurve_b_150    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_151    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_207
	UINT32 m_ncurve_b_152    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_153    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_208
	UINT32 m_ncurve_b_154    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_155    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_209
	UINT32 m_ncurve_b_156    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_157    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_210
	UINT32 m_ncurve_b_158    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_159    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_211
	UINT32 m_ncurve_b_160    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_161    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_212
	UINT32 m_ncurve_b_162    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_163    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_213
	UINT32 m_ncurve_b_164    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_165    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_214
	UINT32 m_ncurve_b_166    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_167    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_215
	UINT32 m_ncurve_b_168    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_169    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_216
	UINT32 m_ncurve_b_170    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_171    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_217
	UINT32 m_ncurve_b_172    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_173    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_218
	UINT32 m_ncurve_b_174    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_175    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_219
	UINT32 m_ncurve_b_176    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_177    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_220
	UINT32 m_ncurve_b_178    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_179    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_221
	UINT32 m_ncurve_b_180    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_181    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_222
	UINT32 m_ncurve_b_182    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_183    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_223
	UINT32 m_ncurve_b_184    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_185    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_224
	UINT32 m_ncurve_b_186    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_187    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_225
	UINT32 m_ncurve_b_188    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_189    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_226
	UINT32 m_ncurve_b_190    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_191    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_227
	UINT32 m_ncurve_b_192    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_193    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_228
	UINT32 m_ncurve_b_194    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_195    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_229
	UINT32 m_ncurve_b_196    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_197    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_230
	UINT32 m_ncurve_b_198    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_199    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_231
	UINT32 m_ncurve_b_200    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_201    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_232
	UINT32 m_ncurve_b_202    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_203    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_233
	UINT32 m_ncurve_b_204    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_205    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_234
	UINT32 m_ncurve_b_206    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_207    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_235
	UINT32 m_ncurve_b_208    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_209    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_236
	UINT32 m_ncurve_b_210    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_211    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_237
	UINT32 m_ncurve_b_212    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_213    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_238
	UINT32 m_ncurve_b_214    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_215    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_239
	UINT32 m_ncurve_b_216    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_217    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_240
	UINT32 m_ncurve_b_218    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_219    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_241
	UINT32 m_ncurve_b_220    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_221    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_242
	UINT32 m_ncurve_b_222    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_223    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_243
	UINT32 m_ncurve_b_224    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_225    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_244
	UINT32 m_ncurve_b_226    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_227    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_245
	UINT32 m_ncurve_b_228    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_229    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_246
	UINT32 m_ncurve_b_230    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_231    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_247
	UINT32 m_ncurve_b_232    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_233    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_248
	UINT32 m_ncurve_b_234    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_235    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_249
	UINT32 m_ncurve_b_236    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_237    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_250
	UINT32 m_ncurve_b_238    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_239    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_251
	UINT32 m_ncurve_b_240    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_241    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_252
	UINT32 m_ncurve_b_242    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_243    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_253
	UINT32 m_ncurve_b_244    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_245    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_254
	UINT32 m_ncurve_b_246    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_247    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_255
	UINT32 m_ncurve_b_248    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_249    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_256
	UINT32 m_ncurve_b_250    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_251    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_257
	UINT32 m_ncurve_b_252    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_253    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_258
	UINT32 m_ncurve_b_254    : 10;
	UINT32:6;
	UINT32 m_ncurve_b_255    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_259
	UINT32 m_ncurve_b_256    : 10;
	UINT32:22;

	//REGISTER saturn_usrgamma_reg_260
	UINT32 m_ncurve_c_000    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_001    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_261
	UINT32 m_ncurve_c_002    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_003    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_262
	UINT32 m_ncurve_c_004    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_005    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_263
	UINT32 m_ncurve_c_006    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_007    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_264
	UINT32 m_ncurve_c_008    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_009    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_265
	UINT32 m_ncurve_c_010    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_011    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_266
	UINT32 m_ncurve_c_012    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_013    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_267
	UINT32 m_ncurve_c_014    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_015    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_268
	UINT32 m_ncurve_c_016    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_017    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_269
	UINT32 m_ncurve_c_018    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_019    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_270
	UINT32 m_ncurve_c_020    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_021    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_271
	UINT32 m_ncurve_c_022    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_023    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_272
	UINT32 m_ncurve_c_024    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_025    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_273
	UINT32 m_ncurve_c_026    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_027    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_274
	UINT32 m_ncurve_c_028    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_029    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_275
	UINT32 m_ncurve_c_030    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_031    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_276
	UINT32 m_ncurve_c_032    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_033    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_277
	UINT32 m_ncurve_c_034    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_035    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_278
	UINT32 m_ncurve_c_036    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_037    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_279
	UINT32 m_ncurve_c_038    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_039    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_280
	UINT32 m_ncurve_c_040    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_041    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_281
	UINT32 m_ncurve_c_042    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_043    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_282
	UINT32 m_ncurve_c_044    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_045    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_283
	UINT32 m_ncurve_c_046    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_047    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_284
	UINT32 m_ncurve_c_048    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_049    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_285
	UINT32 m_ncurve_c_050    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_051    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_286
	UINT32 m_ncurve_c_052    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_053    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_287
	UINT32 m_ncurve_c_054    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_055    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_288
	UINT32 m_ncurve_c_056    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_057    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_289
	UINT32 m_ncurve_c_058    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_059    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_290
	UINT32 m_ncurve_c_060    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_061    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_291
	UINT32 m_ncurve_c_062    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_063    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_292
	UINT32 m_ncurve_c_064    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_065    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_293
	UINT32 m_ncurve_c_066    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_067    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_294
	UINT32 m_ncurve_c_068    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_069    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_295
	UINT32 m_ncurve_c_070    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_071    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_296
	UINT32 m_ncurve_c_072    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_073    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_297
	UINT32 m_ncurve_c_074    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_075    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_298
	UINT32 m_ncurve_c_076    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_077    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_299
	UINT32 m_ncurve_c_078    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_079    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_300
	UINT32 m_ncurve_c_080    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_081    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_301
	UINT32 m_ncurve_c_082    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_083    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_302
	UINT32 m_ncurve_c_084    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_085    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_303
	UINT32 m_ncurve_c_086    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_087    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_304
	UINT32 m_ncurve_c_088    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_089    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_305
	UINT32 m_ncurve_c_090    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_091    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_306
	UINT32 m_ncurve_c_092    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_093    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_307
	UINT32 m_ncurve_c_094    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_095    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_308
	UINT32 m_ncurve_c_096    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_097    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_309
	UINT32 m_ncurve_c_098    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_099    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_310
	UINT32 m_ncurve_c_100    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_101    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_311
	UINT32 m_ncurve_c_102    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_103    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_312
	UINT32 m_ncurve_c_104    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_105    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_313
	UINT32 m_ncurve_c_106    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_107    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_314
	UINT32 m_ncurve_c_108    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_109    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_315
	UINT32 m_ncurve_c_110    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_111    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_316
	UINT32 m_ncurve_c_112    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_113    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_317
	UINT32 m_ncurve_c_114    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_115    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_318
	UINT32 m_ncurve_c_116    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_117    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_319
	UINT32 m_ncurve_c_118    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_119    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_320
	UINT32 m_ncurve_c_120    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_121    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_321
	UINT32 m_ncurve_c_122    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_123    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_322
	UINT32 m_ncurve_c_124    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_125    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_323
	UINT32 m_ncurve_c_126    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_127    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_324
	UINT32 m_ncurve_c_128    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_129    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_325
	UINT32 m_ncurve_c_130    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_131    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_326
	UINT32 m_ncurve_c_132    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_133    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_327
	UINT32 m_ncurve_c_134    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_135    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_328
	UINT32 m_ncurve_c_136    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_137    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_329
	UINT32 m_ncurve_c_138    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_139    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_330
	UINT32 m_ncurve_c_140    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_141    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_331
	UINT32 m_ncurve_c_142    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_143    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_332
	UINT32 m_ncurve_c_144    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_145    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_333
	UINT32 m_ncurve_c_146    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_147    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_334
	UINT32 m_ncurve_c_148    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_149    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_335
	UINT32 m_ncurve_c_150    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_151    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_336
	UINT32 m_ncurve_c_152    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_153    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_337
	UINT32 m_ncurve_c_154    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_155    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_338
	UINT32 m_ncurve_c_156    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_157    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_339
	UINT32 m_ncurve_c_158    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_159    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_340
	UINT32 m_ncurve_c_160    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_161    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_341
	UINT32 m_ncurve_c_162    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_163    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_342
	UINT32 m_ncurve_c_164    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_165    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_343
	UINT32 m_ncurve_c_166    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_167    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_344
	UINT32 m_ncurve_c_168    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_169    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_345
	UINT32 m_ncurve_c_170    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_171    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_346
	UINT32 m_ncurve_c_172    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_173    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_347
	UINT32 m_ncurve_c_174    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_175    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_348
	UINT32 m_ncurve_c_176    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_177    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_349
	UINT32 m_ncurve_c_178    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_179    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_350
	UINT32 m_ncurve_c_180    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_181    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_351
	UINT32 m_ncurve_c_182    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_183    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_352
	UINT32 m_ncurve_c_184    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_185    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_353
	UINT32 m_ncurve_c_186    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_187    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_354
	UINT32 m_ncurve_c_188    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_189    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_355
	UINT32 m_ncurve_c_190    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_191    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_356
	UINT32 m_ncurve_c_192    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_193    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_357
	UINT32 m_ncurve_c_194    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_195    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_358
	UINT32 m_ncurve_c_196    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_197    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_359
	UINT32 m_ncurve_c_198    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_199    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_360
	UINT32 m_ncurve_c_200    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_201    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_361
	UINT32 m_ncurve_c_202    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_203    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_362
	UINT32 m_ncurve_c_204    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_205    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_363
	UINT32 m_ncurve_c_206    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_207    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_364
	UINT32 m_ncurve_c_208    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_209    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_365
	UINT32 m_ncurve_c_210    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_211    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_366
	UINT32 m_ncurve_c_212    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_213    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_367
	UINT32 m_ncurve_c_214    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_215    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_368
	UINT32 m_ncurve_c_216    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_217    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_369
	UINT32 m_ncurve_c_218    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_219    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_370
	UINT32 m_ncurve_c_220    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_221    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_371
	UINT32 m_ncurve_c_222    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_223    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_372
	UINT32 m_ncurve_c_224    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_225    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_373
	UINT32 m_ncurve_c_226    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_227    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_374
	UINT32 m_ncurve_c_228    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_229    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_375
	UINT32 m_ncurve_c_230    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_231    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_376
	UINT32 m_ncurve_c_232    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_233    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_377
	UINT32 m_ncurve_c_234    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_235    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_378
	UINT32 m_ncurve_c_236    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_237    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_379
	UINT32 m_ncurve_c_238    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_239    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_380
	UINT32 m_ncurve_c_240    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_241    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_381
	UINT32 m_ncurve_c_242    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_243    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_382
	UINT32 m_ncurve_c_244    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_245    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_383
	UINT32 m_ncurve_c_246    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_247    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_384
	UINT32 m_ncurve_c_248    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_249    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_385
	UINT32 m_ncurve_c_250    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_251    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_386
	UINT32 m_ncurve_c_252    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_253    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_387
	UINT32 m_ncurve_c_254    : 10;
	UINT32:6;
	UINT32 m_ncurve_c_255    : 10;
	UINT32:6;

	//REGISTER saturn_usrgamma_reg_388
	UINT32 m_ncurve_c_256    : 10;
	UINT32:22;

	//REGISTER saturn_usrgamma_reg_389
	UINT32 usr_gma_cfg_done  : 1;
	UINT32:31;

	};

	INT32 value32[390];

} USR_GMA_REG;

#endif
