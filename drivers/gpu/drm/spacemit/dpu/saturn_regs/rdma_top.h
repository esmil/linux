/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Spacemit Co., Ltd.
 *
 */

#ifndef RDMA_TOP_REG_H
#define RDMA_TOP_REG_H

typedef union {
	struct {
	//REGISTER saturn_dma_top_reg_0
	UINT32 dbg_en                  : 1;
	UINT32:31;

	//REGISTER saturn_dma_top_reg_1
	UINT32 image_rr_ratio          : 8;
	UINT32 round_robin_mode        : 1;
	UINT32 pixel_num_th            : 6;
	UINT32:17;

	//REGISTER saturn_dma_top_reg_2
	UINT32 rdma_timeout_limit      : 16;
	UINT32 wdma_timeout_limit      : 16;

	//REGISTER saturn_dma_top_reg_3
	UINT32 dmac0_rstn_pwr          : 1;
	UINT32 dmac0_rst_req           : 1;
	UINT32 dmac0_burst_length      : 3;
	UINT32:3;
	UINT32 dmac0_arcache           : 4;
	UINT32 dmac0_awcache           : 4;
	UINT32 dmac0_arregion          : 4;
	UINT32 dmac0_awregion          : 4;
	UINT32:8;

	//REGISTER saturn_dma_top_reg_4
	UINT32 dmac1_rstn_pwr          : 1;
	UINT32 dmac1_rst_req           : 1;
	UINT32 dmac1_burst_length      : 3;
	UINT32:3;
	UINT32 dmac1_arcache           : 4;
	UINT32 dmac1_awcache           : 4;
	UINT32 dmac1_arregion          : 4;
	UINT32 dmac1_awregion          : 4;
	UINT32:8;

	//REGISTER saturn_dma_top_reg_5
	UINT32 dmac2_rstn_pwr          : 1;
	UINT32 dmac2_rst_req           : 1;
	UINT32 dmac2_burst_length      : 3;
	UINT32:3;
	UINT32 dmac2_arcache           : 4;
	UINT32 dmac2_awcache           : 4;
	UINT32 dmac2_arregion          : 4;
	UINT32 dmac2_awregion          : 4;
	UINT32:8;

	//REGISTER saturn_dma_top_reg_6
	UINT32 dmac3_rstn_pwr          : 1;
	UINT32 dmac3_rst_req           : 1;
	UINT32 dmac3_burst_length      : 3;
	UINT32:3;
	UINT32 dmac3_arcache           : 4;
	UINT32 dmac3_awcache           : 4;
	UINT32 dmac3_arregion          : 4;
	UINT32 dmac3_awregion          : 4;
	UINT32:8;

	//REGISTER saturn_dma_top_reg_7
	UINT32 online_rqos             : 4;
	UINT32 offline_rqos            : 4;
	UINT32 online_wqos             : 4;
	UINT32 offline_wqos            : 4;
	UINT32 cmdlist_rqos            : 4;
	UINT32:12;

	//REGISTER saturn_dma_top_reg_8
	UINT32 dmac0_rd_oust_num       : 8;
	UINT32 dmac0_wr_oust_num       : 8;
	UINT32:16;

	//REGISTER saturn_dma_top_reg_9
	UINT32 dmac1_rd_oust_num       : 8;
	UINT32 dmac1_wr_oust_num       : 8;
	UINT32:16;

	//REGISTER saturn_dma_top_reg_10
	UINT32 dmac2_rd_oust_num       : 8;
	UINT32 dmac2_wr_oust_num       : 8;
	UINT32:16;

	//REGISTER saturn_dma_top_reg_11
	UINT32 dmac3_rd_oust_num       : 8;
	UINT32 dmac3_wr_oust_num       : 8;
	UINT32:16;

	//REGISTER saturn_dma_top_reg_12
	UINT32 cmdlist_dbg_irq_raw     : 32;

	//REGISTER saturn_dma_top_reg_13
	UINT32 cmdlist_dbg_irq_mask    : 32;

	//REGISTER saturn_dma_top_reg_14
	UINT32 cmdlist_dbg_irq_status  : 32;

	//REGISTER saturn_dma_top_reg_15
	UINT32 wb0_dbg_irq_raw         : 32;

	//REGISTER saturn_dma_top_reg_16
	UINT32 wb0_dbg_irq_mask        : 32;

	//REGISTER saturn_dma_top_reg_17
	UINT32 wb0_dbg_irq_status      : 32;

	//REGISTER saturn_dma_top_reg_18
	UINT32 arb_debug_info_axi0     : 32;

	//REGISTER saturn_dma_top_reg_19
	UINT32 arb_debug_info_axi1     : 32;

	//REGISTER saturn_dma_top_reg_20
	UINT32 arb_debug_info_axi2     : 32;

	//REGISTER saturn_dma_top_reg_21
	UINT32 arb_debug_info_axi3     : 32;

	//REGISTER saturn_dma_top_reg_22
	UINT32 icg_override            : 1;
	UINT32:31;

	//REGISTER saturn_dma_top_reg_23
	UINT32 regbank_reset           : 1;
	UINT32:31;

	//REGISTER saturn_dma_top_reg_24
	UINT32 regbank_reset2          : 1;
	UINT32:31;

	};

	INT32 value32[25];

} RDMA_TOP_REG;

#endif

typedef RDMA_TOP_REG DMA_TOP_REG;