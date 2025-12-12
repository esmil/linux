/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef MMU_TOP_REG_H
#define MMU_TOP_REG_H

typedef union {
	struct {
	//REGISTER mmu_top_reg_0
	UINT32 burst_split_mode        : 1;
	UINT32 burst_split_en          : 1;
	UINT32:14;
	UINT32 rdma_timelimit          : 16;

	//REGISTER mmu_top_reg_1
	UINT32 sram0_tlb_axi_port_sel  : 2;
	UINT32 sram1_tlb_axi_port_sel  : 2;
	UINT32:28;

	//REGISTER mmu_top_reg_2
	UINT32 dmac0_rd_outs_num       : 8;
	UINT32 dmac0_arregion          : 4;
	UINT32 dmac0_arcache           : 4;
	UINT32 dmac0_burst_length      : 3;
	UINT32:13;

	//REGISTER mmu_top_reg_3
	UINT32 dmac1_rd_outs_num       : 8;
	UINT32 dmac1_arregion          : 4;
	UINT32 dmac1_arcache           : 4;
	UINT32 dmac1_burst_length      : 3;
	UINT32:13;

	//REGISTER mmu_top_reg_4
	UINT32 axi_s0_ar_debug         : 32;

	//REGISTER mmu_top_reg_5
	UINT32 axi_m0_ar_debug         : 32;

	//REGISTER mmu_top_reg_6
	UINT32 axi_s0_aw_debug         : 32;

	//REGISTER mmu_top_reg_7
	UINT32 axi_m0_aw_debug         : 32;

	//REGISTER mmu_top_reg_8
	UINT32 axi_s1_ar_debug         : 32;

	//REGISTER mmu_top_reg_9
	UINT32 axi_m1_ar_debug         : 32;

	//REGISTER mmu_top_reg_10
	UINT32 axi_s1_aw_debug         : 32;

	//REGISTER mmu_top_reg_11
	UINT32 axi_m1_aw_debug         : 32;

	//REGISTER mmu_top_reg_12
	UINT32 tlb_cmd_num             : 20;
	UINT32 tlb_miss_num            : 12;

	//REGISTER mmu_top_reg_13
	UINT32 tlb_wait_cycle          : 16;
	UINT32:16;

	//REGISTER mmu_top_reg_14
	UINT32 tlb_wait_cycle_sel      : 7;
	UINT32:1;
	UINT32 tlb_cmd_num_sel         : 7;
	UINT32:1;
	UINT32 tlb_miss_num_sel        : 7;
	UINT32 tlb_miss_num_clr        : 1;
	UINT32:8;

	//REGISTER mmu_top_reg_15
	UINT32 tlb_wait_cycle_total    : 32;

	//REGISTER mmu_top_reg_16
	UINT32 tlb_cmd_num_total       : 32;

	//REGISTER mmu_top_reg_17
	UINT32 tlb_miss_num_total      : 32;

	//REGISTER mmu_top_reg_18
	UINT32 icg_override            : 1;
	UINT32:31;

	//REGISTER mmu_top_reg_19
	UINT32 regbank_reset           : 1;
	UINT32:31;

	//REGISTER mmu_top_reg_20
	UINT32 regbank_reset2          : 1;
	UINT32:31;

	//REGISTER mmu_top_reg_21
	UINT32 axi_safty_addr_low      : 32;

	//REGISTER mmu_top_reg_22
	UINT32 axi_safty_addr_high     : 12;
	UINT32:20;

	};

	INT32 value32[23];

} MMU_TOP_REG;

#endif
