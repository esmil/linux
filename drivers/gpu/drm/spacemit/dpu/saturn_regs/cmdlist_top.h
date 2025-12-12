/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef CMDLIST_TOP_REG_H
#define CMDLIST_TOP_REG_H

typedef union {
	struct {
	//REGISTER cmdlist_reg_0
	struct {
	UINT32:4;
	UINT32 cmdlist_ch_start_addrl          : 28;
	} cmdlist_reg_0[13];

	//REGISTER cmdlist_reg_13
	struct {
	UINT32:32;
	} cmdlist_reg_13[3];

	//REGISTER cmdlist_reg_16
	struct {
	UINT32 cmdlist_ch_start_addrh          : 6;
	UINT32:26;
	} cmdlist_reg_16[13];

	//REGISTER cmdlist_reg_29
	struct {
	UINT32:32;
	} cmdlist_reg_29[3];

	//REGISTER cmdlist_reg_32
	struct {
	UINT32 cmdlist_ch_secu_en              : 1;
	UINT32 cmdlist_ch_user_id              : 4;
	UINT32:27;
	} cmdlist_reg_32[13];

	//REGISTER cmdlist_reg_45
	UINT32 secu_cfg_icg_override           : 1;
	UINT32:31;

	//REGISTER cmdlist_reg_46
	struct {
	UINT32:32;
	} cmdlist_reg_46[2];

	//REGISTER cmdlist_reg_48
	struct {
	UINT32 cmdlist_ch_arcache              : 4;
	UINT32 cmdlist_ch_arqos                : 4;
	UINT32 cmdlist_ch_arregion             : 4;
	UINT32 cmdlist_ch_y_first              : 13;
	UINT32 cmdlist_ch_osd_dep              : 4;
	UINT32:3;
	} cmdlist_reg_48[13];

	//REGISTER cmdlist_reg_61
	struct {
	UINT32:32;
	} cmdlist_reg_61[3];

	//REGISTER cmdlist_reg_64
	struct {
	UINT32:4;
	UINT32 cmdlist_ch_cont_addrl           : 28;
	} cmdlist_reg_64[13];

	//REGISTER cmdlist_reg_77
	struct {
	UINT32:32;
	} cmdlist_reg_77[3];

	//REGISTER cmdlist_reg_80
	struct {
	UINT32 cmdlist_ch_cont_addrh           : 6;
	UINT32:26;
	} cmdlist_reg_80[13];

	//REGISTER cmdlist_reg_93
	struct {
	UINT32:32;
	} cmdlist_reg_93[3];

	//REGISTER cmdlist_reg_96
	UINT32 cmdlist_burst_len               : 5;
	UINT32 axi_port_sel                    : 2;
	UINT32 onl_arb_ratio                   : 3;
	UINT32 reorder_burst_split_en          : 1;
	UINT32 reorder_burst_split_mode        : 1;
	UINT32 reorder_osd_num                 : 6;
	UINT32:14;

	//REGISTER cmdlist_reg_97
	UINT32:32;

	//REGISTER cmdlist_reg_98
	struct {
	UINT32 cmdlist_ch_y_other              : 13;
	UINT32:19;
	} cmdlist_reg_98[13];

	//REGISTER cmdlist_reg_111
	struct {
	UINT32:32;
	} cmdlist_reg_111[3];

	//REGISTER cmdlist_reg_114
	UINT32:16;
	UINT32 cmdlist_clr_timeout_th          : 16;

	//REGISTER cmdlist_reg_115
	UINT32 cmdlist_pslverr_int_msk         : 1;
	UINT32:15;
	UINT32 cmdlist_ch_clr_timeout_int_msk  : 8;
	UINT32:8;

	//REGISTER cmdlist_reg_116
	UINT32 cmdlist_pslverr_ints            : 1;
	UINT32:15;
	UINT32 cmdlist_ch_clr_timeout_ints     : 13;
	UINT32:3;

	//REGISTER cmdlist_reg_117
	UINT32 cmdlist_pslverr_int_raw         : 1;
	UINT32:15;
	UINT32 cmdlist_ch_clr_timeout_int_raw  : 13;
	UINT32:3;

	//REGISTER cmdlist_reg_118
	struct {
	UINT32 cmdlist_ch_dbg0                 : 11;
	UINT32:5;
	UINT32 cmdlist_ch_dbg1                 : 11;
	UINT32:5;
	} cmdlist_reg_118[8];

	//REGISTER cmdlist_reg_126
	UINT32 cmdlist_dbg                     : 6;
	UINT32:26;

	//REGISTER cmdlist_reg_127
	UINT32 mem_lp_auto_en                  : 2;
	UINT32:30;

	};

	INT32 value32[128];

} CMDLIST_TOP_REG;

#endif

typedef CMDLIST_TOP_REG CMDLIST_REG;
