/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef MMU_TBU_X_REG_H
#define MMU_TBU_X_REG_H

typedef union {
	struct {
	//REGISTER mmu_tbu_reg_0
	UINT32 tbu_en                 : 1;
	UINT32 tbu_fbc_mode           : 1;
	UINT32 tbu_plane_num          : 2;
	UINT32 tbu_burst_limit_en     : 1;
	UINT32 tlb_fetch_active_en    : 1;
	UINT32:2;
	UINT32 tbu_qos                : 4;
	UINT32:20;

	//REGISTER mmu_tbu_reg_1
	UINT32 tbu_base_addr0_low     : 32;

	//REGISTER mmu_tbu_reg_2
	UINT32 tbu_base_addr0_high    : 12;
	UINT32:20;

	//REGISTER mmu_tbu_reg_3
	UINT32 tbu_base_addr1_low     : 32;

	//REGISTER mmu_tbu_reg_4
	UINT32 tbu_base_addr1_high    : 12;
	UINT32:20;

	//REGISTER mmu_tbu_reg_5
	UINT32 tbu_base_addr2_low     : 32;

	//REGISTER mmu_tbu_reg_6
	UINT32 tbu_base_addr2_high    : 12;
	UINT32:20;

	//REGISTER mmu_tbu_reg_7
	UINT32 tbu_va0                : 32;

	//REGISTER mmu_tbu_reg_8
	UINT32 tbu_va1                : 32;

	//REGISTER mmu_tbu_reg_9
	UINT32 tbu_va2                : 32;

	//REGISTER mmu_tbu_reg_10
	UINT32 tbu_size0              : 16;
	UINT32:16;

	//REGISTER mmu_tbu_reg_11
	UINT32 tbu_size1              : 16;
	UINT32:16;

	//REGISTER mmu_tbu_reg_12
	UINT32 tbu_size2              : 16;
	UINT32:16;

	//REGISTER mmu_tbu_reg_13
	UINT32 force_update_en        : 1;
	UINT32 vsync_update_en        : 1;
	UINT32 shadow_read_en         : 1;
	UINT32:29;

	//REGISTER mmu_tbu_reg_14
	UINT32 force_update_pulse     : 1;
	UINT32:31;

	//REGISTER mmu_tbu_reg_15
	UINT32 force_update_en_se     : 1;
	UINT32 vsync_update_en_se     : 1;
	UINT32 shadow_read_en_se      : 1;
	UINT32:29;

	//REGISTER mmu_tbu_reg_16
	UINT32 force_update_pulse_se  : 1;
	UINT32:31;

	//REGISTER mmu_tbu_reg_17
	UINT32 icg_override           : 1;
	UINT32:31;

	//REGISTER mmu_tbu_reg_18
	UINT32 regbank_reset          : 1;
	UINT32:31;

	//REGISTER mmu_tbu_reg_19
	UINT32 regbank_reset2         : 1;
	UINT32:31;

	//REGISTER mmu_tbu_reg_20
	UINT32 tbu_nsaid              : 16;
	UINT32:16;

	};

	INT32 value32[21];

} MMU_TBU_X_REG;

#endif
typedef MMU_TBU_X_REG MMU_TBU_REG;
